#include "map_projection.h"

#include <math.h>

const double PI_VALUE = 3.14159265358979323846;

// Convert Web Mercator coordinates to a pixel on the 800x480 map.
static bool webMercatorToMapPixel(double mercatorX, double mercatorY,
                                  int16_t &pixelX, int16_t &pixelY) {
  // Do not draw vehicles outside the exported map area.
  if (!isfinite(mercatorX) || !isfinite(mercatorY)
      || mercatorX < MAP_X_MIN || mercatorX > MAP_X_MAX
      || mercatorY < MAP_Y_MIN || mercatorY > MAP_Y_MAX) {
    return false;
  }

  double x = (mercatorX - MAP_X_MIN)
           / (MAP_X_MAX - MAP_X_MIN)
           * (MAP_WIDTH - 1);

  // Map north is up, but display Y grows downwards.
  double y = (MAP_Y_MAX - mercatorY)
           / (MAP_Y_MAX - MAP_Y_MIN)
           * (MAP_HEIGHT - 1);

  pixelX = static_cast<int16_t>(lround(x));
  pixelY = static_cast<int16_t>(lround(y));
  return true;
}

// Convert a normal GPS latitude and longitude to a map pixel.
bool latLonToMapPixel(double latitude, double longitude,
                      int16_t &pixelX, int16_t &pixelY) {
  if (!isfinite(latitude) || !isfinite(longitude)
      || latitude < -90.0 || latitude > 90.0
      || longitude < -180.0 || longitude > 180.0) {
    return false;
  }

  if (latitude > WEB_MERCATOR_MAX_LATITUDE) {
    latitude = WEB_MERCATOR_MAX_LATITUDE;
  } else if (latitude < -WEB_MERCATOR_MAX_LATITUDE) {
    latitude = -WEB_MERCATOR_MAX_LATITUDE;
  }

  double latitudeRadians = latitude * PI_VALUE / 180.0;
  double longitudeRadians = longitude * PI_VALUE / 180.0;

  double mercatorX = WEB_MERCATOR_RADIUS * longitudeRadians;
  double mercatorY = WEB_MERCATOR_RADIUS
                   * log(tan(PI_VALUE / 4.0 + latitudeRadians / 2.0));

  return webMercatorToMapPixel(mercatorX, mercatorY, pixelX, pixelY);
}
