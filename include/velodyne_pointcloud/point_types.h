#ifndef VELODYNE_POINTCLOUD_POINT_TYPES_H
#define VELODYNE_POINTCLOUD_POINT_TYPES_H

#include <pcl/point_types.h>
#include <cstdint>

namespace velodyne_pointcloud
{
  /** Euclidean Velodyne coordinate, including intensity and ring number. */
  struct PointXYZIR
  {
    PCL_ADD_POINT4D;                    // quad-word XYZ
    float intensity;                     ///< laser intensity reading
    std::uint16_t ring;                  ///< laser ring number
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW      // ensure proper alignment
  } EIGEN_ALIGN16;

  /** Euclidean Velodyne coordinate, including intensity, ring number, distance, and rotation. */
  struct PointXYZIDRA
  {
    PCL_ADD_POINT4D;                     // quad-word XYZ
    float intensity;                      ///< laser intensity reading
    float distance;
    std::uint16_t ring;                   ///< laser ring number
    std::uint16_t azimuth;                ///< laser rotation
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW       // ensure proper alignment
  } EIGEN_ALIGN16;

} // namespace velodyne_pointcloud

POINT_CLOUD_REGISTER_POINT_STRUCT(velodyne_pointcloud::PointXYZIR,
                                  (float, x, x)
                                  (float, y, y)
                                  (float, z, z)
                                  (float, intensity, intensity)
                                  (std::uint16_t, ring, ring))

POINT_CLOUD_REGISTER_POINT_STRUCT(velodyne_pointcloud::PointXYZIDRA,
                                  (float, x, x)
                                  (float, y, y)
                                  (float, z, z)
                                  (float, intensity, intensity)
                                  (float, distance, distance)
                                  (std::uint16_t, ring, ring)
                                  (std::uint16_t, azimuth, azimuth))

#endif // VELODYNE_POINTCLOUD_POINT_TYPES_H
