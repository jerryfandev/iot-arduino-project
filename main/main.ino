#include <FastLED.h>

constexpr uint8_t ledPin = 37; // Data pin for WS2812B LED strip
constexpr uint8_t sensorPin = 6; // Ambient light sensor analog pin

#define NUM_LEDS 5
CRGB leds[NUM_LEDS];

void wifiConnect();
void mqttSetup();
void mqttLoop();

// Trạng thái đèn nhận từ MQTT topic `home/livingroom/light` ("ON"/"OFF`).
// Mặc định OFF, chỉ khi nhận được lệnh ON mới bật và dùng ambient sensor.
bool gLightOn = false;

void setup() {
  Serial.begin(115200);
  delay(200);
  wifiConnect();
  mqttSetup();

  FastLED.addLeds<WS2812B, ledPin, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(64);
}

void loop() {
  mqttLoop();

  if (!gLightOn) {
    for (int i = 0; i < NUM_LEDS; i++) {
      leds[i] = CRGB::Black;
    }
    FastLED.show();
    delay(50);
    return;
  }

  // Ambient light sensor logic được tạm tắt.
  // const int lightLevel = analogRead(sensorPin); // ESP32 ADC is typically 0..4095
  // int brightness = map(lightLevel, 0, 4095, 255, 0);
  // brightness = constrain(brightness, 0, 255);
  // FastLED.setBrightness(static_cast<uint8_t>(brightness));
  FastLED.setBrightness(128);

  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB::White;
  }

  FastLED.show();
  delay(50);
}