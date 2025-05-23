#include <chrono>
#include <numeric>

#include <rclcpp/rclcpp.hpp>
// ros msgs
#include <nav_msgs/msg/odometry.hpp>

// Pcl
#include <pcl/point_types.h>
#include <pcl_conversions/pcl_conversions.h>
#include <velodyne_pointcloud/point_types.h>

// ros opencv transport
#include <image_transport/image_transport.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <cv_bridge/cv_bridge.hpp>

// ros tf
#include <tf2_ros/transform_listener.h>

// grid map
#include <grid_map_msgs/msg/grid_map.hpp>
#include <grid_map_ros/grid_map_ros.hpp>
#include <grid_map_cv/grid_map_cv.hpp>

#include <groundgrid/GroundGrid.h>
// #include <groundgrid/GroundGridFwd.h>
#include <groundgrid/GroundSegmentation.h>

namespace groundgrid {


class GroundGridNode : public rclcpp::Node {
public:
    typedef velodyne_pointcloud::PointXYZIR PCLPoint;

    GroundGridNode(const rclcpp::NodeOptions & options) : Node("groundgrid_node", options), 
    mTfBuffer_(this->get_clock()), mTfListener_(mTfBuffer_) 
    {
        groundgrid_ = std::make_shared<GroundGrid>();
        ground_segmentation_.init(groundgrid_->mDimension, groundgrid_->mResolution);

        // Initialize publishers and subscribers
        // image_transport::ImageTransport it(shared_from_this());
        // grid_map_cv_img_pub_ = it.advertise("groundgrid/grid_map_cv", 1);
        // terrain_im_pub_ = it.advertise("groundgrid/terrain", 1);
        grid_map_pub_ = this->create_publisher<grid_map_msgs::msg::GridMap>("groundgrid/grid_map", 1);
        filtered_cloud_pub_ = this->create_publisher<sensor_msgs::msg::PointCloud2>("groundgrid/segmented_cloud", 1);        

        pos_sub_ = this->create_subscription<nav_msgs::msg::Odometry>(
            "dlio/odom_node/odom", 1, std::bind(&GroundGridNode::odom_callback, this, std::placeholders::_1));
        auto point_cloud_qos = rclcpp::QoS(1);
        point_cloud_qos.reliability(RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT);
        points_sub_ = this->create_subscription<sensor_msgs::msg::PointCloud2>(
            "ouster/points", point_cloud_qos, std::bind(&GroundGridNode::points_callback, this, std::placeholders::_1));

        rclcpp::TimerBase::SharedPtr timer = this->create_wall_timer(
            std::chrono::milliseconds(100), std::bind(&GroundGridNode::transform_callback, this));
        
        
    }

protected:
    void transform_callback(){
        try {
            mapToBaseTransform_ = mTfBuffer_.lookupTransform("odom", "base_link", tf2::TimePointZero);
            cloudOriginTransform_ = mTfBuffer_.lookupTransform("odom", "os_lidar", tf2::TimePointZero);
        }
        catch (const tf2::TransformException &ex) {
            RCLCPP_WARN(this->get_logger(), "Failed to get transforms in GroundGridNode.cpp: %s", ex.what());
            return;
        }
    }

    void odom_callback(const nav_msgs::msg::Odometry::SharedPtr inOdom) {
        RCLCPP_INFO(this->get_logger(), "Received odometry message");
        auto start = std::chrono::steady_clock::now();
        map_ptr_ = groundgrid_->update(inOdom, mapToBaseTransform_);
        auto end = std::chrono::steady_clock::now();
        RCLCPP_DEBUG(this->get_logger(), "Grid map update took %ld ms", 
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
    }

    void points_callback(const sensor_msgs::msg::PointCloud2::SharedPtr cloud_msg) {
        RCLCPP_INFO(this->get_logger(), "Received point cloud message");
        auto start = std::chrono::steady_clock::now();
        static size_t time_vals = 0;
        static double avg_time = 0.0;
        static double avg_cpu_time = 0.0;
        pcl::PointCloud<velodyne_pointcloud::PointXYZIR>::Ptr cloud(new pcl::PointCloud<velodyne_pointcloud::PointXYZIR>);
        pcl::fromROSMsg (*cloud_msg, *cloud);

        // Map not initialized yet, this means the node hasn't received any odom message so far.
        if(!map_ptr_)
            return;

        geometry_msgs::msg::PointStamped origin;
        origin.header = cloud_msg->header;
        origin.header.frame_id = "os_lidar";
        origin.point.x = 0.0f;
        origin.point.y = 0.0f;
        origin.point.z = 0.0f;

        tf2::doTransform(origin, origin, cloudOriginTransform_);

        // Transform cloud into map coordinate system
        if(cloud_msg->header.frame_id != "odom"){
            geometry_msgs::msg::TransformStamped transformStamped;
            pcl::PointCloud<velodyne_pointcloud::PointXYZIR>::Ptr transformed_cloud(new pcl::PointCloud<velodyne_pointcloud::PointXYZIR>);
            transformed_cloud->header = cloud->header;
            transformed_cloud->header.frame_id = "odom";
            transformed_cloud->points.reserve(cloud->points.size());

            try {
                mTfBuffer_.canTransform("odom", cloud_msg->header.frame_id, tf2::TimePointZero);
                transformStamped = mTfBuffer_.lookupTransform("odom", cloud_msg->header.frame_id, tf2::TimePointZero);
            }
            catch (const tf2::TransformException &ex) {
                RCLCPP_WARN(this->get_logger(), "Failed to get map transform for point cloud transformation: %s", ex.what());
                return;
            }

            geometry_msgs::msg::PointStamped psIn;
            psIn.header = cloud_msg->header;
            psIn.header.frame_id = "odom";

            for(const auto& point : cloud->points){
                psIn.point.x = point.x;
                psIn.point.y = point.y;
                psIn.point.z = point.z;

                tf2::doTransform(psIn, psIn, transformStamped);

                PCLPoint& point_transformed = transformed_cloud->points.emplace_back(point);
                point_transformed.x = psIn.point.x;
                point_transformed.y = psIn.point.y;
                point_transformed.z = psIn.point.z;
            }

            cloud = transformed_cloud;
        }

        auto end = std::chrono::steady_clock::now();
        RCLCPP_DEBUG(this->get_logger(), "cloud transformation took %ld ms", 
                    std::chrono::duration_cast<std::chrono::milliseconds>(end-start).count());

        auto start2 = std::chrono::steady_clock::now();
        std::clock_t c_clock = std::clock();
        sensor_msgs::msg::PointCloud2 cloud_msg_out;
        PCLPoint origin_pclPoint;
        origin_pclPoint.x = origin.point.x;
        origin_pclPoint.y = origin.point.y;
        origin_pclPoint.z = origin.point.z;
        pcl::toROSMsg(*(ground_segmentation_.filter_cloud(cloud, origin_pclPoint, mapToBaseTransform_, *map_ptr_)), cloud_msg_out);

        cloud_msg_out.header = cloud_msg->header;
        cloud_msg_out.header.frame_id = "odom";
        filtered_cloud_pub_->publish(cloud_msg_out);

        end = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed_seconds = end - start2;
        const double milliseconds = elapsed_seconds.count() * 1000;
        const double c_millis = double(std::clock() - c_clock)/CLOCKS_PER_SEC * 1000;
        avg_time = (milliseconds + time_vals * avg_time)/(time_vals+1);
        avg_cpu_time = (c_millis + time_vals * avg_cpu_time)/(time_vals+1);
        ++time_vals;
        RCLCPP_INFO(this->get_logger(), "groundgrid took %.3f ms (avg: %.3f ms)", milliseconds, avg_time);
        RCLCPP_DEBUG(this->get_logger(), "total cpu time used: %.3f ms (avg: %.3f ms)", c_millis, avg_cpu_time);

        grid_map_msgs::msg::GridMap grid_map_msg = *grid_map::GridMapRosConverter::toMessage(*map_ptr_);
        grid_map_msg.header.stamp = cloud_msg->header.stamp;
        grid_map_pub_->publish(grid_map_msg);

        image_transport::ImageTransport it(shared_from_this());
        for(const auto& layer : map_ptr_->getLayers()){
            if(layer_pubs_.find(layer) == layer_pubs_.end()){
                layer_pubs_[layer] = it.advertise("/groundgrid/grid_map_cv_" + layer, 1);
            }
            publish_grid_map_layer(layer_pubs_.at(layer), layer, cloud_msg->header.stamp);
        }

        if(terrain_im_pub_.getNumSubscribers()){
            publish_grid_map_layer(terrain_im_pub_, "terrain", cloud_msg->header.stamp);
        }

        end = std::chrono::steady_clock::now();
        RCLCPP_DEBUG(this->get_logger(), "overall %ld ms", std::chrono::duration_cast<std::chrono::milliseconds>(end-start).count());
    }

    void publish_grid_map_layer(const image_transport::Publisher &pub, const std::string &layer_name,
        const rclcpp::Time &stamp = rclcpp::Clock().now()) {

        cv::Mat img, normalized_img, color_img, mask;

        if (pub.getNumSubscribers() > 0) {
            if (layer_name != "terrain") {
                const auto &map = *map_ptr_;
                grid_map::GridMapCvConverter::toImage<unsigned char, 1>(map, layer_name, CV_8UC1, img);
                cv::applyColorMap(img, color_img, cv::COLORMAP_TWILIGHT);

                auto msg = cv_bridge::CvImage(std_msgs::msg::Header(), "bgr8", color_img).toImageMsg();
                msg->header.stamp = stamp;
                pub.publish(msg);
            } else { // special treatment for the terrain evaluation
                const auto &map = *map_ptr_;
                img = cv::Mat(map.getSize()(0), map.getSize()(1), CV_32FC3, cv::Scalar(0, 0, 0));
                normalized_img = cv::Mat(map.getSize()(0), map.getSize()(1), CV_32FC3, cv::Scalar(0, 0, 0));
                const grid_map::Matrix &data = map["ground"];
                const grid_map::Matrix &visited_layer = map["pointsRaw"];
                const grid_map::Matrix &gp_layer = map["groundpatch"];
                const float &car_height = data(181, 181);
                const float &ground_min = map["ground"].minCoeff() - car_height;
                const float &ground_max = map["ground"].maxCoeff() - car_height;
                if (ground_max == ground_min)
                    return;

                for (grid_map::GridMapIterator iterator(map); !iterator.isPastEnd(); ++iterator) {
                    const grid_map::Index index(*iterator);
                    const float &value = data(index(0), index(1)); // - car_height;
                    const float &gp = gp_layer(index(0), index(1));
                    const grid_map::Index imageIndex(iterator.getUnwrappedIndex());
                    const float &pointssum = visited_layer.block<3, 3>(index(0) - 1, index(1) - 1).sum();
                    const float &pointcount = visited_layer(index(0), index(1));

                    img.at<cv::Point3f>(imageIndex(0), imageIndex(1)) =
                    cv::Point3f(value, pointssum >= 27 ? 1.0f : 0.0f, pointcount);
                }

                geometry_msgs::msg::TransformStamped baseToUtmTransform;

                try {
                    baseToUtmTransform = mTfBuffer_.lookupTransform("utm", "base_link", tf2::TimePointZero);
                } catch (const tf2::TransformException &ex) {
                    RCLCPP_WARN(rclcpp::get_logger("rclcpp"), "%s", ex.what());
                    return;
                }

                geometry_msgs::msg::PointStamped ps;
                ps.header.frame_id = "base_link";
                ps.header.stamp = stamp;
                tf2::doTransform(ps, ps, baseToUtmTransform);

                auto msg = cv_bridge::CvImage(std_msgs::msg::Header(), "32FC3", img).toImageMsg();
                msg->header.frame_id = std::to_string(ps.point.x) + "_" + std::to_string(ps.point.y);
                pub.publish(msg);
            }
        }
    }

private:
    /// subscriber
    rclcpp::Subscription<sensor_msgs::msg::PointCloud2>::SharedPtr points_sub_;
    rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr pos_sub_;

    /// publisher
    image_transport::Publisher grid_map_cv_img_pub_;
    image_transport::Publisher terrain_im_pub_;
    rclcpp::Publisher<grid_map_msgs::msg::GridMap>::SharedPtr grid_map_pub_;
    rclcpp::Publisher<sensor_msgs::msg::PointCloud2>::SharedPtr filtered_cloud_pub_;
    std::unordered_map<std::string, image_transport::Publisher> layer_pubs_;

    /// pointer to the functionality class
    std::shared_ptr<GroundGrid> groundgrid_;

    /// grid map
    std::shared_ptr<grid_map::GridMap> map_ptr_;

    /// Filter class for grid map
    GroundSegmentation ground_segmentation_;

    /// tf stuff
    tf2_ros::Buffer mTfBuffer_;
    tf2_ros::TransformListener mTfListener_;

    geometry_msgs::msg::TransformStamped mapToBaseTransform_, cloudOriginTransform_;
};
}

#include <rclcpp_components/register_node_macro.hpp>
RCLCPP_COMPONENTS_REGISTER_NODE(groundgrid::GroundGridNode)
