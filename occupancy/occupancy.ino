/**
 * @file occupancy.ino
 * @brief I2C Scanner to verify sensor wiring on ESP32-S3
 * 
 * Hardware Target: ESP32-S3
 * 
 * Pin Map Allocation:
 *   - I2C SDA: GPIO 1
 *   - I2C SCL: GPIO 2
 * 
 * Description:
 * This script scans the I2C bus for active devices. It is used to verify that the
 * sensor is correctly wired to the ESP32-S3's I2C pins (SDA=1, SCL=2) and is
 * responding to I2C communication. The results are printed to the Serial Monitor.
 */

#include <Arduino.h>
#include <Wire.h>

// ---------------------------------------------------------
// HARDWARE PIN DEFINITIONS
// ---------------------------------------------------------
constexpr int I2C_SDA_PIN = 1;
constexpr int I2C_SCL_PIN = 2;

void setup() {
  // Initialize hardware serial interface at 115200 baud
  Serial.begin(115200);
  
  // Wait briefly for serial monitor synchronization
  while (!Serial) {
    delay(10);
  }

  Serial.println("\nI2C Scanner Initializing...");

  // Initialize hardware I2C bus using the explicit ESP32-S3 pins
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Serial.printf("I2C Pins Configured - SDA: GPIO %d, SCL: GPIO %d\n", I2C_SDA_PIN, I2C_SCL_PIN);
  Serial.println("Scanning I2C Bus...\n");
}

void loop() {
  byte error, address;
  int nDevices = 0;

  Serial.println("Scanning...");

  // The i2c_scanner uses the return value of
  // the Write.endTransmission to see if
  // a device did acknowledge to the address.
  for (address = 1; address < 127; address++ ) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("I2C device found at address 0x");
      if (address < 16) {
        Serial.print("0");
      }
      Serial.print(address, HEX);
      Serial.println("  !");

      nDevices++;
    } else if (error == 4) {
      Serial.print("Unknown error at address 0x");
      if (address < 16) {
        Serial.print("0");
      }
      Serial.println(address, HEX);
    }
  }

  if (nDevices == 0) {
    Serial.println("No I2C devices found. Check wiring (Power, GND, SDA, SCL) and pull-up resistors.");
  } else {
    Serial.println("done\n");
  }

  // Wait 5 seconds for next scan
  delay(5000);
}
