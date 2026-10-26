/**
 * @file point_in_polygon.c
 * @brief Ray-Casting Point-in-Polygon Implementation
 *
 * Uses the standard ray-casting algorithm: cast a horizontal ray
 * eastward from the test point and count polygon edge crossings.
 * Odd number of crossings = inside.
 */

#include "point_in_polygon.h"

bool point_in_polygon(float test_lat, float test_lon,
                      const float *poly_lats, const float *poly_lons,
                      int num_vertices)
{
    if (num_vertices < 3) {
        return false;
    }

    bool inside = false;
    int j = num_vertices - 1;

    for (int i = 0; i < num_vertices; i++) {
        float yi = poly_lats[i];
        float xi = poly_lons[i];
        float yj = poly_lats[j];
        float xj = poly_lons[j];

        if (((yi > test_lat) != (yj > test_lat)) &&
            (test_lon < (xj - xi) * (test_lat - yi) / (yj - yi) + xi))
        {
            inside = !inside;
        }

        j = i;
    }

    return inside;
}
