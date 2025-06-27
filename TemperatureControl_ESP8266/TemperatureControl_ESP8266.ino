// =====================================================================
// Temperature Control System for ESP8266 with MQTT and Web Interface
//
// This code implements a comprehensive temperature control system using an ESP8266.
// It features:
// - DS18B20 temperature sensor for temperature readings
// - 433 MHz RF control for relay switching (heating/cooling)
// - Web interface for configuration and monitoring
// - MQTT integration for smart home automation (Home Assistant compatible)
// - Frost protection with configurable temperature threshold
// - Time-based operation with configurable schedules
// - EEPROM storage for persistent settings
// - NTP time synchronization
// - WiFiManager for easy WiFi credential management
// - Real-time status monitoring and control
// =====================================================================

//===================== Libraries ===============================
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <RCSwitch.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <time.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// Include MQTT settings (specific for this device)
#include "MQTT_settings_private.h"

//===================== Settings ==============================
// MQTT settings if not using MQTT_settings_private.h
// const char* mqtt_server = "homeassistant.local";  // Change to your MQTT broker address
// const int mqtt_port = 1883;
// const char* mqtt_user = "";     // MQTT username (if required)
// const char* mqtt_password = ""; // MQTT password (if required)
// MQTT client ID will be generated at runtime using chip ID
char mqtt_client_id[30];
const char* mqtt_topic = "home/temperature_control";
const char* mqtt_availability_topic = "home/temperature_control/status";
const char* mqtt_command_topic = "home/temperature_control/set";
const char* mqtt_state_topic = "home/temperature_control/state";
const char* mqtt_temperature_topic = "home/temperature_control/temperature";

WiFiClient espClient;
PubSubClient mqttClient(espClient);
unsigned long lastReconnectAttempt = 0;
const unsigned long RECONNECT_INTERVAL = 30000; // 30 seconds

constexpr int DS18B20_DATA_PIN = D1; // Data pin for temperature sensor DS18B20
constexpr int SENDER_PIN = D5; // Pin for the 433 MHz transmitter
constexpr int RELAY_PROTOCOL = 1; // Protocol for 433Mhz transmitter  
constexpr int RELAY_PULSE_LENGTH = 317; // Pulse length for 433Mhz transmitter
constexpr int RELAY_WAIT_MS = 500; // Wait time for 433Mhz transmitter
constexpr int EEPROM_SIZE = 32; // Size of EEPROM
constexpr unsigned long NTP_SYNC_INTERVAL_MS = 21600000UL; // 6 hours

//===================== Enums and Structs =====================
enum ControlMode { AUTO, MANUAL };

//===================== Variables ===============================
RCSwitch rcSwitch = RCSwitch(); // RCSwitch object for 433 MHz communication

// MQTT state tracking
bool mqtt_connected = false;
unsigned long lastMqttPublish = 0;
const unsigned long MQTT_PUBLISH_INTERVAL = 5000; // 5 seconds

// Temperature sensor
OneWire oneWire(DS18B20_DATA_PIN);  // OneWire object for temperature sensor
DallasTemperature sensors(&oneWire); // DallasTemperature object for temperature sensor

// State Variables
ControlMode controlMode = AUTO;
bool isRelayOn = false;

// Configuration Variables
float targetTemp1 = 20.0; // Target temperature for control
float frostProtectionTemp = 3.0; // Frost protection temperature
float currentTemp = 0.0; // Current temperature
float deltaTemp1 = 2.0; // Temperature delta for control
int targetMin1 = 0, targetHour1 = 8; // First target time for control
int targetMin2 = 0, targetHour2 = 17; // Second target time for control

char Relay_ON[] = "0FFFFF0FFFF1"; // ON signal for relay
char Relay_OFF[] = "0FFFFF0FFFF0"; // OFF signal for relay

// Create web server on port 80
ESP8266WebServer server(80);

// Track relay switch times and ON period
time_t relayLastOnTime = 0;
time_t relayLastOffTime = 0;
time_t relayOnStart = 0;
unsigned long relayLastOnPeriod = 0; // seconds

// ===================== Helper Functions =====================
// Non-blocking delay using millis()
unsigned long lastTempRead = 0; // Last time temperature was read
const unsigned long tempReadInterval = 1000; // 1 second - Read interval for temperature sensor

unsigned long lastNtpSync = 0; // Last time NTP was synced

// MQTT callback function
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // ====================================================================
  // START: Diagnostic Logging
  // ====================================================================
  Serial.println("---- MQTT Message Received ----");
  Serial.print("Topic: ");
  Serial.println(topic);
  Serial.print("Payload: ");
  char msg[length + 1];
  strncpy(msg, (char*)payload, length);
  msg[length] = '\0';
  Serial.println(msg);
  // ====================================================================

  if (strcmp(topic, mqtt_command_topic) == 0) {
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, payload, length);

    if (error) {
      Serial.print("deserializeJson() failed: ");
      Serial.println(error.c_str());
      return;
    }

    bool settings_changed = false;

    // Handle mode and state updates
    if (doc.containsKey("mode")) {
      String newModeStr = doc["mode"];
      if (newModeStr.equalsIgnoreCase("MANUAL")) {
        controlMode = MANUAL;
      } else if (newModeStr.equalsIgnoreCase("AUTO")) {
        controlMode = AUTO;
      }
      Serial.print("Control mode set to: ");
      Serial.println(controlMode == MANUAL ? "MANUAL" : "AUTO");
    }

    // Handle state updates from MQTT
    if (doc.containsKey("state") && controlMode == MANUAL) {
      String newStateStr = doc["state"];
      if (newStateStr.equalsIgnoreCase("ON")) {
        setRelay(true);
        Serial.println("Relay turned ON manually via MQTT");
      } else if (newStateStr.equalsIgnoreCase("OFF")) {
        setRelay(false);
        Serial.println("Relay turned OFF manually via MQTT");
      }
    }

    // Handle target temperature updates from MQTT
    if (doc.containsKey("target_temp")) {
      float newTemp = doc["target_temp"];
      Serial.print("Received target_temp: "); Serial.println(newTemp);
      if (isValidTemperature(newTemp) && targetTemp1 != newTemp) {
        targetTemp1 = newTemp;
        Serial.print("Target temperature updated via MQTT to: ");
        Serial.println(targetTemp1);
        settings_changed = true;
      } else {
        Serial.print("Received invalid or unchanged target temperature via MQTT: ");
        Serial.println(newTemp);
      }
    }

    // Handle delta temperature updates from MQTT
    if (doc.containsKey("delta_temp")) {
      float newDeltaTemp = doc["delta_temp"];
      Serial.print("Received delta_temp: "); 
      Serial.println(newDeltaTemp);
      if (newDeltaTemp > 0 && newDeltaTemp < 10 && deltaTemp1 != newDeltaTemp) {
        deltaTemp1 = newDeltaTemp;
        Serial.print("Delta temperature updated via MQTT to: ");
        Serial.println(deltaTemp1);
        settings_changed = true;
      } else {
        Serial.print("Received invalid or unchanged delta temperature via MQTT: ");
        Serial.println(newDeltaTemp);
      }
    }

    // Handle frost protection temperature updates
    if (doc.containsKey("frost_protection_temp")) {
      float newFrostTemp = doc["frost_protection_temp"];
      Serial.print("Received frost_protection_temp: "); 
      Serial.println(newFrostTemp);
      if (isValidTemperature(newFrostTemp) && frostProtectionTemp != newFrostTemp) {
        frostProtectionTemp = newFrostTemp;
        Serial.print("Frost protection temperature updated via MQTT to: ");
        Serial.println(frostProtectionTemp);
        settings_changed = true;
      } else {
        Serial.print("Received invalid or unchanged frost protection temperature via MQTT: ");
        Serial.println(newFrostTemp);
      }
    }

    // Handle operating time updates
    if (doc.containsKey("start_hour")) {
      int val = doc["start_hour"];
      Serial.print("Received start_hour: "); Serial.println(val);
      if (val >= 0 && val < 24 && targetHour1 != val) { targetHour1 = val; settings_changed = true; Serial.println("  -> Updated targetHour1"); }
    }
    if (doc.containsKey("start_min")) {
      int val = doc["start_min"];
      Serial.print("Received start_min: "); Serial.println(val);
      if (val >= 0 && val < 60 && targetMin1 != val) { targetMin1 = val; settings_changed = true; Serial.println("  -> Updated targetMin1"); }
    }
    if (doc.containsKey("end_hour")) {
      int val = doc["end_hour"];
      Serial.print("Received end_hour: "); Serial.println(val);
      if (val >= 0 && val < 24 && targetHour2 != val) { targetHour2 = val; settings_changed = true; Serial.println("  -> Updated targetHour2"); }
    }
    if (doc.containsKey("end_min")) {
      int val = doc["end_min"];
      Serial.print("Received end_min: "); Serial.println(val);
      if (val >= 0 && val < 60 && targetMin2 != val) { targetMin2 = val; settings_changed = true; Serial.println("  -> Updated targetMin2"); }
    }

    // Handle settings updates
    if (settings_changed) {
      Serial.println("Settings updated via MQTT, saving to EEPROM.");
      eeprom_save(); // Save the new settings to EEPROM
    } else {
      Serial.println("No settings were changed by this MQTT message.");
    }
  }
}

// Connect to MQTT broker
bool mqttConnect() {
  Serial.println("Connecting to MQTT...");
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);
  
  // Generate unique client ID
  sprintf(mqtt_client_id, "temp_control_%08X", ESP.getChipId());
  
  // Attempt to connect with credentials
  if (mqttClient.connect(mqtt_client_id, mqtt_user, mqtt_password, 
                        mqtt_availability_topic, 1, true, "offline")) {
    Serial.println("MQTT connected");
    // Publish online status
    mqttClient.publish(mqtt_availability_topic, "online", true);

    // Subscribe to the command topic
    Serial.print("Subscribing to command topic: ");
    Serial.println(mqtt_command_topic);
    if (mqttClient.subscribe(mqtt_command_topic)) {
      Serial.println("Subscription successful");
    } else {
      Serial.println("ERROR: Subscription failed!");
    }

    // Publish initial state
    publishState();
    publishTemperature();
    mqtt_connected = true;
    return true;
  } else {
    Serial.print("MQTT connection failed, rc=");
    Serial.println(mqttClient.state());
    mqtt_connected = false;
    return false;
  }
}

// Publish device state to MQTT
void publishState() {
  if (!mqtt_connected) return;
  
  StaticJsonDocument<256> doc; // Increased buffer size
  doc["state"] = isRelayOn ? "ON" : "OFF";
  doc["mode"] = (controlMode == AUTO) ? "AUTO" : "MANUAL";  doc["target_temp"] = targetTemp1;
  doc["current_temp"] = currentTemp;
  doc["delta_temp"] = deltaTemp1;
  doc["start_hour"] = targetHour1;
  doc["start_min"] = targetMin1;
  doc["end_hour"] = targetHour2;
  doc["end_min"] = targetMin2;
  doc["frost_protection_temp"] = frostProtectionTemp;
  
  char buffer[256];
  serializeJson(doc, buffer);
  Serial.print("Publishing state to ");
  Serial.print(mqtt_state_topic);
  Serial.print(": ");
  Serial.println(buffer);
  
  if (mqttClient.publish(mqtt_state_topic, buffer, true)) {
    Serial.println("State published successfully");
  } else {
    Serial.println("Failed to publish state");
  }}

// Publish temperature to MQTT
void publishTemperature() {
  if (!mqtt_connected) return;
  
  char tempStr[10];
  dtostrf(currentTemp, 4, 2, tempStr);
  Serial.print("Publishing temperature: ");
  Serial.print(tempStr);
  Serial.print(" to ");
  Serial.println(mqtt_temperature_topic);
  
  if (mqttClient.publish(mqtt_temperature_topic, tempStr, true)) {
    Serial.println("Temperature published successfully");
  } else {
    Serial.println("Failed to publish temperature");
  }
}

// Control relay
void setRelay(bool turnOn) {
  if (isRelayOn == turnOn) return; // State is already correct, do nothing

  isRelayOn = turnOn;
  time_t now = time(nullptr);

  if (isRelayOn) {
    relayLastOnTime = now;
    relayOnStart = now;
  } else {
    relayLastOffTime = now;
    if (relayOnStart != 0) {
      relayLastOnPeriod = now - relayOnStart;
      relayOnStart = 0;
    }
  }

  Serial.print("Setting relay to: ");
  Serial.println(isRelayOn ? "ON" : "OFF");

  rcSwitch.enableTransmit(SENDER_PIN); // Re-enable transmitter before sending
  rcSwitch.sendTriState(isRelayOn ? Relay_ON : Relay_OFF);
  
  // Short delay to ensure signal is sent and to prevent rapid toggling
  delay(RELAY_WAIT_MS);
  printStatus();
  publishState(); // Publish state immediately on change
}

// Read temperature
float readTemperature() {
  sensors.requestTemperatures();
  return sensors.getTempCByIndex(0);
}

// Synchronize time with NTP server
void syncTimeWithNTP() {
  configTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.nist.gov");
  Serial.println("Synchronizing time with NTP server...");
  
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    Serial.println("Time synchronized successfully");
    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
    Serial.print("Current time: ");
    Serial.println(timeStr);
  } else {
    Serial.println("Failed to obtain time");
  }
}

// Format date and time
String formatDateTime(time_t t) {
  if (t == 0) return "N/A";
  struct tm *tm_info = localtime(&t);
  char buffer[30];
  strftime(buffer, 30, "%Y-%m-%d %H:%M:%S", tm_info);
  return String(buffer);
}

// Format a time period in seconds to a human-readable string
String formatPeriod(unsigned long period_s) {
  if (period_s == 0) return "0s";
  unsigned long days = period_s / 86400;
  period_s %= 86400;
  unsigned long hours = period_s / 3600;
  period_s %= 3600;
  unsigned long mins = period_s / 60;
  unsigned long secs = period_s % 60;
  String result = "";
  if (days > 0) result += String(days) + "d ";
  if (hours > 0) result += String(hours) + "h ";
  if (mins > 0) result += String(mins) + "m ";
  result += String(secs) + "s";
  return result;
}

// Create HTML page
String buildWebPage() {
  String disabled = (controlMode == MANUAL) ? "disabled" : "";
  String page = "<!DOCTYPE html><html><head><title>Temperature Control</title>";
  page += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  page += "<style>";
  page += "body{font-family:sans-serif;background-color:#f2f2f2;text-align:center;} h1{color:#333;} .container{max-width:600px;margin:auto;background-color:white;padding:20px;border-radius:10px;box-shadow:0 4px 8px rgba(0,0,0,0.1);} .card{padding:15px;margin-bottom:15px;border:1px solid #ddd;border-radius:5px;} .status-grid{display:grid;grid-template-columns:1fr 1fr;gap:5px 10px;text-align:left;} .status-grid span:nth-child(odd){font-weight:bold;} .status{font-size:1.2em;font-weight:bold;margin:10px 0;} .temp{font-size:2.5em;color:#007bff;} .label{font-weight:bold;color:#555;} form div{margin-bottom:10px;text-align:left;} input[type='number'],input[type='submit']{width:calc(100% - 22px);padding:10px;border-radius:5px;border:1px solid #ccc;} input[type='submit']{background-color:#007bff;color:white;font-size:1em;cursor:pointer;} input[type='submit']:hover{background-color:#0056b3;} .switch{position:relative;display:inline-block;width:60px;height:34px;} .switch input{opacity:0;width:0;height:0;} .slider{position:absolute;cursor:pointer;top:0;left:0;right:0;bottom:0;background-color:#ccc;transition:.4s;border-radius:34px;} .slider:before{position:absolute;content:'';height:26px;width:26px;left:4px;bottom:4px;background-color:white;transition:.4s;border-radius:50%;} input:checked+.slider{background-color:#2196F3;} input:checked+.slider:before{transform:translateX(26px);}";
  page += "</style></head><body>";
  page += "<div class='container'>";
  page += "<h1>ESP8266 Temperature Control</h1>";
  
  // Status Card
  page += "<div class='card'><h2>Status</h2>";
  page += "<div class='status-grid'>";
  page += "<span>Date:</span><span>" + getDate() + "</span>";
  page += "<span>Temperature:</span><span><span class='temp'>" + String(currentTemp, 1) + "&deg;C</span></span>";
  page += "<span>Last ON:</span><span>" + formatDateTime(relayLastOnTime) + "</span>";
  page += "<span>Last OFF:</span><span>" + formatDateTime(relayLastOffTime) + "</span>";
  page += "<span>Last ON Period:</span><span>";
  if (isRelayOn && relayOnStart != 0) {
    page += formatPeriod(time(nullptr) - relayOnStart);
  } else {
    page += formatPeriod(relayLastOnPeriod);
  }
  page += "</span></div></div>";

  // Control Mode Toggle
  page += "<div class='card'><h2>Control Mode</h2>";
  page += "<form action='/toggleMode' method='POST'>";
  page += "<label class='switch'><input type='checkbox' name='controlMode' onchange='this.form.submit()' " + String(controlMode == MANUAL ? "checked" : "") + ">";
  page += "<span class='slider'></span></label>";
  page += "<p class='status'>Mode: <strong>" + String(controlMode == MANUAL ? "MANUAL" : "AUTO") + "</strong></p></form></div>";

  // Relay Status Toggle
  page += "<div class='card'><h2>Relay Control</h2>";
  page += "<form action='/toggleStatus' method='POST'>";
  page += "<label class='switch'><input type='checkbox' name='status1' onchange='this.form.submit()' " + String(isRelayOn ? "checked" : "") + " " + String(controlMode == AUTO ? "disabled" : "") + ">";
  page += "<span class='slider'></span></label>";
  page += "<p class='status'>Relay is <strong>" + String(isRelayOn ? "ON" : "OFF") + "</strong></p></form></div>";

  // Settings Form
  page += "<div class='card'><h2>Settings (Auto Mode)</h2>";
  page += "<form action='/updateSettings' method='POST'>";
  page += "<div><label class='label' for='targetTemp1'>Target Temperature (&deg;C):</label><input type='number' step='0.1' id='targetTemp1' name='targetTemp1' value='" + String(targetTemp1) + "' " + disabled + "></div>";
  page += "<div><label class='label' for='deltaTemp1'>Delta Temperature (&deg;C):</label><input type='number' step='0.1' id='deltaTemp1' name='deltaTemp1' value='" + String(deltaTemp1) + "' " + disabled + "></div>";
  page += "<div><label class='label' for='frostProtectionTemp'>Frost Protection (&deg;C):</label><input type='number' step='0.1' id='frostProtectionTemp' name='frostProtectionTemp' value='" + String(frostProtectionTemp) + "' " + disabled + "></div>";
  page += "<div><label class='label'>Operating Time:</label>";
  page += "<input type='number' name='targetHour1' min='0' max='23' value='" + String(targetHour1) + "' " + disabled + "> : ";
  page += "<input type='number' name='targetMin1' min='0' max='59' value='" + String(targetMin1) + "' " + disabled + "> to ";
  page += "<input type='number' name='targetHour2' min='0' max='23' value='" + String(targetHour2) + "' " + disabled + "> : ";
  page += "<input type='number' name='targetMin2' min='0' max='59' value='" + String(targetMin2) + "' " + disabled + "></div>";
  page += "<input type='submit' value='Save Settings' " + disabled + ">";
  page += "</form></div>";
  
  page += "</div></body></html>";
  return page;
}

// Handler for relay toggle switch from web UI
void handleToggleStatus() {
  if (controlMode == MANUAL) {
    bool shouldBeOn = server.hasArg("status1");
    setRelay(shouldBeOn);
  }
  handleRoot(); // Refresh page to show the change
}

// Handler for Auto/Manual mode toggle from web UI
void handleToggleMode() {
  if (server.hasArg("controlMode")) {
    controlMode = MANUAL;
  } else {
    controlMode = AUTO;
  }
  publishState(); // Publish the new mode
  handleRoot(); // Refresh page
}

// Create HTML page
void handleRoot() {
  String page = buildWebPage();
  server.send(200, "text/html", page);
}

// Update settings from web interface
void updateSettings() {
  if (server.hasArg("targetTemp1")) {
    float val = server.arg("targetTemp1").toFloat();
    if (isValidTemperature(val)) targetTemp1 = val;
  }
  if (server.hasArg("deltaTemp1")) {
    float val = server.arg("deltaTemp1").toFloat();
    if (val > 0 && val < 20) deltaTemp1 = val;
  }
  if (server.hasArg("frostProtectionTemp")) {
    float val = server.arg("frostProtectionTemp").toFloat();
    if (isValidTemperature(val)) frostProtectionTemp = val;
  }
  if (server.hasArg("targetHour1")) {
    int val = server.arg("targetHour1").toInt();
    if (val >= 0 && val < 24) targetHour1 = val;
  }
  if (server.hasArg("targetMin1")) {
    int val = server.arg("targetMin1").toInt();
    if (val >= 0 && val < 60) targetMin1 = val;
  }
  if (server.hasArg("targetHour2")) {
    int val = server.arg("targetHour2").toInt();
    if (val >= 0 && val < 24) targetHour2 = val;
  }
  if (server.hasArg("targetMin2")) {
    int val = server.arg("targetMin2").toInt();
    if (val >= 0 && val < 60) targetMin2 = val;
  }
  handleRoot(); // Back to main page
  eeprom_save();  // Save variables to EEPROM
}

// Get current date
String getDate() {
  time_t now = time(nullptr);
  struct tm * timeinfo;
  timeinfo = localtime(&now);
  char buffer[20];
  strftime(buffer, sizeof(buffer), "%d %b %Y", timeinfo);
  return String(buffer);
}

// Main temperature control logic
void handleTemperatureControl() {
  if (controlMode != AUTO) {
    return; // Only run logic in AUTO mode
  }

  bool operating = isOperatingTime();
  bool shouldBeOn = isRelayOn; // Start with current state to avoid unnecessary toggling

  // Priority 1: Frost protection (always active in AUTO mode)
  if (currentTemp <= frostProtectionTemp) {
    shouldBeOn = true;
  } 
  // Priority 2: Standard temperature control within operating hours
  else if (operating) {
    if (currentTemp <= (targetTemp1 - deltaTemp1)) {
      shouldBeOn = true;
    } else if (currentTemp >= (targetTemp1 + deltaTemp1)) {
      shouldBeOn = false;
    }
  } 
  // Priority 3: Outside operating hours (and not in frost protection range)
  else {
    shouldBeOn = false;
  }

  setRelay(shouldBeOn); // Apply the final calculated state
}

// Check if current time is within operating hours
bool isOperatingTime() {
  time_t now = time(nullptr);
  struct tm * timeinfo;
  timeinfo = localtime(&now);
  int hour = timeinfo->tm_hour;
  int minute = timeinfo->tm_min;
  bool afterStart = (targetHour1 < hour) || (targetHour1 == hour && targetMin1 <= minute);
  bool beforeEnd = (targetHour2 > hour) || (targetHour2 == hour && targetMin2 >= minute);
  return afterStart && beforeEnd;
}

// Validate DS18B20 temperature reading
bool isValidTemperature(float temp) {
  return temp > -55.0 && temp < 125.0;
}

// Function to save the current variables to EEPROM
void eeprom_save() {
  Serial.println("Saving settings to EEPROM...");
  EEPROM.begin(EEPROM_SIZE);
  
  int address = 0;
  EEPROM.put(address, targetTemp1); address += sizeof(targetTemp1);
  EEPROM.put(address, deltaTemp1); address += sizeof(deltaTemp1);
  EEPROM.put(address, frostProtectionTemp); address += sizeof(frostProtectionTemp);
  EEPROM.put(address, targetHour1); address += sizeof(targetHour1);
  EEPROM.put(address, targetMin1); address += sizeof(targetMin1);
  EEPROM.put(address, targetHour2); address += sizeof(targetHour2);
  EEPROM.put(address, targetMin2); address += sizeof(targetMin2);
  
  if (EEPROM.commit()) {
    Serial.println("EEPROM successfully saved");
  } else {
    Serial.println("ERROR: EEPROM commit failed");
  }
  EEPROM.end();
}

// Function to load the variables from EEPROM during setup
void eeprom_load() {
  EEPROM.begin(EEPROM_SIZE);
  
  int address = 0;
  EEPROM.get(address, targetTemp1); address += sizeof(targetTemp1);
  EEPROM.get(address, deltaTemp1); address += sizeof(deltaTemp1);
  EEPROM.get(address, frostProtectionTemp); address += sizeof(frostProtectionTemp);
  EEPROM.get(address, targetHour1); address += sizeof(targetHour1);
  EEPROM.get(address, targetMin1); address += sizeof(targetMin1);
  EEPROM.get(address, targetHour2); address += sizeof(targetHour2);
  EEPROM.get(address, targetMin2); address += sizeof(targetMin2);

  // Validate loaded values to ensure they are reasonable
  if (!isValidTemperature(targetTemp1)) targetTemp1 = 20.0;
  if (deltaTemp1 <= 0 || deltaTemp1 >= 20) deltaTemp1 = 2.0;
  if (!isValidTemperature(frostProtectionTemp)) frostProtectionTemp = 3.0;
  if (targetHour1 < 0 || targetHour1 > 23) targetHour1 = 8;
  if (targetMin1 < 0 || targetMin1 > 59) targetMin1 = 0;
  if (targetHour2 < 0 || targetHour2 > 23) targetHour2 = 17;
  if (targetMin2 < 0 || targetMin2 > 59) targetMin2 = 0;

  Serial.println("Loaded settings from EEPROM:");
  Serial.println("Target Temp1: " + String(targetTemp1));
  Serial.println("Delta Temp1: " + String(deltaTemp1));
  Serial.println("Frost Protection Temp: " + String(frostProtectionTemp));
  Serial.println("Target Hour1: " + String(targetHour1));
  Serial.println("Target Min1: " + String(targetMin1));
  Serial.println("Target Hour2: " + String(targetHour2));
  Serial.println("Target Min2: " + String(targetMin2));

  EEPROM.end();
}

// Print system status to serial
void printStatus() {
  Serial.print("TargetTemp1: "); Serial.println(targetTemp1);
  Serial.print("DeltaTemp1: "); Serial.println(deltaTemp1);
  Serial.print("FrostProtectionTemp: "); Serial.println(frostProtectionTemp);
  Serial.print("CurrentTemp: "); Serial.println(currentTemp);
  Serial.print("OperatingTime: "); Serial.println(isOperatingTime());
  Serial.print("isRelayOn: "); Serial.println(isRelayOn);
}

void setup(){
  // Initialize serial communication
  Serial.begin(115200);
  Serial.println("\nStarting Temperature Control System...");

  // Initialize EEPROM with the defined size
  EEPROM.begin(EEPROM_SIZE);
  
  // Load settings from EEPROM
  eeprom_load();
  
  // Initialize RCSwitch
  rcSwitch.enableTransmit(SENDER_PIN);
  rcSwitch.setProtocol(RELAY_PROTOCOL);
  rcSwitch.setPulseLength(RELAY_PULSE_LENGTH);
  
  // Initialize temperature sensor
  sensors.begin();
  
  // Initialize WiFiManager
  WiFiManager wifiManager;
  
  // Uncomment the following line to reset WiFi settings
  // wifiManager.resetSettings();
  
  // Set minimum quality of signal so it ignores AP's under that quality
  // defaults to 8% if not specified
  // wifiManager.setMinimumSignalQuality(15);
  
  // Set config portal timeout (seconds)
  wifiManager.setConfigPortalTimeout(180);
  
  // Connect to WiFi or start config portal
  if (!wifiManager.autoConnect("TemperatureControl")) {
    Serial.println("Failed to connect and hit timeout");
    delay(3000);
    // Reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }
  
  // If you get here you have connected to the WiFi
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  // Configure time
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  syncTimeWithNTP();
  
  // Set up MQTT
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);
  
  // Set up web server routes
  server.on("/", handleRoot);
  server.on("/toggleStatus", handleToggleStatus);
  server.on("/toggleMode", handleToggleMode);
  server.on("/updateSettings", updateSettings);
  
  // Start the server
  server.begin();
  Serial.println("HTTP server started");
  
  // Initial MQTT connection attempt
  mqttConnect();
}

void loop(){

  // Read temperature
  currentTemp = readTemperature();

  // Handle incoming client requests
  server.handleClient();

  // Call mqttClient.loop() on every loop iteration
  mqttClient.loop();

  // Handle MQTT connection
  if (!mqttClient.connected()) {
    unsigned long now = millis();
    if (now - lastReconnectAttempt > RECONNECT_INTERVAL) {
      lastReconnectAttempt = now;
      if (mqttConnect()) {
        lastReconnectAttempt = 0;
      }
    }
  } else {
    
    handleTemperatureControl();
    
    // Publish temperature and state periodically
    unsigned long now = millis();
    if (now - lastMqttPublish > MQTT_PUBLISH_INTERVAL) {
      publishTemperature();
      publishState();
      lastMqttPublish = now;
    }
  }
  
  // Periodically print status to serial
  static unsigned long lastStatusPrint = 0;
  if (millis() - lastStatusPrint > 60000) { // Every minute
    printStatus();
    lastStatusPrint = millis();
  }
  
  // Periodically sync time with NTP
  if (millis() - lastNtpSync > NTP_SYNC_INTERVAL_MS) {
    syncTimeWithNTP();
    lastNtpSync = millis();
  }
  
  // Add a small delay to prevent watchdog reset
  delay(10);
}