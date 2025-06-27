# WiFiManager Integration for ESP8266

This project uses the WiFiManager library to securely manage WiFi credentials for the ESP8266. Instead of hardcoding your WiFi SSID and password in the code, WiFiManager allows you to configure these settings through a web portal on first boot or when the device can't connect to WiFi.

## How It Works
- On first boot, or if WiFi credentials are missing/invalid, the ESP8266 creates its own access point (AP).
- Connect to this AP with your phone or computer. The AP will be named something like `ESP8266-Config`.
- Open a browser and you'll be redirected to a captive portal to enter your WiFi credentials.
- The ESP8266 stores these credentials in flash memory and uses them for future connections.

## How to Add WiFiManager to Your Project
1. **Install the WiFiManager Library:**
   - In Arduino IDE, go to Library Manager and search for `WiFiManager` by tzapu. Install it.
2. **Add to Your Code:**
   - Include the library and replace your WiFi connection code in `setup()` with the WiFiManager logic (see below).
3. **Example Code Snippet:**

```cpp
#include <ESP8266WiFi.h>
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager

void setup() {
  Serial.begin(115200);
  WiFiManager wifiManager;
  // Uncomment for testing to erase stored credentials
  // wifiManager.resetSettings();
  wifiManager.autoConnect("ESP8266-Config");
  Serial.println("Connected to WiFi!");
}
```

4. **Remove Hardcoded Credentials:**
   - Delete or comment out any `const char* ssid` and `const char* password` variables from your code.

## Benefits
- Credentials are not stored in your code or version control.
- Easy to change WiFi settings without reflashing the device.
- More secure and user-friendly.

## More Info
See the [WiFiManager GitHub page](https://github.com/tzapu/WiFiManager) for advanced options and troubleshooting.
