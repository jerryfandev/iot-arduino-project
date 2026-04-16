/**
 * INMP441 I2S Microphone Test - ESP32-S3
 * =======================================
 * Read audio data from INMP441 microphone via I2S and display it on the Serial Monitor.
 * 
 * Connections:
 *   INMP441 VDD  → 3.3V
 *   INMP441 GND  → GND
 *   INMP441 SD   → GPIO 10  (Data)
 *   INMP441 WS   → GPIO 11  (Word Select / LR Clock)
 *   INMP441 SCK  → GPIO 12  (Bit Clock)
 *   INMP441 L/R  → GND      (Left channel)
 * 
 * Features:
 *   - Display amplitude as an ASCII VU meter on Serial
 *   - Display raw PCM values for debugging
 *   - Voice detection with custom thresholds
 */

#include <driver/i2s.h>

// ============================================================
// PIN CONFIGURATION
// ============================================================
#define I2S_WS_PIN   11   // Word Select (LRCK)
#define I2S_SCK_PIN  12   // Bit Clock (BCLK)
#define I2S_SD_PIN   10   // Serial Data (DOUT của mic)

// ============================================================
// I2S CONFIGURATION
// ============================================================
#define I2S_PORT         I2S_NUM_0
#define I2S_SAMPLE_RATE  16000        // 16kHz - standard for speech
#define I2S_SAMPLE_BITS  I2S_BITS_PER_SAMPLE_32BIT  // INMP441 is 24-bit, ESP32 reads 32-bit
#define I2S_CHANNEL_FMT  I2S_CHANNEL_FMT_ONLY_LEFT  // L/R = GND → Left channel
#define I2S_BUFFER_SIZE  1024         // Number of samples per read

// ============================================================
// DISPLAY CONFIGURATION
// ============================================================
#define NOISE_THRESHOLD  5000         // Noise floor threshold (increase if noisy)
#define VOICE_THRESHOLD  50000        // Voice detection threshold
#define VU_BAR_WIDTH     40           // ASCII VU meter width
#define PRINT_RAW_VALUES false        // true = print raw PCM values (debug)
#define PRINT_INTERVAL_MS 50          // Update Serial every 50ms

// ============================================================
// GLOBAL VARIABLES
// ============================================================
int32_t i2sBuffer[I2S_BUFFER_SIZE];
unsigned long lastPrintTime = 0;
bool voiceDetected = false;

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("==========================================");
  Serial.println("  INMP441 Microphone Test - ESP32-S3");
  Serial.println("==========================================");
  Serial.println("Pins: SD=10, WS=11, SCK=12, L/R=GND");
  Serial.println("Sample Rate: 16000 Hz | Bits: 24-bit I2S");
  Serial.println("------------------------------------------");
  Serial.println("Speak into the mic to see amplitude...");
  Serial.println();

  i2s_init();
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  size_t bytesRead = 0;

  // Read I2S data
  esp_err_t result = i2s_read(
    I2S_PORT,
    (void*)i2sBuffer,
    sizeof(int32_t) * I2S_BUFFER_SIZE,
    &bytesRead,
    portMAX_DELAY  // Wait until data is available
  );

  if (result != ESP_OK) {
    Serial.printf("[ERROR] i2s_read failed: %d\n", result);
    delay(500);
    return;
  }

  int samplesRead = bytesRead / sizeof(int32_t);
  if (samplesRead == 0) return;

  // Calculate audio statistics
  int64_t sum = 0;
  int32_t maxVal = 0;
  int32_t minVal = 0;

  for (int i = 0; i < samplesRead; i++) {
    // INMP441 output is 24-bit left-justified in 32-bit word
    // Shift right 8 to get 24-bit value
    int32_t sample = i2sBuffer[i] >> 8;  // Get top 24 bits

    sum += abs(sample);
    if (sample > maxVal) maxVal = sample;
    if (sample < minVal) minVal = sample;

    if (PRINT_RAW_VALUES && i < 5) {
      Serial.printf("  raw[%d] = %ld\n", i, (long)sample);
    }
  }

  int32_t avgAmplitude = (int32_t)(sum / samplesRead);
  int32_t peakToPeak = maxVal - minVal;

  // Print result every PRINT_INTERVAL_MS ms
  unsigned long now = millis();
  if (now - lastPrintTime >= PRINT_INTERVAL_MS) {
    lastPrintTime = now;
    printVUMeter(avgAmplitude, peakToPeak);
  }
}

// ============================================================
// KHỞI TẠO I2S
// ============================================================
void i2s_init() {
  Serial.print("[I2S] Initializing... ");

  i2s_config_t i2sConfig = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = I2S_SAMPLE_RATE,
    .bits_per_sample = I2S_SAMPLE_BITS,
    .channel_format = I2S_CHANNEL_FMT,
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

  esp_err_t err = i2s_driver_install(I2S_PORT, &i2sConfig, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("[FAILED] i2s_driver_install error: %d\n", err);
    while (true) delay(1000);
  }

  err = i2s_set_pin(I2S_PORT, &pinConfig);
  if (err != ESP_OK) {
    Serial.printf("[FAILED] i2s_set_pin error: %d\n", err);
    while (true) delay(1000);
  }

  // Clear I2S buffer to avoid old data/noise floor reading
  i2s_zero_dma_buffer(I2S_PORT);

  Serial.println("OK!");
  Serial.println("[I2S] Starting microphone sequence...\n");
}

// ============================================================
// PRINT VU METER TO SERIAL
// ============================================================
void printVUMeter(int32_t avgAmplitude, int32_t peakToPeak) {
  // Determine if voice is detected
  bool currentVoice = (avgAmplitude > VOICE_THRESHOLD);
  if (currentVoice != voiceDetected) {
    voiceDetected = currentVoice;
    if (voiceDetected) {
      Serial.println("\n>>> 🎤 VOICE DETECTED! <<<");
    } else {
      Serial.println("--- silence ---");
    }
  }

  // Only print VU meter when sound is above noise floor
  if (avgAmplitude < NOISE_THRESHOLD) {
    // Silence - print minimal output
    Serial.print("[Silent] avg=");
    Serial.println(avgAmplitude);
    return;
  }

  // Calculate bar length (logarithmic scale for better visibility)
  float logAmplitude = log10(max((int32_t)1, avgAmplitude));
  float logMax = log10(8388608.0f);  // log10(2^23) = max 24-bit value
  int barLen = (int)(logAmplitude / logMax * VU_BAR_WIDTH);
  barLen = constrain(barLen, 0, VU_BAR_WIDTH);

  // Select character and label based on level
  char barChar = '=';
  const char* label = " [Low]  ";
  if (avgAmplitude > VOICE_THRESHOLD * 3) {
    barChar = '#';
    label = " [HIGH] ";
  } else if (avgAmplitude > VOICE_THRESHOLD) {
    barChar = '*';
    label = " [Voice]";
  }

  // In VU meter
  Serial.print("[VU] |");
  for (int i = 0; i < VU_BAR_WIDTH; i++) {
    Serial.print(i < barLen ? barChar : ' ');
  }
  Serial.print("|");
  Serial.print(label);
  Serial.print(" avg=");
  Serial.print(avgAmplitude);
  Serial.print(" peak=");
  Serial.println(peakToPeak);
}
