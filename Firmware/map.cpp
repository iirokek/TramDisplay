#include "map.h"

#include <stdint.h>

#include "app.h"
#include "map_projection.h"
#include "tampere_map.h"

const int MARKER_OUTER_RADIUS = 7;
const int MARKER_INNER_RADIUS = 5;

struct MapPixel {
  int16_t x;
  int16_t y;
};

static_assert(TAMPERE_MAP_WIDTH == MAP_WIDTH,
              "Map bitmap width does not match map calibration");
static_assert(TAMPERE_MAP_HEIGHT == MAP_HEIGHT,
              "Map bitmap height does not match map calibration");

void renderTramMap(const TramVehicleList &vehicleList) {
  MapPixel visibleVehicles[MAX_TRAM_VEHICLES];
  size_t visibleCount = 0;

  for (size_t index = 0; index < vehicleList.count; ++index) {
    int16_t pixelX;
    int16_t pixelY;
    const TramVehicle &vehicle = vehicleList.vehicles[index];
    if (latLonToMapPixel(vehicle.latitude, vehicle.longitude, pixelX, pixelY)) {
      visibleVehicles[visibleCount++] = {pixelX, pixelY};
    }
  }

  display.setFullWindow();
  display.firstPage();
  do {
    // Start from the clean map so old tram dots disappear.
    display.fillScreen(GxEPD_WHITE);
    display.drawBitmap(
        0,
        0,
        tampere_map,
        MAP_WIDTH,
        MAP_HEIGHT,
        GxEPD_BLACK);

    // White outer dot keeps the black center visible over tram tracks.
    for (size_t index = 0; index < visibleCount; ++index) {
      const MapPixel &vehicle = visibleVehicles[index];
      display.fillCircle(
          vehicle.x, vehicle.y, MARKER_OUTER_RADIUS, GxEPD_WHITE);
      display.fillCircle(
          vehicle.x, vehicle.y, MARKER_INNER_RADIUS, GxEPD_BLACK);
    }
  } while (display.nextPage());

  // Serial1.printf("map: %u vehicles inside map\n",
  //                static_cast<unsigned>(visibleCount));
}
