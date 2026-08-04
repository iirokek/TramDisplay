#include "app.h"
#include "config.h"

// Global state populated by fetchDepartures() and read by renderDepartureScreen()
String g_stopName;
String g_updatedAt;
DynamicJsonDocument g_departures(1024);

struct BatteryCalibrationPoint {
  float voltage;
  int percentage;
};

// Convert the measured LiPo voltage using Seeed's E1001 calibration curve.
static int batteryVoltageToPercentage(float voltage) {
  static const BatteryCalibrationPoint curve[] = {
    {3.27f, 0}, {3.30f, 5}, {3.41f, 10}, {3.49f, 20},
    {3.58f, 30}, {3.68f, 40}, {3.75f, 50}, {3.80f, 60},
    {3.85f, 70}, {3.91f, 80}, {3.96f, 90}, {4.15f, 100}
  };
  const size_t pointCount = sizeof(curve) / sizeof(curve[0]);

  if (voltage <= curve[0].voltage) return curve[0].percentage;

  for (size_t i = 1; i < pointCount; i++) {
    if (voltage <= curve[i].voltage) {
      const BatteryCalibrationPoint &lower = curve[i - 1];
      const BatteryCalibrationPoint &upper = curve[i];
      float position = (voltage - lower.voltage) / (upper.voltage - lower.voltage);
      float percentage = lower.percentage
                       + position * (upper.percentage - lower.percentage);
      return static_cast<int>(percentage + 0.5f);
    }
  }

  return curve[pointCount - 1].percentage;
}

// Read the E1001 battery voltage through its switched 2:1 voltage divider.
static int getBatteryPercentage() {
  digitalWrite(BATTERY_ENABLE_PIN, HIGH);
  delay(10);
  uint32_t millivolts = analogReadMilliVolts(BATTERY_ADC_PIN);
  digitalWrite(BATTERY_ENABLE_PIN, LOW);

  float voltage = (millivolts / 1000.0f) * 2.0f;
  int percentage = batteryVoltageToPercentage(voltage);

  // Serial1.printf("bat: %d%% (%.2fV)\n", percentage, voltage);
  return percentage;
}

// Format minutes-until-departure as a human-readable ETA string
static String formatEta(int mins) {
  if (mins < 60) {
    return String(mins) + " min";
  }
  int h = mins / 60;
  int m = mins % 60;
  if (m == 0) return String(h) + " h";
  return String(h) + " h " + String(m) + " min";
}

// Shorten text without splitting a UTF-8 character.
static String fitTextToWidth(String text, int maxWidth) {
  if (maxWidth <= 0) return "";
  if (u8g2.getUTF8Width(text.c_str()) <= maxWidth) return text;

  const String ellipsis = "...";
  if (u8g2.getUTF8Width(ellipsis.c_str()) > maxWidth) return "";
  while (text.length() > 0
      && u8g2.getUTF8Width((text + ellipsis).c_str()) > maxWidth) {
    int lastCharacter = text.length() - 1;
    while (lastCharacter > 0
        && (static_cast<uint8_t>(text[lastCharacter]) & 0xC0) == 0x80) {
      lastCharacter--;
    }
    text.remove(lastCharacter);
  }

  return text + ellipsis;
}

// Draw a 20-minute approach indicator. The final five minutes are dotted.
static void drawArrivalProgress(int startX, int endX, int centerY, int minutes) {
  const int visibleMinutes = 20;
  const int dottedMinutes = 5;
  const int ballRadius = 8;
  const int lineHalfThickness = 2;
  const int dashLength = 9;
  const int dashGap = 7;

  int width = endX - startX;
  if (width < 40) return;

  int dottedStart = endX - (width * dottedMinutes / visibleMinutes);

  for (int offset = -lineHalfThickness; offset <= lineHalfThickness; offset++) {
    display.drawLine(startX, centerY + offset,
                     dottedStart, centerY + offset, GxEPD_BLACK);
  }

  for (int dashX = dottedStart; dashX <= endX; dashX += dashLength + dashGap) {
    int dashEnd = dashX + dashLength;
    if (dashEnd > endX) dashEnd = endX;
    for (int offset = -lineHalfThickness; offset <= lineHalfThickness; offset++) {
      display.drawLine(dashX, centerY + offset,
                       dashEnd, centerY + offset, GxEPD_BLACK);
    }
  }

  int clampedMinutes = minutes;
  if (clampedMinutes < 0) clampedMinutes = 0;
  if (clampedMinutes > visibleMinutes) clampedMinutes = visibleMinutes;

  int ballX = endX - (static_cast<long>(width) * clampedMinutes / visibleMinutes);
  display.fillCircle(ballX, centerY, ballRadius, GxEPD_BLACK);
}

// Draw the full departure board onto the e-paper frame buffer
static void drawDisplay(const String &stop, JsonArray departures, const String &updated,
                        int batteryPct) {

  const int W = display.width();
  const int H = display.height();

  display.fillScreen(GxEPD_WHITE);

  // HEADER
  String batteryStr = String(batteryPct) + "%";

  u8g2.setFont(u8g2_font_helvB14_tf);
  int16_t batteryWidth = u8g2.getUTF8Width(batteryStr.c_str());

  u8g2.setFont(u8g2_font_helvB24_tf);
  String fittedStop = fitTextToWidth(stop, W - batteryWidth - 45);
  u8g2.setCursor(10, 30);
  u8g2.print(fittedStop);

  // Battery percentage
  u8g2.setFont(u8g2_font_helvB14_tf);
  u8g2.setCursor(W - 10 - batteryWidth, 30);
  u8g2.print(batteryStr);

  int headerLineY = 48;
  display.drawLine(10, headerLineY, W - 10, headerLineY, GxEPD_BLACK);

  // FOOTER 
  const int footerHeight = 42;
  const int footerTop = H - footerHeight;

  String foot = String("Päivitetty ") + updated;

  u8g2.setFont(u8g2_font_helvB18_tf);
  int16_t fw = u8g2.getUTF8Width(foot.c_str());
  int footerX = (W - fw) / 2;
  if (footerX < 10) footerX = 10;

  int footerBaseline = H - 10;
  u8g2.setCursor(footerX, footerBaseline);
  u8g2.print(foot);

  // DEPARTURE LIST 
  u8g2.setFont(u8g2_font_helvB24_tf);

  const int listTop = headerLineY + 40;
  const int listBottom = footerTop - 12;
  const int rowStep = 60;

  int maxRows = 1 + (listBottom - listTop) / rowStep;
  if (maxRows < 1) maxRows = 1;

  int y = listTop;
  int row = 0;

  for (JsonVariant v : departures) {
    if (row >= maxRows) break;

    // Each departure is a JSON array: [line, destination, time, minutes]
    JsonArray dep = v.as<JsonArray>();
    if (dep.isNull() || dep.size() < 4) continue;

    // Left side: line number + destination
    String left = dep[0].as<String>();
    left += " ";
    left += dep[1].as<String>();

    // Right side: scheduled time + ETA
    String right = dep[2].as<String>();
    right += "  ";
    right += formatEta(dep[3].as<int>());

    // Right-align the time/ETA text
    int16_t rw = u8g2.getUTF8Width(right.c_str());
    int rightX = W - 10 - rw;
    if (rightX < 10) rightX = 10;

    const int textToLineGap = 14;
    const int minimumProgressWidth = 90;
    int maxLeftWidth = rightX - 10 - (2 * textToLineGap) - minimumProgressWidth;
    left = fitTextToWidth(left, maxLeftWidth);

    u8g2.setCursor(10, y);
    u8g2.print(left);

    u8g2.setCursor(rightX, y);
    u8g2.print(right);

    int16_t leftWidth = u8g2.getUTF8Width(left.c_str());
    int progressStart = 10 + leftWidth + textToLineGap;
    int progressEnd = rightX - textToLineGap;
    drawArrivalProgress(progressStart, progressEnd, y - 9, dep[3].as<int>());

    y += rowStep;
    row++;
  }
}

void renderDepartureScreen() {
  int batteryPct = getBatteryPercentage();
  display.setFullWindow();
  display.firstPage();
  do {
    drawDisplay(g_stopName, g_departures.as<JsonArray>(), g_updatedAt, batteryPct);
  } while (display.nextPage());
}

bool fetchDepartures() {

  if (WiFi.status() != WL_CONNECTED) {
    // Serial1.println("wifi down");
    return false;
  }

  // Construct the full API URL from config
  String apiUrl = String(API_HOST) + "/display/" + String(STOP_ID);

  HTTPClient http;
  http.begin(apiUrl);

  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {

    String payload = http.getString();

    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      // Serial1.printf("json error: %s\n", error.c_str());
      http.end();
      return false;
    }

    // Serialize departures to string for comparison
    String departuresStr;
    serializeJson(doc["departures"], departuresStr);
    
    // Check if departure data has actually changed
    static String lastDeparturesStr = "";
    bool dataChanged = (departuresStr != lastDeparturesStr);
    
    if (dataChanged) {
      // Serial1.println("data changed");
      
      // Map the backend JSON fields to global state
      g_stopName = doc["stop_name"].as<String>();

      // Convert the unix timestamp to a readable HH:MM string
      long updated_ts = doc["updated_at"].as<long>();
      time_t t = (time_t)updated_ts;
      struct tm *tm_info = localtime(&t);
      char buffer[6];
      strftime(buffer, sizeof(buffer), "%H:%M", tm_info);
      g_updatedAt = String(buffer);

      g_departures.clear();
      for (JsonVariant dep : doc["departures"].as<JsonArray>()) {
        g_departures.add(dep);
      }
      
      lastDeparturesStr = departuresStr;
    } else {
      // Serial1.println("no changes");
    }
    
    http.end();
    return dataChanged;

  } else {
    // Serial1.printf("http error %d\n", httpCode);
    http.end();
    return false;
  }
}