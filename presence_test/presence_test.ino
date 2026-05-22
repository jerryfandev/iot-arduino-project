/**
 * @file presence_test.ino
 * @brief ESP32 Presence Benchmarking (PIR vs C4001 mmWave Radar)
 * 
 * Hardware Target: DFRobot FireBeetle ESP32-E v1.0
 * PIR Sensor: GPIO 26 (Digital Input)
 * mmWave Radar (C4001): UART Serial2 (RX=GPIO 16, TX=GPIO 17) @ 9600 baud
 * 
 * This firmware polls the PIR sensor and UART buffer every 200ms.
 * The C4001 mmWave radar uses an NMEA-style ASCII protocol:
 * Sentence format: $DFHPD,<status>, , , *<checksum>\r\n
 * Status byte '1' = Presence Detected | '0' = No Presence
 */

#include <Arduino.h>

// --- Hardware Definitions ---
constexpr int PIR_PIN = 26;
constexpr int MMWAVE_RX_PIN = 16;
constexpr int MMWAVE_TX_PIN = 17;

// --- Timing Variables ---
unsigned long lastPollTime = 0;
constexpr unsigned long POLL_INTERVAL_MS = 200;

// --- State Variables ---
bool mmWaveOccupied = false;
int pir_State = 0;

// --- NMEA Line Buffer ---
// C4001 max sentence: "$DFHPD,1, , , *XX\r\n" (~22 chars)
constexpr int LINE_BUF_LEN = 64;
char line_buf[LINE_BUF_LEN];
int line_pos = 0;

void setup() {
  // 1. Initialize primary debugging Serial for PC data logging
  Serial.begin(115200);
  
  // 2. Initialize hardware Serial2 for mmWave Sensor
  // Format: Serial2.begin(baud, protocol, RX_PIN, TX_PIN)
  Serial2.begin(9600, SERIAL_8N1, MMWAVE_RX_PIN, MMWAVE_TX_PIN);

  // 3. Map digital PIR sensor pin
  // INPUT_PULLDOWN prevents the GPIO from floating HIGH when the PIR output is idle.
  // Most PIR sensors are active-HIGH (output goes HIGH on motion detection).
  // If your PIR is active-LOW, change this to INPUT_PULLUP.
  pinMode(PIR_PIN, INPUT_PULLDOWN);

  // Allow hardware to stabilize
  delay(100);

  // 4. Print clean CSV header
  Serial.println("PIR_State,mmWave_State");
}

void loop() {
  unsigned long currentMillis = millis();

  // Continuously read NMEA-style ASCII lines from the C4001 UART stream
  while (Serial2.available() > 0) {
    char c = (char)Serial2.read();

    if (c == '\n') {
      // Null-terminate and process the complete line
      line_buf[line_pos] = '\0';

      // Expected format: $DFHPD,<status>, , , *<checksum>
      // Check that the line starts with the correct sentence ID
      if (strncmp(line_buf, "$DFHPD,", 7) == 0 && line_pos > 7) {
        // The presence status digit is at character index 7
        char statusChar = line_buf[7];
        if (statusChar == '1') {
          mmWaveOccupied = true;
        } else if (statusChar == '0') {
          mmWaveOccupied = false;
        }
      }

      // Reset buffer for the next sentence
      line_pos = 0;
    } else if (c != '\r') {
      // Append character, guard against buffer overflow
      if (line_pos < LINE_BUF_LEN - 1) {
        line_buf[line_pos++] = c;
      } else {
        // Overflow guard: discard the malformed sentence and reset
        line_pos = 0;
      }
    }
  }

  // Non-blocking 200ms evaluation loop
  if (currentMillis - lastPollTime >= POLL_INTERVAL_MS) {
    lastPollTime = currentMillis;

    // Read the digital state of the PIR sensor
    pir_State = digitalRead(PIR_PIN);

    // Output clean binary CSV string without extra text decoration
    Serial.print(pir_State);
    Serial.print(",");
    Serial.println(mmWaveOccupied ? 1 : 0);
  }
}
