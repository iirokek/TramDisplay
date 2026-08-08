#pragma once

#include <stdint.h>

// Size of the exported QGIS map
const uint16_t MAP_WIDTH = 800;
const uint16_t MAP_HEIGHT = 480;

// Exact EPSG:3857 extent used when the map image was exported
const double MAP_X_MIN = 2629714.934;
const double MAP_Y_MIN = 8724789.108;
const double MAP_X_MAX = 2672903.748;
const double MAP_Y_MAX = 8750702.396;

// Web Mercator constants
const double WEB_MERCATOR_RADIUS = 6378137.0;
const double WEB_MERCATOR_MAX_LATITUDE = 85.05112878;

bool latLonToMapPixel(double latitude, double longitude,
                      int16_t &pixelX, int16_t &pixelY);
