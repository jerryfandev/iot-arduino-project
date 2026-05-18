#include "ESP_I2S.h"
#include "ESP_SR.h"
#include <Adafruit_NeoPixel.h>

constexpr uint8_t ledPin =
    2; // Changed to PIN 1 (port "1 2" at the top edge of the board)
constexpr uint8_t lightSensorPin = 6; // Digital light sensor pin

#define NUM_LEDS 5
Adafruit_NeoPixel pixels(NUM_LEDS, ledPin, NEO_GRB + NEO_KHZ800);

void wifiConnect();
void timeSetup();
void mqttSetup();
void mqttLoop();

void commandsSetup();
void commandsLoop();

// Light state received from MQTT topic `home/livingroom/light` ("ON"/"OFF").
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

  pinMode(lightSensorPin, INPUT);

  pixels.begin();
  pixels.setBrightness(128);
  pixels.show(); // Initialize all pixels to 'off'
}

void loop() {
  mqttLoop();
  commandsLoop(); // Give CPU to WebSocket & Voice Processing

  static int currentBrightness = 0; // Current actual brightness
  int targetBrightness = 0;         // Target brightness based on state/sensor

  // Determine Target Brightness
  if (gLightOn) {
    if (gLightSensorEnabled) {
      // Read Analog Sensor (Pin 6) as per user request
      // "a brighter environment will result in a higher analog value"
      int analogVal = analogRead(lightSensorPin);

      // Map Analog Value to Brightness
      // Input: 0 (Dark) -> 4095 (Bright) (Assuming ESP32 12-bit ADC)
      // Output: 255 (Max Brightness) -> 10 (Min Brightness)
      // "The brighter the environment, the dimmer the LED" -> Higher analog =
      // Lower brightness
      targetBrightness = map(analogVal, 0, 4095, 255, 10);
      targetBrightness = constrain(targetBrightness, 0, 255);

      // Debug print every 500ms
      static unsigned long lastDebug = 0;
      if (millis() - lastDebug > 500) {
        lastDebug = millis();
        Serial.print("[Sensor] Analog(6): ");
        Serial.print(analogVal);
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
