# ESP8266 Temperature Control System

## Overview

This project implements a web-based temperature control system using an ESP8266 microcontroller. It is designed to monitor temperature via a DS18B20 sensor and control a 433 MHz relay switch for heating or cooling. The system features a modern web interface for configuration, automatic/manual relay control, persistent settings, and NTP time synchronization. It is compatible with Home Assistant via MQTT and can be easily integrated into any smart home system.

---

## Features

- **Temperature Sensing:**  
  Reads temperature from a DS18B20 sensor.

- **Relay Control:**  
  Controls a 433 MHz relay (for heating or cooling) with both automatic (temperature/time-based) and manual modes.

- **Web Interface:**  
  - Modern, responsive interface accessible from any browser.
  - Toggle between Auto and Manual control modes.
  - In Auto mode, relay is controlled by temperature and time schedules.
  - In Manual mode, relay can be toggled directly by the user.
  - Settings for temperature setpoint, delta, frost protection, and on/off schedules.
  - Displays current temperature, relay status, and relay switch history (last ON/OFF times and ON duration).

- **Persistent Settings:**  
  Stores user configuration in EEPROM, minimizing writes to extend EEPROM lifespan.

- **Time Synchronization:**  
  - Synchronizes time with NTP servers.
  - Handles daylight saving time for Berlin timezone.
  - Periodically resynchronizes every 6 hours.

- **WiFi Management:**  
  Uses WiFiManager for easy and secure WiFi setup.

### 🌡️ Core Temperature Control
- **Precise Temperature Monitoring**
  - DS18B20 digital temperature sensor with ±0.5°C accuracy
  - Configurable temperature setpoint and hysteresis (delta)
  - Frost protection with adjustable threshold
  - Temperature filtering for stable readings

### ⚡ Relay Control
- **Flexible Relay Operation**
  - 433 MHz RF relay control (supporting both ON/OFF and toggle modes)
  - Configurable pulse duration for relay control
  - Manual override capability

### 🌐 Web Interface
- **Modern, Responsive UI**
  - Real-time temperature display
  - Toggle between Auto/Manual modes
  - Configure temperature settings and schedules
  - View system status and connection information
  - Mobile-friendly design

### 🤖 MQTT Integration (Home Assistant Compatible)
- **Seamless Smart Home Integration**
  - Publish temperature updates at configurable intervals
  - Remote control via MQTT commands
  - Secure authentication support

#### MQTT Topics
- **Command Topic:** `home/temperature_control/set`
  - Send JSON payloads to control the device:
    ```json
    {
      "state": "ON",
      "target_temp": 22.5,
      "delta_temp": 0.5,
      "frost_protection": 5.0
    }
    ```
- **State Topic:** `home/temperature_control/state`
  - Current device state in JSON format
- **Temperature Topic:** `home/temperature_control/temperature`
  - Current temperature readings
- **Availability Topic:** `home/temperature_control/status`
  - Reports "online" or "offline" status

### ⏰ Smart Scheduling
- **Time-based Control**
  - Automatic daylight saving time adjustment
  - NTP time synchronization

### 🔄 System Management
- **Persistent Configuration**
  - EEPROM storage for settings
  - WiFiManager for easy WiFi configuration
  - System status monitoring

---

## Main Components

### Hardware

- ESP8266 microcontroller (e.g., NodeMCU, Wemos D1 Mini)
- DS18B20 temperature sensor
- 433 MHz transmitter module
- 433 MHz relay switch

### Libraries Used

- `OneWire` / `DallasTemperature` — for DS18B20 sensor
- `RCSwitch` — for 433 MHz relay communication
- `EEPROM` — for persistent storage
- `ESP8266WiFi`, `ESP8266WebServer` — for WiFi and web server
- `WiFiManager` — for WiFi configuration
- `time.h` — for NTP and time management

---

## Code Structure

- **Global Settings & Variables:**  
  Pin assignments, relay protocol, EEPROM size, NTP sync interval, and main variables for temperature, relay status, and user settings.

- **Setup:**  
  - Initializes hardware interfaces and libraries.
  - Loads settings from EEPROM.
  - Sets up WiFi and NTP.
  - Starts the web server and registers HTTP handlers.

- **Loop:**  
  - Handles web server requests.
  - Periodically reads temperature (non-blocking).
  - In Auto mode, executes temperature/time-based relay control.
  - Periodically resynchronizes time with NTP.

- **Web Interface:**  
  - Built dynamically via `buildWebPage()`.
  - Uses modern CSS and JavaScript for a user-friendly experience.
  - Displays current status, relay history, and settings forms.
  - Allows toggling between Auto/Manual modes and updating settings.

- **Relay Control:**  
  - `setRelay(bool on)` switches the relay and records ON/OFF times and durations.
  - In Auto mode, relay is controlled by temperature and time logic.
  - In Manual mode, relay is controlled by user input.

- **EEPROM Management:**  
  - Reads and writes settings only when changes are detected to minimize EEPROM wear.

- **Time Handling:**  
  - Uses NTP for accurate timekeeping.
  - Adjusts for daylight saving time.

---

## Security

- The web interface is currently open and does not require authentication. For security, consider adding password protection if the device is accessible from outside a trusted network.

---

## Usage

1. **Connect Hardware:**  
   Wire the DS18B20 sensor and 433 MHz transmitter to the ESP8266 as per the pin assignments.

2. **Upload Code:**  
   Flash the code to your ESP8266 using the Arduino IDE or PlatformIO.

3. **Connect to WiFi:**  
   On first boot, the device will start in WiFiManager mode. Connect to the ESP8266 AP and configure your WiFi credentials.

4. **Access Web Interface:**  
   Find the ESP8266’s IP address (shown in serial monitor or router) and open it in your browser.

5. **Configure Settings:**  
   Use the web interface to set temperature setpoints, delta, frost protection, and time schedules. Toggle between Auto and Manual modes as needed.

---

## Customization

- Adjust pin assignments, relay protocol, and temperature logic as needed for your specific hardware.
- Enhance security by adding authentication to the web interface.
- Extend the UI or backend for additional features such as logging or notifications.

---

## License

This project is open source and provided as-is, without warranty.  
Feel free to modify and adapt it to your needs!

---

## 🏗️ Hardware Setup

### Wiring
```
ESP8266 (NodeMCU)    DS18B20         433MHz Transmitter
===============      ======         ===============
3.3V                VDD (Red)      VCC
D4 (GPIO2)          DQ (Yellow)    DATA
GND                 GND (Black)    GND
                                  
Add a 4.7kΩ pull-up resistor between DQ and VDD on the DS18B20
```

### Required Hardware
- ESP8266 (NodeMCU, Wemos D1 Mini, etc.)
- DS18B20 Waterproof Temperature Sensor
- 433MHz RF Transmitter Module
- 433MHz Relay Switch
- 4.7kΩ Resistor (for DS18B20)
- Breadboard and jumper wires
- 5V Power Supply

---

## 🔌 Home Assistant Integration

### Manual Configuration
Add `temperature_control_package.yaml` to your homeassistant file directory and add the following to your `configuration.yaml`:

```yaml
homeassistant:
  packages:
    thermostat: !include thermostat_package.yaml
