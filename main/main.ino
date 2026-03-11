#include <Adafruit_NeoPixel.h>

// Onboard addressable RGB LED (NeoPixel) on IO48
const int RGB_PIN    = 48;
const int RGB_COUNT  = 1;

Adafruit_NeoPixel rgb(RGB_COUNT, RGB_PIN, NEO_GRB + NEO_KHZ800);

void setup() {
  // Initialize Serial at 115200 baud
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n--- ESP32-S3 TEST ---");
  
  // 1. Check chip information
  Serial.printf("Chip Model: %s\n", ESP.getChipModel());
  Serial.printf("Chip Cores: %d\n", ESP.getChipCores());
  

  // 2. Check flash memory (important for running AI later)
  Serial.printf("Flash Size: %d MB\n", ESP.getFlashChipSize() / (1024 * 1024));
  
  // Check PSRAM (if > 0, OPI PSRAM is enabled correctly)
  if (psramInit()) {
    Serial.printf("PSRAM Size: %d MB (Detected - Good!)\n", ESP.getPsramSize() / (1024 * 1024));
  } else {
    Serial.println("PSRAM: Not detected (please re-check Tools -> PSRAM settings)");
  }

  Serial.println("------------------------------");
  Serial.println("If you see this message repeatedly, your ESP32-S3 is running correctly!");

  // Initialize NeoPixel RGB LED on IO48
  rgb.begin();
  rgb.show(); // Turn off LED at startup
}

// Helper function to set RGB LED color (0–255)
void setRgbColor(uint8_t r, uint8_t g, uint8_t b) {
  rgb.setPixelColor(0, rgb.Color(r, g, b));
  rgb.show();
}

void loop() {
  Serial.println("Hello World! Tom is waiting for commands...");
  
  // Green
  setRgbColor(0, 255, 0);
  delay(1000);

  // Red
  setRgbColor(255, 0, 0);
  delay(1000);

  // Purple (Red + Blue)
  setRgbColor(255, 0, 255);
  delay(1000);

  // Yellow (Red + Green)
  setRgbColor(255, 255, 0);
  delay(1000);
}