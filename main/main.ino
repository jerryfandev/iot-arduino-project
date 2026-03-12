#include <FastLED.h>

constexpr uint8_t ledPin = 37; // Data pin for WS2812B LED strip
constexpr uint8_t sensorPin = 6; // Ambient light sensor analog pin

#define NUM_LEDS 5
CRGB leds[NUM_LEDS];

void wifiConnect();

void setup() {
  Serial.begin(115200);
  delay(200);
  wifiConnect();

  FastLED.addLeds<WS2812B, ledPin, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(64);
}

void loop() {
  const int lightLevel = analogRead(sensorPin); // ESP32 ADC is typically 0..4095

  // Darker room (lower reading) -> brighter LEDs.
  int brightness = map(lightLevel, 0, 4095, 255, 0);
  brightness = constrain(brightness, 0, 255);

  FastLED.setBrightness(static_cast<uint8_t>(brightness));

  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB::White;
  }

  FastLED.show();
  delay(50);
}