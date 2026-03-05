#include "app.h"
#include "config.h"

// Global state populated by fetchDepartures() and read by renderDepartureScreen()
String g_stopName;
String g_updatedAt;
DynamicJsonDocument g_departures(1024);

// Read battery voltage and convert to percentage
static int getBatteryPercentage() {
  int adcValue = analogRead(BATTERY_PIN);
  
  float voltage = (adcValue / 4095.0) * 3.3 * 2.0;
  float percentage = ((voltage - 3.3) / (4.2 - 3.3)) * 100.0;

  if (percentage > 100) percentage = 100;
  if (percentage < 0) percentage = 0;
  
  // Serial1.printf("bat: %d%% (%.2fV)\n", (int)percentage, voltage);
  
  return (int)percentage;
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

// Draw the full departure board onto the e-paper frame buffer
static void drawDisplay(const String &stop, JsonArray departures, const String &updated) {

  const int W = display.width();
  const int H = display.height();

  display.fillScreen(GxEPD_WHITE);

  u8g2.setFont(u8g2_font_helvB18_tf);

  // HEADER
  u8g2.setCursor(10, 30);
  u8g2.print(stop);
  
  // Battery percentage
  int batteryPct = getBatteryPercentage();
  String batteryStr = String(batteryPct) + "%";
  
  u8g2.setFont(u8g2_font_helvB12_tf);
  int16_t batteryWidth = u8g2.getUTF8Width(batteryStr.c_str());
  u8g2.setCursor(W - 10 - batteryWidth, 30);
  u8g2.print(batteryStr);
  
  u8g2.setFont(u8g2_font_helvB18_tf);  

  int headerLineY = 42;
  display.drawLine(10, headerLineY, W - 10, headerLineY, GxEPD_BLACK);

  // FOOTER 
  const int footerHeight = 50;
  const int footerTop = H - footerHeight;

  display.drawLine(10, footerTop, W - 10, footerTop, GxEPD_BLACK);

  String foot = String("Päivitetty ") + updated;

  int16_t fw = u8g2.getUTF8Width(foot.c_str());
  int footerX = (W - fw) / 2;
  if (footerX < 10) footerX = 10;

  int footerBaseline = H - 15;
  u8g2.setCursor(footerX, footerBaseline);
  u8g2.print(foot);

  display.drawLine(10, H - 2, W - 10, H - 2, GxEPD_BLACK);

  // DEPARTURE LIST 
  const int listTop = headerLineY + 30;
  const int listBottom = footerTop - 10;
  const int rowStep = 34;

  int maxRows = (listBottom - listTop) / rowStep;
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
    left += "  ";
    left += dep[1].as<String>();

    // Right side: scheduled time + ETA
    String right = dep[2].as<String>();
    right += "  ";
    right += formatEta(dep[3].as<int>());

    u8g2.setCursor(10, y);
    u8g2.print(left);

    // Right-align the time/ETA text
    int16_t rw = u8g2.getUTF8Width(right.c_str());
    int rightX = W - 10 - rw;
    if (rightX < 10) rightX = 10;

    u8g2.setCursor(rightX, y);
    u8g2.print(right);

    y += rowStep;
    row++;
  }
}

void renderDepartureScreen() {
  display.setFullWindow();
  display.firstPage();
  do {
    drawDisplay(g_stopName, g_departures.as<JsonArray>(), g_updatedAt);
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