#pragma once

#include <stddef.h>


constexpr size_t MAX_TRAM_VEHICLES = 64;

struct TramVehicle {
  float latitude;
  float longitude;
};

struct TramVehicleList {
  TramVehicle vehicles[MAX_TRAM_VEHICLES];
  size_t count = 0;
};

bool fetchTramVehicles(TramVehicleList &vehicleList);
