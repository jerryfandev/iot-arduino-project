/**
 * @file test_luminance.ino
 * @brief Benchmark test for digital VEML7700 ambient light sensor vs. Raw Analog light sensor.
 * 
 * Hardware Target: DFRobot FireBeetle ESP32-E v1.0 / ESP32-WROOM-32E
 * 
 * Pin Map Allocation:
 *   - I2C SDA: GPIO 21 (Default FireBeetle SDA)
 *   - I2C SCL: GPIO 22 (Default FireBeetle SCL)
 *   - Analog Sensor: GPIO 36 (A0 footprint pin)
 * 
 * Description:
 * This script reads absolute lux from the VEML7700 via I2C, and concurrently reads 
 * raw analog values (0-4095) from GPIO 36. It calculates the corresponding voltage 
 * using a 3.3V reference. Data is formatted as comma-separated values (CSV) every 
 * 500ms, heavily optimized for real-time visualization in the Arduino IDE Serial Plotter.
 */

#include <Arduino.h>
#include <Wire.h>
#include "DFRobot_VEML7700.h" // Replace with <Adafruit_VEML7700.h> if using the Adafruit module

// ---------------------------------------------------------
// HARDWARE PIN DEFINITIONS
// ---------------------------------------------------------
// Explicit mapping for the Analog light sensor (A0 on FireBeetle ESP32-E)
constexpr int ANALOG_LIGHT_PIN = 36; 

// Default I2C pins for ESP32 FireBeetle
constexpr int I2C_SDA_PIN = 21;
constexpr int I2C_SCL_PIN = 22;

// ---------------------------------------------------------
// SENSOR INSTANTIATION & CONSTANTS
// ---------------------------------------------------------
// Initialize the VEML7700 object. 
DFRobot_VEML7700 veml; 

// ADC configuration constants
constexpr float ADC_REFERENCE_VOLTAGE = 3.3f;
constexpr float ADC_MAX_VALUE = 4095.0f; // 12-bit ADC max value on ESP32

// Polling interval for standard 500ms updates
constexpr unsigned long READ_INTERVAL_MS = 500;
unsigned long lastReadTime = 0;

void setup() {
  // 1. Initialize hardware serial interface at 115200 baud
  Serial.begin(115200);
  
  // Wait briefly for serial monitor synchronization
  while (!Serial) {
    delay(10);
  }

  // 2. Configure the analog pin mode
  pinMode(ANALOG_LIGHT_PIN, INPUT);

  // Configure ESP32 ADC attenuation to safely read up to full scale (approx 3.3V).
  // 11dB attenuation provides the maximum measurable voltage range.
  analogSetPinAttenuation(ANALOG_LIGHT_PIN, ADC_11db);

  // 3. Initialize hardware I2C bus using the explicit FireBeetle pins
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  // 4. Verification logic to ensure the VEML7700 is responding on the I2C bus
  Wire.beginTransmission(0x10); // Standard VEML7700 I2C address
  byte error = Wire.endTransmission();

  if (error == 0) {
    Serial.println("System: VEML7700 digital sensor validated on I2C bus.");
  } else {
    Serial.println("Error: VEML7700 digital sensor not found! Please check wiring.");
  }

  // 5. Initialize the VEML7700 digital light sensor configuration
  veml.begin();
  
  // Delay to allow sensor to stabilize
  delay(100);

  // Print CSV header for Serial Plotter / Data logging
  // Format: "Lux,Raw_Analog,Voltage(V)"
  Serial.println("Lux,RawAnalog,Voltage_V");
}

void loop() {
  unsigned long currentMillis = millis();

  // Non-blocking timer for reading at the specified interval
  if (currentMillis - lastReadTime >= READ_INTERVAL_MS) {
    lastReadTime = currentMillis;

    // -------------------------------------------------------
    // SENSOR READINGS
    // -------------------------------------------------------
    
    // Read absolute lux value from the digital VEML7700 sensor
    float lux = 0.0f;
    veml.getALSLux(lux); // Standard DFRobot method. (If using Adafruit, change to: lux = veml.readLux();)

    // Read raw 12-bit analog input value from GPIO 36
    int rawAnalog = analogRead(ANALOG_LIGHT_PIN);

    // Calculate corresponding voltage based on 3.3V reference
    float voltage = (rawAnalog / ADC_MAX_VALUE) * ADC_REFERENCE_VOLTAGE;

    // -------------------------------------------------------
    // DATA OUTPUT (SERIAL PLOTTER FORMAT)
    // -------------------------------------------------------
    // Print data strings in a clean, comma-separated format
    Serial.print(lux, 2);       // Absolute lux with 2 decimal places
    Serial.print(",");
    Serial.print(rawAnalog);    // Raw 12-bit analog reading (0 - 4095)
    Serial.print(",");
    Serial.println(voltage, 3); // Calculated voltage with 3 decimal places
  }
}
