#include "tram_api.h"

#include <math.h>

#include "app.h"
#include "config.h"

const int HTTP_CONNECT_TIMEOUT_MS = 5000;
const int HTTP_RESPONSE_TIMEOUT_MS = 8000;
const int MAX_TRAM_RESPONSE_BYTES = 16 * 1024;

static bool isValidCoordinate(double latitude, double longitude) {
  return isfinite(latitude) && isfinite(longitude)
      && latitude >= -90.0 && latitude <= 90.0
      && longitude >= -180.0 && longitude <= 180.0;
}


bool fetchTramVehicles(TramVehicleList &vehicleList) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial1.println("map: WiFi unavailable");
    return false;
  }

  // Serial1.println("map: fetching vehicles");

  HTTPClient http;
  const String apiUrl = String(API_HOST) + "/trams";
  if (!http.begin(apiUrl)) {
    Serial1.println("map: HTTP setup failed");
    return false;
  }

  http.setConnectTimeout(HTTP_CONNECT_TIMEOUT_MS);
  http.setTimeout(HTTP_RESPONSE_TIMEOUT_MS);

  bool success = false;
  do {
    const int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
      Serial1.printf("map: HTTP %d\n", httpCode);
      break;
    }

    const int responseSize = http.getSize();
    if (responseSize > MAX_TRAM_RESPONSE_BYTES) {
      Serial1.printf("map: response too large (%d bytes)\n", responseSize);
      break;
    }

    JsonDocument filter;
    filter["vehicles"][0]["latitude"] = true;
    filter["vehicles"][0]["longitude"] = true;

    JsonDocument document;
    const DeserializationError error = deserializeJson(
        document,
        http.getStream(),
        DeserializationOption::Filter(filter),
        DeserializationOption::NestingLimit(4));
    if (error) {
      Serial1.println("map: JSON parse failed");
      break;
    }

    JsonArrayConst vehicles = document["vehicles"].as<JsonArrayConst>();
    if (vehicles.isNull()) {
      Serial1.println("map: invalid JSON structure");
      break;
    }
    if (vehicles.size() > MAX_TRAM_VEHICLES) {
      Serial1.printf("map: vehicle limit exceeded (%u)\n",
                     static_cast<unsigned>(vehicles.size()));
      break;
    }

    TramVehicleList received;
    for (JsonObjectConst vehicle : vehicles) {
      JsonVariantConst latitudeValue = vehicle["latitude"];
      JsonVariantConst longitudeValue = vehicle["longitude"];
      if (!latitudeValue.is<double>() || !longitudeValue.is<double>()) {
        continue;
      }

      const double latitude = latitudeValue.as<double>();
      const double longitude = longitudeValue.as<double>();
      if (!isValidCoordinate(latitude, longitude)) {
        continue;
      }

      received.vehicles[received.count++] = {
        static_cast<float>(latitude),
        static_cast<float>(longitude),
      };
    }

    vehicleList = received;
    // Serial1.printf("map: %u vehicles received\n",
    //                static_cast<unsigned>(vehicleList.count));
    success = true;
  } while (false);

  http.end();
  return success;
}
