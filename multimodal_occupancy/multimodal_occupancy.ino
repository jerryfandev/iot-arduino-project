/**
 * @file multimodal_occupancy.ino
 * @brief Multimodal Occupancy Detection (mmWave Radar + I2S Digital Mic)
 * 
 * Hardware Target: ESP32-S3
 * 
 * Pin Configuration:
 *   - mmWave Radar (Serial2): RX = 16, TX = 17
 *   - INMP441 Mic (I2S): WS = 11, SCK = 12, SD = 10, L/R = GND
 * 
 * Output: CSV telemetry formatted for direct plotting over Serial Monitor.
 */

#include <Arduino.h>
#include <driver/i2s.h>

// ============================================================
// I2S MICROPHONE CONFIGURATION (INMP441)
// ============================================================
#define I2S_WS_PIN   11   // Word Select (LRCK)
#define I2S_SCK_PIN  12   // Bit Clock (BCLK)
#define I2S_SD_PIN   10   // Serial Data (DOUT)
#define I2S_PORT         I2S_NUM_0
#define I2S_SAMPLE_RATE  16000
#define I2S_BUFFER_SIZE  512

#define VOICE_THRESHOLD  200000 // High amplitude threshold for loud noise (study room setting)
#define MIC_COOLDOWN_MS  30000  // 30 seconds hold time for audio trigger

// ============================================================
// RADAR CONFIGURATION (DFRobot C4001)
// ============================================================
#define RADAR_RX_PIN 16
#define RADAR_TX_PIN 17

// ============================================================
// GLOBAL STATE
// ============================================================
unsigned long lastSampleTime = 0;
const unsigned long SAMPLE_INTERVAL_MS = 200;

bool radarOccupied = false;
unsigned long lastHoldTimer = 0; // Renamed from lastMicTriggerTime

// History Buffers (10 seconds = 50 ticks at 200ms)
#define HISTORY_SIZE_10S 50
int radarHistory[HISTORY_SIZE_10S] = {0};
int micHistory[HISTORY_SIZE_10S] = {0};
int historyIdx = 0;

// UART parsing buffers
#define LINE_BUF_LEN 64
char line_buf[LINE_BUF_LEN];
int line_pos = 0;

int32_t i2sBuffer[I2S_BUFFER_SIZE];

// ============================================================
// HISTORY HELPER FUNCTIONS
// ============================================================
int sumHistory(int* historyArray, int ticks) {
  int sum = 0;
  for (int i = 0; i < ticks; i++) {
    int idx = (historyIdx - 1 - i + HISTORY_SIZE_10S) % HISTORY_SIZE_10S;
    sum += historyArray[idx];
  }
  return sum;
}

// ============================================================
// INITIALIZATION
// ============================================================
void i2s_init() {
  i2s_config_t i2sConfig = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = I2S_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pinConfig = {
    .bck_io_num = I2S_SCK_PIN,
    .ws_io_num = I2S_WS_PIN,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_SD_PIN
  };

  i2s_driver_install(I2S_PORT, &i2sConfig, 0, NULL);
  i2s_set_pin(I2S_PORT, &pinConfig);
  i2s_zero_dma_buffer(I2S_PORT);
}

void setup() {
  // Main Serial for CSV Output
  Serial.begin(115200);
  while (!Serial) delay(10);

  // Serial2 for mmWave Radar
  Serial2.begin(9600, SERIAL_8N1, RADAR_RX_PIN, RADAR_TX_PIN);

  // Initialize Microphone
  i2s_init();

  delay(500); // Settle
  
  // Print CSV Header
  Serial.println("Radar_State,Mic_Trigger,Fused_System_State");
  lastSampleTime = millis();
  
  // Ensure the cooldown is expired on boot
  lastHoldTimer = millis() - MIC_COOLDOWN_MS;
}

// ============================================================
// MAIN LOOP
// ============================================================
void loop() {
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

    // A. Read Microphone Audio Chunk
    size_t bytesRead = 0;
    i2s_read(I2S_PORT, (void*)i2sBuffer, sizeof(int32_t) * I2S_BUFFER_SIZE, &bytesRead, 0); // Non-blocking read (timeout 0)
    int samplesRead = bytesRead / sizeof(int32_t);
    
    int32_t maxVal = 0;
    int32_t minVal = 0;
    int64_t sum = 0;

    for (int i = 0; i < samplesRead; i++) {
      int32_t sample = i2sBuffer[i] >> 8; // 24-bit INMP441 shift
      sum += abs(sample);
      if (sample > maxVal) maxVal = sample;
      if (sample < minVal) minVal = sample;
    }

    int32_t avgAmplitude = (samplesRead > 0) ? (int32_t)(sum / samplesRead) : 0;
    
    // B. Evaluate Microphone Trigger
    bool rawMicTrigger = (avgAmplitude > VOICE_THRESHOLD);
    
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
    // "person might focus on their work while stay silent"
    if (radarSum3s >= 10) {
      fusedState = 1;
      lastHoldTimer = now; // Verified human, reset 30s cooldown
    }
    // Rule C: Radar Spike Validation 
    // "only trigger when it is 0 for a long time but suddenly become 1"
    // We check if the radar was mostly quiet over the last 10s (sum < 10), but spiked to 1 right now.
    else if (radarOccupied && radarSum10s < 10) {
      if (micSum10s >= 1) { // Any noise in the last 10 seconds?
        fusedState = 1;
        lastHoldTimer = now; // Spike validated, reset 30s cooldown
      }
    }
    // Rule A: Continuous Noise Override
    // Mic is 1 for EVERY time point in the last 3 secs (15 out of 15)
    else if (micSum3s == 15) {
      fusedState = 1;
      // Does NOT reset the 30s hold timer! Drops to 0 immediately if noise discontinues.
    }
    
    // Anti-False-Negative Cooldown (Hold Timer)
    // If the rules above evaluated to 0, but the 30s hold timer is still active, remain OCCUPIED.
    if (fusedState == 0) {
      if (now - lastHoldTimer < MIC_COOLDOWN_MS) {
        fusedState = 1;
      }
    }

    // D. Output CSV Telemetry
    Serial.printf("%d,%d,%d\n", radarOccupied ? 1 : 0, rawMicTrigger ? 1 : 0, fusedState);
  }
}
