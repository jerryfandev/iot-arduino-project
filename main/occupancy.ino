#include <Arduino.h>

// ============================================================
// GLOBAL STATE FOR OCCUPANCY
// ============================================================
#define VOICE_THRESHOLD_16BIT 780   // Scaled mathematically from 200000 in 24-bit
#define MIC_COOLDOWN_MS       30000 // 30 seconds hold time for audio trigger

// mmWave Radar Pins
#define RADAR_RX_PIN 16
#define RADAR_TX_PIN 17

static unsigned long lastSampleTime = 0;
static const unsigned long SAMPLE_INTERVAL_MS = 200;

static bool radarOccupied = false;
static unsigned long lastHoldTimer = 0;

// History Buffers (10 seconds = 50 ticks at 200ms)
#define HISTORY_SIZE_10S 50
static int radarHistory[HISTORY_SIZE_10S] = {0};
static int micHistory[HISTORY_SIZE_10S] = {0};
static int historyIdx = 0;

// UART parsing buffers
#define LINE_BUF_LEN 64
static char line_buf[LINE_BUF_LEN];
static int line_pos = 0;

// Audio energy accumulator fed from commands.ino's onSrAudio
static volatile int64_t audioSampleSum = 0;
static volatile int32_t audioSampleCount = 0;

static int lastFusedState = -1; // Track transitions

extern bool gLightOn;
extern bool gNeedsUpdate;
void mqttPublishOccupancy(int occupied); // Defined in mqtt.ino

// ============================================================
// HISTORY HELPER FUNCTIONS
// ============================================================
static int sumHistory(int* historyArray, int ticks) {
  int sum = 0;
  for (int i = 0; i < ticks; i++) {
    int idx = (historyIdx - 1 - i + HISTORY_SIZE_10S) % HISTORY_SIZE_10S;
    sum += historyArray[idx];
  }
  return sum;
}

// Called by onSrAudio in commands.ino
void occupancyProcessAudio(const int16_t *samples, size_t sample_count, uint8_t channels) {
  if (!samples || channels == 0) return;

  size_t frames = sample_count / channels;
  int64_t sum = 0;
  for (size_t i = 0; i < frames; i++) {
    sum += abs(samples[i * channels]);
  }

  // Accrete samples safely
  audioSampleSum += sum;
  audioSampleCount += frames;
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
  lastHoldTimer = millis() - MIC_COOLDOWN_MS;
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

    // A. Retrieve and Reset Audio Statistics
    int32_t avgAmplitude = 0;
    int64_t localSum = audioSampleSum;
    int32_t localCount = audioSampleCount;
    audioSampleSum = 0;
    audioSampleCount = 0;

    if (localCount > 0) {
      avgAmplitude = (int32_t)(localSum / localCount);
    }

    // B. Evaluate Microphone Trigger
    bool rawMicTrigger = (avgAmplitude > VOICE_THRESHOLD_16BIT);

    // Update Sliding Windows
    radarHistory[historyIdx] = radarOccupied ? 1 : 0;
    micHistory[historyIdx] = rawMicTrigger ? 1 : 0;
    historyIdx = (historyIdx + 1) % HISTORY_SIZE_10S;

    // C. Evaluate Fused Logic (Temporal Sliding Window State Machine)
    int radarSum3s  = sumHistory(radarHistory, 15); // Radar active in last 3s
    int radarSum10s = sumHistory(radarHistory, 50); // Radar active in last 10s
    int micSum3s    = sumHistory(micHistory, 15);   // Mic active in last 3s
    int micSum10s   = sumHistory(micHistory, 50);   // Mic active in last 10s

    int fusedState = 0;

    // Rule B: Solid Radar Presence (e.g. 10 out of 15 ticks = 2s out of 3s)
    if (radarSum3s >= 10) {
      fusedState = 1;
      lastHoldTimer = now; // Verified human, reset 30s cooldown
    }
    // Rule C: Radar Spike Validation
    else if (radarOccupied && radarSum10s < 10) {
      if (micSum10s >= 1) { // Any noise in the last 10 seconds?
        fusedState = 1;
        lastHoldTimer = now; // Spike validated, reset 30s cooldown
      }
    }
    // Rule A: Continuous Noise Override
    else if (micSum3s == 15) {
      fusedState = 1;
      // Does NOT reset the 30s hold timer! Drops to 0 immediately if noise discontinues.
    }

    // Anti-False-Negative Cooldown (Hold Timer)
    if (fusedState == 0) {
      if (now - lastHoldTimer < MIC_COOLDOWN_MS) {
        fusedState = 1;
      }
    }

    // D. Transition and Lighting Control Logic
    if (fusedState != lastFusedState) {
      Serial.printf("[Occupancy] State transitioned from %d to %d\n", lastFusedState, fusedState);
      
      // MQTT Publish
      mqttPublishOccupancy(fusedState);

      // Control Light based on transitions
      if (fusedState == 1) {
        // Room became occupied -> turn light ON if it was OFF
        if (!gLightOn) {
          gLightOn = true;
          gNeedsUpdate = true;
          Serial.println("[Occupancy] Room occupied -> Turning Light ON");
        }
      } else {
        // Room became vacant -> turn light OFF if it was ON
        if (gLightOn) {
          gLightOn = false;
          gNeedsUpdate = true;
          Serial.println("[Occupancy] Room vacant -> Turning Light OFF");
        }
      }
      
      lastFusedState = fusedState;
    }

    // Standard format for Direct CSV plotting
    // Serial.printf("%d,%d,%d\n", radarOccupied ? 1 : 0, rawMicTrigger ? 1 : 0, fusedState);
  }
}
