/**
 * @file point_in_polygon.h
 * @brief Ray-Casting Point-in-Polygon Algorithm
 */

#ifndef POINT_IN_POLYGON_H
#define POINT_IN_POLYGON_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Determine if a point is inside a polygon using ray-casting.
 * @param test_lat    Test point latitude (degrees).
 * @param test_lon    Test point longitude (degrees).
 * @param poly_lats   Array of polygon vertex latitudes.
 * @param poly_lons   Array of polygon vertex longitudes.
 * @param num_vertices Number of polygon vertices.
 * @return true if point is inside the polygon.
 */
bool point_in_polygon(float test_lat, float test_lon,
                      const float *poly_lats, const float *poly_lons,
                      int num_vertices);

#ifdef __cplusplus
}
#endif

#endif /* POINT_IN_POLYGON_H */
