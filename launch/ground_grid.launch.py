import launch
import launch_ros.actions
from launch.substitutions import LaunchConfiguration
from launch.actions import DeclareLaunchArgument

def generate_launch_description():
    return launch.LaunchDescription([
        DeclareLaunchArgument(
            '/ouster/points',
            default_value='/sensors/velodyne_points',
            description='Point cloud topic to subscribe to'
        ),
        DeclareLaunchArgument(
            'use_nodelets',
            default_value='false',
            description='Use nodelets (not applicable in ROS 2)'
        ),

        # Component container
        launch_ros.actions.ComposableNodeContainer(
            name='groundgrid_container',
            package='rclcpp_components',
            executable='component_container',
            namespace='',
            output='screen',
            composable_node_descriptions=[
                launch_ros.descriptions.ComposableNode(
                    package='groundgrid',
                    plugin='groundgrid::GroundGridNode',
                    name='groundgrid_node',
                    remappings=[
                        ('/sensors/velodyne_points', LaunchConfiguration('point_cloud_topic'))
                    ]
                )
            ]
        ),
    ])
