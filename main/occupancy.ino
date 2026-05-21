#include <Arduino.h>

// ============================================================
// GLOBAL STATE FOR OCCUPANCY (Motion + Sound, Single Timer)
// ============================================================

// --- Shared Timeout Duration ---
#define OCCUPANCY_TIMEOUT_MS 60000UL // 60 seconds without detection turns off light

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
static bool radarFrameReceived = false;
static bool lastRadarOccupied = false;

// --- Occupancy Countdown Timer ---
// A single non-blocking timer starts when motion OR sound turns the light on.
// Any later motion OR sound detection resets the full 60-second countdown.
static unsigned long occupancyTimerStart = 0;
static bool occupancyTimerActive = false;
static bool frontendSensorWasEnabled = false;
static bool motionInputArmed = false;
static bool lastLightOn = false;
static unsigned long lastExternalControlTimeSeen = 0;

// --- UART parsing buffers ---
#define LINE_BUF_LEN 64
static char line_buf[LINE_BUF_LEN];
static int line_pos = 0;

// --- Audio energy accumulator (fed from commands.ino onSrAudio) ---
static volatile int64_t audioSampleSum = 0;
static volatile int32_t audioSampleCount = 0;
static bool soundWasAboveThreshold = false;

static unsigned long audioFirstCallTime = 0; // Set on first audio callback
static bool audioFirstCallDone = false;

static int lastFusedState = -1; // Track transitions

extern bool gLightOn;
extern bool gNeedsUpdate;
extern bool gMotionSensorEnabled;
extern unsigned long gLastExternalControlTime;
void mqttPublishOccupancy(int occupied); // Defined in mqtt.ino

static void resetOccupancyTimer() {
  occupancyTimerStart = 0;
  occupancyTimerActive = false;
}

static void drainSensorInputs() {
  while (Serial2.available() > 0) {
    Serial2.read();
  }

  line_pos = 0;
  radarOccupied = false;
  radarFrameReceived = false;
  lastRadarOccupied = false;
  audioSampleSum = 0;
  audioSampleCount = 0;
  soundWasAboveThreshold = false;
}

static void publishOccupancyIfChanged(int occupied) {
  if (occupied == lastFusedState)
    return;

  mqttPublishOccupancy(occupied);
  lastFusedState = occupied;
}

static void startOccupancyCountdown(unsigned long now) {
  occupancyTimerStart = now;
  occupancyTimerActive = true;
}

static void logDetection(bool timerWasActive, bool motionTriggered,
                         bool soundTriggered, int32_t avgAmplitude) {
  const char *action =
      timerWasActive ? "countdown reset" : "light ON, countdown 60s";

  if (soundTriggered) {
    Serial.printf("[Occupancy] Detection -> %s (motion=%s, sound=yes, "
                  "amp=%d)\n",
                  action, motionTriggered ? "yes" : "no", avgAmplitude);
  } else {
    Serial.printf("[Occupancy] Detection -> %s (motion=%s, sound=no)\n",
                  action, motionTriggered ? "yes" : "no");
  }
}

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

  // Ensure the countdown timer is reset on boot
  resetOccupancyTimer();
  radarOccupied = false;
  radarFrameReceived = false;
  lastRadarOccupied = false;
  soundWasAboveThreshold = false;
  lastLightOn = gLightOn;
  lastExternalControlTimeSeen = gLastExternalControlTime;

  lastFusedState = 0;
}

// ============================================================
// MAIN LOOP
// ============================================================
void occupancyLoop() {
  unsigned long now = millis();

  // Frontend OFF: immediately reset timers and ignore sensor inputs.
  // The current light state is intentionally left unchanged.
  if (!gMotionSensorEnabled) {
    if (occupancyTimerActive || lastFusedState != 0) {
      Serial.println(
          "[Occupancy] Frontend motion sensor OFF -> reset countdown");
    }

    resetOccupancyTimer();
    drainSensorInputs();
    frontendSensorWasEnabled = false;
    motionInputArmed = false;
    lastLightOn = gLightOn;
    lastExternalControlTimeSeen = gLastExternalControlTime;
    publishOccupancyIfChanged(0);
    return;
  }

  if (!frontendSensorWasEnabled) {
    frontendSensorWasEnabled = true;
    resetOccupancyTimer();
    drainSensorInputs();
    lastSampleTime = now;
    motionInputArmed = false;
    lastLightOn = gLightOn;
    lastExternalControlTimeSeen = gLastExternalControlTime;
    publishOccupancyIfChanged(0);
    Serial.println("[Occupancy] Frontend motion sensor ON -> standby");
    return;
  }

  bool lightTurnedOn = !lastLightOn && gLightOn;
  bool externalControlChanged =
      (gLastExternalControlTime != lastExternalControlTimeSeen);
  if (externalControlChanged) {
    lastExternalControlTimeSeen = gLastExternalControlTime;
  }

  if (gLightOn && (lightTurnedOn || externalControlChanged)) {
    startOccupancyCountdown(now);
    publishOccupancyIfChanged(1);
    Serial.println(
        "[Occupancy] External Light ON -> countdown 60s");
  }
  lastLightOn = gLightOn;

  // 1. NON-BLOCKING RADAR PARSING
  while (Serial2.available() > 0) {
    char c = (char)Serial2.read();
    if (c == '\n') {
      line_buf[line_pos] = '\0';
      if (strncmp(line_buf, "$DFHPD,", 7) == 0 && line_pos > 7) {
        char statusChar = line_buf[7];
        if (statusChar == '1') {
          radarOccupied = true;
          radarFrameReceived = true;
        } else if (statusChar == '0') {
          radarOccupied = false;
          radarFrameReceived = true;
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
  if (now - lastSampleTime >= SAMPLE_INTERVAL_MS) {
    lastSampleTime = now;

    // A. MOTION DETECTOR
    bool motionTriggered = false;
    if (!motionInputArmed) {
      if (radarFrameReceived) {
        motionInputArmed = true;
        lastRadarOccupied = false;
        Serial.println("[Occupancy] Motion input armed after first radar frame");
      }
    } else {
      motionTriggered = radarOccupied && !lastRadarOccupied;
      lastRadarOccupied = radarOccupied;
    }

    // B. SOUND DETECTOR
    // Retrieve and reset audio statistics accumulated since last tick
    int32_t avgAmplitude = 0;
    int64_t localSum = audioSampleSum;
    int32_t localCount = audioSampleCount;
    audioSampleSum = 0;
    audioSampleCount = 0;

    if (localCount > 0) {
      avgAmplitude = (int32_t)(localSum / localCount);
    }

    bool soundAboveThreshold = (avgAmplitude > VOICE_THRESHOLD_16BIT);
    bool soundTriggered = soundAboveThreshold && !soundWasAboveThreshold;
    soundWasAboveThreshold = soundAboveThreshold;
    bool anySensorTriggered = motionTriggered || soundTriggered;

    // C. STATE MACHINE
    if (anySensorTriggered) {
      bool wasTimerActive = occupancyTimerActive;
      startOccupancyCountdown(now);

      logDetection(wasTimerActive, motionTriggered, soundTriggered,
                   avgAmplitude);

      if (!gLightOn) {
        gLightOn = true;
        gNeedsUpdate = true;
        lastLightOn = true;
        Serial.println("[Occupancy] Turning Light ON");
      }

      publishOccupancyIfChanged(1);
      return;
    }

    if (occupancyTimerActive &&
        (now - occupancyTimerStart >= OCCUPANCY_TIMEOUT_MS)) {
      resetOccupancyTimer();
      publishOccupancyIfChanged(0);

      if (gLightOn) {
        gLightOn = false;
        gNeedsUpdate = true;
        lastLightOn = false;
        Serial.println(
            "[Occupancy] No detection for 60s -> Turning Light OFF");
      }
    }
  }
}
