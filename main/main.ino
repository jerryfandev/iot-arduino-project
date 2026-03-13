#include <Adafruit_NeoPixel.h>

constexpr uint8_t ledPin = 2; // Changed to PIN 1 (port "1 2" at the top edge of the board)
constexpr uint8_t sensorPin = 4; // Ambient light sensor analog pin

#define NUM_LEDS 5
Adafruit_NeoPixel pixels(NUM_LEDS, ledPin, NEO_GRB + NEO_KHZ800);

void wifiConnect();
void mqttSetup();
void mqttLoop();

// Light state received from MQTT topic `home/livingroom/light` ("ON"/"OFF").
// Default OFF, only turn ON and use ambient sensor when ON command is received.
bool gLightOn = false;
bool gNeedsUpdate = true; // Flag indicating LED update needed

void setup() {
  Serial.begin(115200);
  delay(200);
  wifiConnect();
  mqttSetup();

  pixels.begin();
  pixels.setBrightness(128);
  pixels.show(); // Initialize all pixels to 'off'
}

void loop() {
  mqttLoop();

  // Only call pixels.show() when state changes
  if (gNeedsUpdate) {
    if (gLightOn) {
      pixels.setBrightness(128);
      for(int i=0; i<NUM_LEDS; i++) {
        pixels.setPixelColor(i, pixels.Color(255, 255, 255));
      }
    } else {
      pixels.clear(); // Set all pixel colors to 'off'
    }
    pixels.show();
    gNeedsUpdate = false;
    Serial.println(gLightOn ? "[LED] Updated: ON" : "[LED] Updated: OFF");
  }
  
  // Small delay to yield CPU for background tasks (WiFi/MQTT)
  delay(20);
}