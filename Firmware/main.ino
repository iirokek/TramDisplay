#include "app.h"
#include "config.h"
#include "map.h"
#include "tram_api.h"
#include <esp_sleep.h>

// Hardware objects
SPIClass hspi(HSPI);

DisplayT display(GxEPD2_DRIVER_CLASS(EPD_CS_PIN, EPD_DC_PIN, EPD_RES_PIN, EPD_BUSY_PIN));

U8G2_FOR_ADAFRUIT_GFX u8g2;

// Power management 
const unsigned long ACTIVE_DURATION_MS = 5 * 60 * 1000;
const unsigned long DEPARTURE_REFRESH_INTERVAL_MS = 20 * 1000;
const unsigned long MAP_REFRESH_INTERVAL_MS = 60 * 1000;
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15 * 1000;
unsigned long g_wakeTimeMs = 0;
unsigned long g_lastDepartureFetchMs = 0;
unsigned long g_lastMapFetchMs = 0;

ScreenMode g_screenMode = ScreenMode::Departures;
TramVehicleList g_tramVehicles;

RTC_DATA_ATTR int bootCount = 0;

// Display init
static void initDisplay() {
  pinMode(EPD_RES_PIN, OUTPUT);
  pinMode(EPD_DC_PIN, OUTPUT);
  pinMode(EPD_CS_PIN, OUTPUT);

  hspi.begin(EPD_SCK_PIN, -1, EPD_MOSI_PIN, -1);
  display.epd2.selectSPI(hspi, SPISettings(2000000, MSBFIRST, SPI_MODE0));
  display.init(0);

  u8g2.begin(display);
  u8g2.setFontMode(1);       
  u8g2.setForegroundColor(GxEPD_BLACK);
  u8g2.setBackgroundColor(GxEPD_WHITE);
}

static void connectWifi() {
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  const unsigned long startedAtMs = millis();
  while (WiFi.status() != WL_CONNECTED
      && millis() - startedAtMs < WIFI_CONNECT_TIMEOUT_MS) {
    delay(100);
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial1.println("wifi: connection timed out");
    return;
  }

  // Serial1.println(WiFi.localIP());

  // Configure timezone and sync time with NTP
  configTime(0, 0, NTP_SERVER);
  setenv("TZ", TIMEZONE, 1);
  tzset();
}

static void goToDeepSleep() {
  // Serial1.println("going to sleep");
  // Serial1.flush();
  
  const uint64_t wakeupPins = (1ULL << BUTTON_REFRESH)
                            | (1ULL << BUTTON_DEPARTURES)
                            | (1ULL << BUTTON_MAP);
  esp_sleep_enable_ext1_wakeup_io(wakeupPins, ESP_EXT1_WAKEUP_ANY_LOW);
  display.hibernate();
  esp_deep_sleep_start();
}

// Simple button debounce. Returns once for each press.
static bool buttonPressed(int pin) {
  if (digitalRead(pin) != LOW) return false;

  delay(50);
  if (digitalRead(pin) != LOW) return false;

  while (digitalRead(pin) == LOW) delay(10);
  return true;
}

static ScreenMode initialScreenMode(esp_sleep_wakeup_cause_t wakeupReason) {
  if (wakeupReason != ESP_SLEEP_WAKEUP_EXT1) {
    return ScreenMode::Departures;
  }

  const uint64_t wakeupPins = esp_sleep_get_ext1_wakeup_status();
  if (wakeupPins & (1ULL << BUTTON_MAP)) {
    return ScreenMode::Map;
  }
  return ScreenMode::Departures;
}

// Change tabs without making a new API request.
static void showDepartureScreen() {
  g_screenMode = ScreenMode::Departures;
  renderDepartureScreen();
  g_lastDepartureFetchMs = millis();
}

static void showMapScreen() {
  g_screenMode = ScreenMode::Map;
  renderTramMap(g_tramVehicles);
  g_lastMapFetchMs = millis();
}

// The green button refreshes whichever tab is currently open.
static void refreshCurrentScreen() {
  if (g_screenMode == ScreenMode::Map) {
    if (fetchTramVehicles(g_tramVehicles)) {
      renderTramMap(g_tramVehicles);
    }
    g_lastMapFetchMs = millis();
  } else {
    fetchDepartures();
    renderDepartureScreen();
    g_lastDepartureFetchMs = millis();
  }
}

void setup() {
  Serial1.begin(115200, SERIAL_8N1, SERIAL_RX, SERIAL_TX);
  delay(100);
  
  ++bootCount;
  // Serial1.printf("boot #%d\n", bootCount);

  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  // if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
  //   Serial1.println("button wake");
  // } else {
  //   Serial1.println("power on");
  // }

  initDisplay();
  pinMode(BUTTON_REFRESH, INPUT_PULLUP);
  pinMode(BUTTON_DEPARTURES, INPUT_PULLUP);
  pinMode(BUTTON_MAP, INPUT_PULLUP);
  
  // Configure ADC for battery reading
  pinMode(BATTERY_ENABLE_PIN, OUTPUT);
  digitalWrite(BATTERY_ENABLE_PIN, LOW);
  analogReadResolution(12);
  analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db);
  
  connectWifi();

  // Serial1.println("syncing time");
  delay(2000);  // Wait for NTP

  g_screenMode = initialScreenMode(wakeup_reason);
  if (g_screenMode == ScreenMode::Map) {
    fetchTramVehicles(g_tramVehicles);
    renderTramMap(g_tramVehicles);
  } else if (fetchDepartures()) {
    renderDepartureScreen();
  }
  
  // Record wake time and first fetch time
  g_wakeTimeMs = millis();
  g_lastDepartureFetchMs = millis();
  g_lastMapFetchMs = millis();
  
  // Serial1.println("ready");
}

void loop() {
  const unsigned long nowMs = millis();

  // Check if 5 minutes have elapsed since wake
  if (nowMs - g_wakeTimeMs >= ACTIVE_DURATION_MS) {
    goToDeepSleep();
    return;
  }

  // Left and middle buttons only change tabs.
  if (buttonPressed(BUTTON_MAP)) {
    showMapScreen();
    return;
  }
  if (buttonPressed(BUTTON_DEPARTURES)) {
    showDepartureScreen();
    return;
  }

  // Right green button refreshes the active tab.
  if (buttonPressed(BUTTON_REFRESH)) {
    refreshCurrentScreen();
    return;
  }

  if (g_screenMode == ScreenMode::Departures
      && nowMs - g_lastDepartureFetchMs >= DEPARTURE_REFRESH_INTERVAL_MS) {
    if (fetchDepartures()) {
      renderDepartureScreen();
    }
    g_lastDepartureFetchMs = millis();
  } else if (g_screenMode == ScreenMode::Map
      && nowMs - g_lastMapFetchMs >= MAP_REFRESH_INTERVAL_MS) {
    if (fetchTramVehicles(g_tramVehicles)) {
      renderTramMap(g_tramVehicles);
    }
    g_lastMapFetchMs = millis();
  }

  delay(10);
}
