#pragma once

#include <WiFi.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <time.h>

#include <GxEPD2_BW.h>
#include <GxEPD2_7C.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeMono9pt7b.h>
#include <ArduinoJson.h>

// UTF-8 font rendering via U8g2 adapter for Adafruit GFX
#include <U8g2_for_Adafruit_GFX.h>
extern U8G2_FOR_ADAFRUIT_GFX u8g2;

// Pin definitions 
#define EPD_SCK_PIN   7
#define EPD_MOSI_PIN  9
#define EPD_CS_PIN    10
#define EPD_DC_PIN    11
#define EPD_RES_PIN   12
#define EPD_BUSY_PIN  13

#define SERIAL_RX     44
#define SERIAL_TX     43

const int BUTTON_REFRESH = 3;       // Right green button refreshes the current screen
const int BUTTON_DEPARTURES = 4;    // Middle button opens departures
const int BUTTON_MAP = 5;           // Left button opens the map
const int BATTERY_ADC_PIN = 1;      // Battery voltage ADC on reTerminal E1001
const int BATTERY_ENABLE_PIN = 21;  // Enables the E1001 battery voltage divider

enum class ScreenMode {
  Departures,
  Map
};

// E-paper display driver 
#define EPD_SELECT 0

#if (EPD_SELECT == 0)
  #define GxEPD2_DISPLAY_CLASS GxEPD2_BW
  #define GxEPD2_DRIVER_CLASS  GxEPD2_750_GDEY075T7
#elif (EPD_SELECT == 1)
  #define GxEPD2_DISPLAY_CLASS GxEPD2_7C
  #define GxEPD2_DRIVER_CLASS  GxEPD2_730c_GDEP073E01
#endif

// Limit the page buffer to 16 KB to fit in ESP32 SRAM
#define MAX_DISPLAY_BUFFER_SIZE 16000
#define MAX_HEIGHT(EPD) \
  (EPD::HEIGHT <= MAX_DISPLAY_BUFFER_SIZE / (EPD::WIDTH / 8) ? EPD::HEIGHT : MAX_DISPLAY_BUFFER_SIZE / (EPD::WIDTH / 8))

using DisplayT = GxEPD2_DISPLAY_CLASS<GxEPD2_DRIVER_CLASS, MAX_HEIGHT(GxEPD2_DRIVER_CLASS)>;

// Shared hardware objects 
extern DisplayT display;
extern SPIClass hspi;

// Departure data 
extern String g_stopName;
extern String g_updatedAt;
extern DynamicJsonDocument g_departures;

bool fetchDepartures();
void renderDepartureScreen();
