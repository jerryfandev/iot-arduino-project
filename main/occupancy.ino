#include <Arduino.h>

// ============================================================
// GLOBAL STATE FOR OCCUPANCY (Motion + Sound, Independent)
// ============================================================

// --- Shared Hold Duration ---
#define OCCUPANCY_HOLD_MS 60000 // 60 seconds minimum hold for either trigger

// --- mmWave Radar Config ---
#define RADAR_RX_PIN 16
#define RADAR_TX_PIN 17

// --- Sound Detection Config (from commit c175055) ---
#define VOICE_THRESHOLD_16BIT                                                  \
  10000 // Minimum amplitude to trigger sound detection
#define MIC_STARTUP_GRACE_MS                                                   \
  2000 // Ignore mic data for 2s after boot (I2S noise)

// --- Sampling ---
static unsigned long lastSampleTime = 0;
static const unsigned long SAMPLE_INTERVAL_MS = 200;

// --- Radar State ---
static bool radarOccupied = false;

// --- Independent Hold Timers ---
// Each detector has its own timer. When it triggers, its timer resets.
// The light stays on as long as EITHER timer is still within OCCUPANCY_HOLD_MS.
static unsigned long motionHoldTimer = 0;
static bool motionHoldActive = false;

static unsigned long soundHoldTimer = 0;
static bool soundHoldActive = false;

// --- UART parsing buffers ---
#define LINE_BUF_LEN 64
static char line_buf[LINE_BUF_LEN];
static int line_pos = 0;

// --- Audio energy accumulator (fed from commands.ino onSrAudio) ---
static volatile int64_t audioSampleSum = 0;
static volatile int32_t audioSampleCount = 0;

static unsigned long audioFirstCallTime = 0; // Set on first audio callback
static bool audioFirstCallDone = false;

static int lastFusedState = -1; // Track transitions

extern bool gLightOn;
extern bool gNeedsUpdate;
extern bool gMotionSensorEnabled;
extern unsigned long gLastExternalControlTime;
void mqttPublishOccupancy(int occupied); // Defined in mqtt.ino

// ============================================================
// AUDIO PROCESSING (called from commands.ino -> onSrAudio)
// ============================================================
void occupancyProcessAudio(const int16_t *samples, size_t sample_count,
                           uint8_t channels) {
  if (!samples || channels == 0)
    return;

  // Record timestamp on very first audio callback (I2S just started)
  if (!audioFirstCallDone) {
    audioFirstCallTime = millis();
    audioFirstCallDone = true;
  }

  // Discard audio during startup grace period (INMP441 outputs noise on init)
  if (millis() - audioFirstCallTime < MIC_STARTUP_GRACE_MS)
    return;

  size_t frames = sample_count / channels;
  int64_t sum = 0;
  for (size_t i = 0; i < frames; i++) {
    sum += abs(samples[i * channels]);
  }

  // Accumulate samples safely (read by occupancyLoop every 200ms)
  audioSampleSum += sum;
  audioSampleCount += frames;
}

// ============================================================
// INITIALIZATION
// ============================================================
void occupancySetup() {
  // Initialize Serial2 for mmWave Radar
  Serial2.begin(9600, SERIAL_8N1, RADAR_RX_PIN, RADAR_TX_PIN);
  Serial.println("[Occupancy] Initialized mmWave Radar + Sound Detection");
  lastSampleTime = millis();

  // Ensure both hold timers are expired on boot
  motionHoldTimer = millis() - OCCUPANCY_HOLD_MS;
  motionHoldActive = false;
  soundHoldTimer = millis() - OCCUPANCY_HOLD_MS;
  soundHoldActive = false;

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

    // When motion sensor is DISABLED, clear all holds and skip detection
    if (!gMotionSensorEnabled) {
      // Drain any accumulated audio data
      audioSampleSum = 0;
      audioSampleCount = 0;

      // If holds were active, expire them and publish transition
      if (motionHoldActive || soundHoldActive) {
        motionHoldActive = false;
        soundHoldActive = false;
        if (lastFusedState != 0) {
          Serial.println("[Occupancy] Sensor DISABLED -> clearing holds");
          lastFusedState = 0;
          mqttPublishOccupancy(0);
        }
      }
      return; // Skip all detection logic
    }

    // ── A. MOTION DETECTOR (independent) ──────────────────────
    if (radarOccupied) {
      if (!motionHoldActive) {
        Serial.println("[Occupancy] Motion DETECTED -> hold 60s");
      }
      motionHoldTimer = now;
      motionHoldActive = true;
    } else if (motionHoldActive &&
               (now - motionHoldTimer >= OCCUPANCY_HOLD_MS)) {
      Serial.println("[Occupancy] Motion hold EXPIRED");
      motionHoldActive = false;
    }

    // ── B. SOUND DETECTOR (independent) ───────────────────────
    // Retrieve and reset audio statistics accumulated since last tick
    int32_t avgAmplitude = 0;
    int64_t localSum = audioSampleSum;
    int32_t localCount = audioSampleCount;
    audioSampleSum = 0;
    audioSampleCount = 0;

    if (localCount > 0) {
      avgAmplitude = (int32_t)(localSum / localCount);
    }

    bool soundTriggered = (avgAmplitude > VOICE_THRESHOLD_16BIT);

    if (soundTriggered) {
      if (!soundHoldActive) {
        Serial.printf("[Occupancy] Sound DETECTED (amp=%d) -> hold 60s\n",
                      avgAmplitude);
      } else {
        Serial.printf("[Occupancy] Sound active (amp=%d)\n", avgAmplitude);
      }
      soundHoldTimer = now;
      soundHoldActive = true;
    } else if (soundHoldActive && (now - soundHoldTimer >= OCCUPANCY_HOLD_MS)) {
      Serial.println("[Occupancy] Sound hold EXPIRED");
      soundHoldActive = false;
    }

    // ── C. FUSED STATE: OR of both independent holds ──────────
    // Room is occupied if EITHER detector's hold is still active.
    int fusedState = (motionHoldActive || soundHoldActive) ? 1 : 0;

    // ── D. Transition and MQTT Occupancy Publish ──────────────
    if (fusedState != lastFusedState) {
      Serial.printf("[Occupancy] State transitioned from %d to %d "
                    "(motion=%s, sound=%s)\n",
                    lastFusedState, fusedState,
                    motionHoldActive ? "ACTIVE" : "idle",
                    soundHoldActive ? "ACTIVE" : "idle");

      // MQTT Publish
      mqttPublishOccupancy(fusedState);

      lastFusedState = fusedState;
    }

    // ── E. Lighting Control ───────────────────────────────────
    // Cooldown guard: Only override light if 60 seconds have elapsed since
    // last external control (MQTT/voice command)
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
