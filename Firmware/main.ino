#include "app.h"
#include "config.h"
#include <esp_sleep.h>

// Hardware objects
SPIClass hspi(HSPI);

DisplayT display(GxEPD2_DRIVER_CLASS(EPD_CS_PIN, EPD_DC_PIN, EPD_RES_PIN, EPD_BUSY_PIN));

U8G2_FOR_ADAFRUIT_GFX u8g2;

// Power management 
const unsigned long ACTIVE_DURATION_MS = 5 * 60 * 1000;  
const unsigned long REFRESH_INTERVAL_MS = 20 * 1000;     
unsigned long g_wakeTimeMs = 0;
unsigned long g_lastFetchMs = 0;

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
  while (WiFi.status() != WL_CONNECTED) delay(100);

  // Serial1.println(WiFi.localIP());

  // Configure timezone and sync time with NTP
  configTime(0, 0, NTP_SERVER);
  setenv("TZ", TIMEZONE, 1);
  tzset();
}

static void goToDeepSleep() {
  // Serial1.println("going to sleep");
  // Serial1.flush();
  
  esp_sleep_enable_ext0_wakeup((gpio_num_t)BUTTON_REFRESH, LOW);
  display.hibernate();
  esp_deep_sleep_start();
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
  
  // Configure ADC for battery reading
  analogReadResolution(12);  // 12-bit ADC (0-4095)
  
  connectWifi();

  // Serial1.println("syncing time");
  delay(2000);  // Wait for NTP

  // Serial1.println("fetching data");
  if (fetchDepartures()) {
    renderDepartureScreen();
  }
  
  // Record wake time and first fetch time
  g_wakeTimeMs = millis();
  g_lastFetchMs = millis();
  
  // Serial1.println("ready");
}

void loop() {
  // Check if 5 minutes have elapsed since wake
  if (millis() - g_wakeTimeMs >= ACTIVE_DURATION_MS) {
    goToDeepSleep();
    return;
  }
  
  // Auto-fetch every 20 seconds and render only if data changed
  if (millis() - g_lastFetchMs >= REFRESH_INTERVAL_MS) {
    // Serial1.println("checking updates");
    if (fetchDepartures()) {
      renderDepartureScreen();
    }
    g_lastFetchMs = millis();
  }
  
  // Manual button refresh (always renders)
  if (digitalRead(BUTTON_REFRESH) == LOW) {
    delay(200);  // Debounce
    if (digitalRead(BUTTON_REFRESH) == LOW) {
      // Serial1.println("button press");
      fetchDepartures();  // Ignore return value for manual refresh
      renderDepartureScreen();
      g_lastFetchMs = millis();  // Reset fetch timer
      
      while (digitalRead(BUTTON_REFRESH) == LOW) delay(50);
    }
  }
  
  delay(1000); 
}