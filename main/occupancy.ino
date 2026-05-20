#include <Arduino.h>

// ============================================================
// GLOBAL STATE FOR OCCUPANCY (Motion Only)
// ============================================================
#define RADAR_COOLDOWN_MS 30000 // 30 seconds hold time for motion trigger

// mmWave Radar Pins
#define RADAR_RX_PIN 16
#define RADAR_TX_PIN 17

static unsigned long lastSampleTime = 0;
static const unsigned long SAMPLE_INTERVAL_MS = 200;

static bool radarOccupied = false;
static unsigned long lastHoldTimer = 0;

// UART parsing buffers
#define LINE_BUF_LEN 64
static char line_buf[LINE_BUF_LEN];
static int line_pos = 0;

static int lastFusedState = -1; // Track transitions

extern bool gLightOn;
extern bool gNeedsUpdate;
extern bool gMotionSensorEnabled;
extern unsigned long gLastExternalControlTime;
void mqttPublishOccupancy(int occupied); // Defined in mqtt.ino

// Stub function to prevent compilation errors in commands.ino
void occupancyProcessAudio(const int16_t *samples, size_t sample_count,
                           uint8_t channels) {
  // Sound detection disabled
}

// ============================================================
// INITIALIZATION
// ============================================================
void occupancySetup() {
  // Initialize Serial2 for mmWave Radar
  Serial2.begin(9600, SERIAL_8N1, RADAR_RX_PIN, RADAR_TX_PIN);
  Serial.println("[Occupancy] Initialized mmWave Radar on Serial2");

  lastSampleTime = millis();

  // Ensure the cooldown is expired on boot
  lastHoldTimer = millis() - RADAR_COOLDOWN_MS;
  lastFusedState = 0;
}

// ============================================================
// MAIN LOOP
// ============================================================
void occupancyLoop() {
  // 1. NON-BLOCKING RADAR PARSING
  while (Serial2.available() > 0) {
    char c = (char)Serial2.read();
    if (c == '\n') {
      line_buf[line_pos] = '\0';
      if (strncmp(line_buf, "$DFHPD,", 7) == 0 && line_pos > 7) {
        char statusChar = line_buf[7];
        if (statusChar == '1') {
          radarOccupied = true;
        } else if (statusChar == '0') {
          radarOccupied = false;
        }
      }
      line_pos = 0;
    } else if (c != '\r') {
      if (line_pos < LINE_BUF_LEN - 1) {
        line_buf[line_pos++] = c;
      } else {
        // Overflow guard: discard the malformed sentence and reset
        line_pos = 0;
      }
    }
  }

  // 2. TIMED SENSING LOOP (200ms)
  unsigned long now = millis();
  if (now - lastSampleTime >= SAMPLE_INTERVAL_MS) {
    lastSampleTime = now;

    int fusedState = 0;

    if (radarOccupied) {
      fusedState = 1;
      lastHoldTimer = now; // Reset motion hold timer
    } else {
      // Keep state occupied during hold/cooldown period
      if (now - lastHoldTimer < RADAR_COOLDOWN_MS) {
        fusedState = 1;
      }
    }

    // D. Transition and MQTT Occupancy Publish
    if (fusedState != lastFusedState) {
      Serial.printf("[Occupancy] State transitioned from %d to %d\n",
                    lastFusedState, fusedState);

      // MQTT Publish
      mqttPublishOccupancy(fusedState);

      lastFusedState = fusedState;
    }

    // E. Lighting Control based on gMotionSensorEnabled
    if (gMotionSensorEnabled) {
      // Cooldown guard: Only override light if 60 seconds have elapsed since
      // last external control
      if (millis() - gLastExternalControlTime >= 60000) {
        if (fusedState == 1) {
          // Room occupied -> force light ON if it was OFF
          if (!gLightOn) {
            gLightOn = true;
            gNeedsUpdate = true;
            Serial.println("[Occupancy] Room occupied -> Turning Light ON");
          }
        } else {
          // Room vacant -> force light OFF if it was ON
          if (gLightOn) {
            gLightOn = false;
            gNeedsUpdate = true;
            Serial.println("[Occupancy] Room vacant -> Turning Light OFF");
          }
        }
      }
    }
  }
}
