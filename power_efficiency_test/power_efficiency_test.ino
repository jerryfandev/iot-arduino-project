/**
 * @file power_efficiency_test.ino
 * @brief ESP32 Adaptive Daylight Harvesting Firmware (Real-Time Hardware Telemetry)
 * 
 * Hardware Target: DFRobot FireBeetle ESP32-E v1.0
 * Output: WS2812 RGB LED Bar (30 Pixels) on GPIO 25, powered via 5V VCC
 * Input: VEML7700 Digital Light Sensor on I2C (SDA=21, SCL=22)
 * 
 * This firmware actively computes and integrates energy savings locally on the ESP32 silicon 
 * based on dynamic lighting conditions, overriding the need for synthetic Python modeling.
 * It streams 5 precise telemetry values over Serial every 500ms for data logging.
 */

#include <Arduino.h>
#include <Wire.h>
#include "DFRobot_VEML7700.h" // Swap to Adafruit_VEML7700.h if using their module
#include <Adafruit_NeoPixel.h>

// --- Hardware Definitions ---
#define LED_PIN    25
#define LED_COUNT  30

// (I2C pins are automatically mapped by the FireBeetle board package)

// --- Object Instantiation ---
DFRobot_VEML7700 veml;
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// --- State Variables ---
unsigned long lastReadTime = 0;
constexpr unsigned long READ_INTERVAL_MS = 500;
float accumulated_savings_wh = 0.0;

void setup() {
  Serial.begin(115200);
  delay(100); // Brief pause to allow Serial to stabilize
  
  // 1. Initialize I2C Bus & Sensor
  Wire.begin();
  
  Wire.beginTransmission(0x10);
  if (Wire.endTransmission() == 0) {
    Serial.println("System: VEML7700 validated.");
  }
  veml.begin();
  
  // 2. Initialize WS2812 LEDs
  strip.begin();
  strip.show(); // Turn off all pixels immediately
  
  // 3. Print CSV Header for Serial Plotter / Data Logging Sync
  Serial.println("Lux,LED_Brightness,InstantPower_W,SavedPower_W,AccumulatedSavings_Wh");
}

void loop() {
  unsigned long currentMillis = millis();
  
  if (currentMillis - lastReadTime >= READ_INTERVAL_MS) {
    // Determine precise delta-time integration in seconds (accounts for any loop latency)
    float dt_seconds = (currentMillis - lastReadTime) / 1000.0;
    lastReadTime = currentMillis;

    // ==========================================================
    // 1. ENVIRONMENTAL SENSING
    // ==========================================================
    float lux = 0.0f;
    veml.getALSLux(lux); // Use lux = veml.readLux(); if using Adafruit library

    // ==========================================================
    // 2. CLOSED-LOOP FIRMWARE MAPPING
    // ==========================================================
    // Map: 100 Lux -> 255 Brightness  |  500 Lux -> 0 Brightness
    float calc_brightness = 255.0 - ((lux - 100.0) / 400.0) * 255.0;
    
    // Strictly constrain output to valid 8-bit PWM bounds (0-255)
    if (calc_brightness < 0) calc_brightness = 0;
    if (calc_brightness > 255) calc_brightness = 255;
    int brightness = (int)calc_brightness;

    // Apply brightness and push full white (RGB 255,255,255) to draw realistic silicon current
    strip.setBrightness(brightness);
    for(int i = 0; i < LED_COUNT; i++) {
      strip.setPixelColor(i, strip.Color(255, 255, 255));
    }
    strip.show();

    // ==========================================================
    // 3. MATHEMATICAL POWER MODEL (SILICON AUDIT)
    // ==========================================================
    // Operational Current: 1.0mA base static + 49.0mA active delta per pixel
    float pixel_current_A = (1.0 + 49.0 * (brightness / 255.0)) / 1000.0;
    float total_current_A = LED_COUNT * pixel_current_A;
    
    // VCC Bus is 5.0 Volts
    float instant_power_W = 5.0 * total_current_A;

    // Benchmark Baseline: 30 LEDs locked at 100% Brightness (50mA each = 1.5A = 7.5W)
    float baseline_power_W = 7.5;
    float saved_power_W = baseline_power_W - instant_power_W;

    // ==========================================================
    // 4. ENERGY INTEGRATION (JOULES TO WATT-HOURS)
    // ==========================================================
    // E = P * t  (Joules = Watts * seconds)
    float energy_saved_joules = saved_power_W * dt_seconds;
    
    // Accumulate continuous Watt-hours 
    accumulated_savings_wh += (energy_saved_joules / 3600.0);

    // ==========================================================
    // 5. SERIAL EXPORT (ROBUST CSV FORMAT)
    // ==========================================================
    Serial.print(lux, 2);
    Serial.print(",");
    Serial.print(brightness);
    Serial.print(",");
    Serial.print(instant_power_W, 3);
    Serial.print(",");
    Serial.print(saved_power_W, 3);
    Serial.print(",");
    Serial.println(accumulated_savings_wh, 6);
  }
}
