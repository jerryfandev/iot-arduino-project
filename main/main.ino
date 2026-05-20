#include "ESP_I2S.h"
#include "ESP_SR.h"
#include <Adafruit_NeoPixel.h>

#include <Wire.h>
#include <DFRobot_VEML7700.h>

constexpr uint8_t ledPin = 3; 

constexpr int I2C_SDA_PIN = 1;
constexpr int I2C_SCL_PIN = 2;

DFRobot_VEML7700 veml;

#define NUM_LEDS 5
Adafruit_NeoPixel pixels(NUM_LEDS, ledPin, NEO_GRB + NEO_KHZ800);

void wifiConnect();
void timeSetup();
void mqttSetup();
void mqttLoop();

void commandsSetup();
void commandsLoop();

void occupancySetup();
void occupancyLoop();

// Light commands received from MQTT topic `home/livingroom/light/set` ("ON"/"OFF").
// Light state is published to `home/livingroom/light/state`.
// Default OFF, only turn ON and use ambient sensor when ON command is received.
bool gLightOn = false;
bool gLightSensorEnabled =
    false; // Controlled by MQTT topic `home/livingroom/light-sensor`
bool gNeedsUpdate = true; // Flag indicating LED update needed

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Hello ESP32-S3!");
  wifiConnect(); // Block here until WiFi is connected.
  timeSetup();   // Perth/AWST clock for scheduled MQTT commands.
  mqttSetup();
  commandsSetup(); // Start local voice recognition
  occupancySetup(); // Start multimodal occupancy sensing

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  veml.begin();

  pixels.begin();
  pixels.setBrightness(128);
  pixels.show(); // Initialize all pixels to 'off'
}

void loop() {
  mqttLoop();
  commandsLoop(); // Give CPU to WebSocket & Voice Processing
  occupancyLoop(); // Process occupancy rules and auto-control light

  static int currentBrightness = 0; // Current actual brightness
  int targetBrightness = 0;         // Target brightness based on state/sensor

  // Determine Target Brightness
  if (gLightOn) {
    if (gLightSensorEnabled) {
      float lux = 0.0f;
      veml.getALSLux(lux);
      
      // Map Lux to Brightness (0 Lux -> 255, ~500 Lux -> 10)
      // The brighter the environment, the dimmer the LED
      if (lux > 500) {
        targetBrightness = 10;
      } else {
        targetBrightness = map((long)lux, 0, 500, 255, 10);
      }
      targetBrightness = constrain(targetBrightness, 10, 255);

      // Debug print every 500ms
      static unsigned long lastDebug = 0;
      if (millis() - lastDebug > 500) {
        lastDebug = millis();
        Serial.print("[Sensor] Lux: ");
        Serial.print(lux);
        Serial.print(" -> Target: ");
        Serial.println(targetBrightness);
      }
    } else {
      targetBrightness = 255; // Full brightness when sensor disabled
    }
  } else {
    targetBrightness = 0; // Light OFF
  }

  // Smooth Transition Logic (Fading)
  if (currentBrightness != targetBrightness) {
    int diff = targetBrightness - currentBrightness;

    // Step size: larger diff = faster step, but keep it smooth
    int step = 1;
    if (abs(diff) > 30)
      step = 5;
    else if (abs(diff) > 10)
      step = 2;

    if (abs(diff) <= step) {
      currentBrightness = targetBrightness;
    } else {
      currentBrightness += (diff > 0 ? step : -step);
    }

    // Apply to LEDs
    if (currentBrightness > 0) {
      pixels.setBrightness(currentBrightness);
      for (int i = 0; i < NUM_LEDS; i++) {
        pixels.setPixelColor(i, pixels.Color(255, 255, 255));
      }
    } else {
      pixels.clear();
    }
    pixels.show();

    // Debug transition end
    if (currentBrightness == targetBrightness) {
      Serial.print("[LED] Stable Brightness: ");
      Serial.println(currentBrightness);
    }
  }

  delay(20); // Cycle delay
}
