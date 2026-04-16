/**
 * INMP441 Mic Check — ESP32-S3
 * ============================
 * Uses New I2S Driver (i2s_std) — NO conflict with speaker.
 *
 * Connections:
 *   SD  → GPIO 10  |  WS  → GPIO 11  |  SCK → GPIO 12  |  L/R → GND
 *
 * Serial Monitor: 115200 baud
 * Expected Results:
 *   - When silent: low amp (< 5000)
 *   - When speaking: amp increases (> 50000 = normal speech, > 100000 = loud
 * speech)
 *   - VU bar extends when sound is detected
 */

#include <driver/i2s_std.h>
#include <math.h>
#include <mbedtls/base64.h>

// ── Pins ──────────────────────────────
#define MIC_WS_PIN 11
#define MIC_SCK_PIN 12
#define MIC_SD_PIN 10

// ── Config ────────────────────────────
#define SAMPLE_RATE 16000
#define BUFFER_SAMPLES 512
#define VU_WIDTH 40 // VU bar width
#define PRINT_MS 80 // Print to Serial every 80ms
// Only print when "voice" exceeds this threshold to avoid spam
#define PRINT_THRESHOLD 800000
// If still above threshold, only reprint after this interval (ms)
#define ABOVE_COOLDOWN_MS 300

// Record WAV (trigger by sending 'r' over Serial)
#define REC_SECONDS 3
#define WAV_BITS_PER_SAMPLE 24
#define WAV_CHANNELS 1

// ── Globals ───────────────────────────
static i2s_chan_handle_t gMicHandle = nullptr;
static int32_t gBuf[BUFFER_SAMPLES];
static unsigned long gLastPrint = 0;
static bool gWasAbove = false;

static void writeWavHeader(uint8_t *hdr, uint32_t dataBytes) {
  // PCM WAV header (44 bytes), little-endian
  const uint32_t sampleRate = SAMPLE_RATE;
  const uint16_t channels = WAV_CHANNELS;
  const uint16_t bitsPerSample = WAV_BITS_PER_SAMPLE;
  const uint32_t byteRate = sampleRate * channels * (bitsPerSample / 8);
  const uint16_t blockAlign = channels * (bitsPerSample / 8);
  const uint32_t riffSize = 36 + dataBytes;

  memcpy(hdr + 0, "RIFF", 4);
  hdr[4] = (uint8_t)(riffSize & 0xFF);
  hdr[5] = (uint8_t)((riffSize >> 8) & 0xFF);
  hdr[6] = (uint8_t)((riffSize >> 16) & 0xFF);
  hdr[7] = (uint8_t)((riffSize >> 24) & 0xFF);
  memcpy(hdr + 8, "WAVE", 4);
  memcpy(hdr + 12, "fmt ", 4);
  hdr[16] = 16;
  hdr[17] = 0;
  hdr[18] = 0;
  hdr[19] = 0; // fmt chunk size
  hdr[20] = 1;
  hdr[21] = 0; // PCM format
  hdr[22] = (uint8_t)(channels & 0xFF);
  hdr[23] = (uint8_t)((channels >> 8) & 0xFF);
  hdr[24] = (uint8_t)(sampleRate & 0xFF);
  hdr[25] = (uint8_t)((sampleRate >> 8) & 0xFF);
  hdr[26] = (uint8_t)((sampleRate >> 16) & 0xFF);
  hdr[27] = (uint8_t)((sampleRate >> 24) & 0xFF);
  hdr[28] = (uint8_t)(byteRate & 0xFF);
  hdr[29] = (uint8_t)((byteRate >> 8) & 0xFF);
  hdr[30] = (uint8_t)((byteRate >> 16) & 0xFF);
  hdr[31] = (uint8_t)((byteRate >> 24) & 0xFF);
  hdr[32] = (uint8_t)(blockAlign & 0xFF);
  hdr[33] = (uint8_t)((blockAlign >> 8) & 0xFF);
  hdr[34] = (uint8_t)(bitsPerSample & 0xFF);
  hdr[35] = (uint8_t)((bitsPerSample >> 8) & 0xFF);
  memcpy(hdr + 36, "data", 4);
  hdr[40] = (uint8_t)(dataBytes & 0xFF);
  hdr[41] = (uint8_t)((dataBytes >> 8) & 0xFF);
  hdr[42] = (uint8_t)((dataBytes >> 16) & 0xFF);
  hdr[43] = (uint8_t)((dataBytes >> 24) & 0xFF);
}

static void dumpWavBase64(const int16_t *pcm, uint32_t samples) {
  const uint32_t dataBytes = samples * sizeof(int16_t);
  uint8_t wavHdr[44];
  writeWavHeader(wavHdr, dataBytes);

  const size_t totalBytes = sizeof(wavHdr) + dataBytes;
  const size_t b64Cap = ((totalBytes + 2) / 3) * 4 + 1;
  uint8_t *b64 = (uint8_t *)malloc(b64Cap);
  if (!b64) {
    Serial.println("[REC] OOM (base64 buffer)");
    return;
  }

  uint8_t *blob = (uint8_t *)malloc(totalBytes);
  if (!blob) {
    free(b64);
    Serial.println("[REC] OOM (wav blob)");
    return;
  }
  memcpy(blob, wavHdr, sizeof(wavHdr));
  memcpy(blob + sizeof(wavHdr), (const uint8_t *)pcm, dataBytes);

  size_t outLen = 0;
  int rc = mbedtls_base64_encode(b64, b64Cap, &outLen, blob, totalBytes);
  free(blob);
  if (rc != 0) {
    free(b64);
    Serial.printf("[REC] base64 encode failed: %d\n", rc);
    return;
  }

  Serial.println("-----BEGIN_WAV_BASE64-----");
  // Print in manageable lines for copy/paste
  const size_t LINE = 76;
  for (size_t i = 0; i < outLen; i += LINE) {
    size_t n = (outLen - i > LINE) ? LINE : (outLen - i);
    Serial.write(b64 + i, n);
    Serial.println();
  }
  Serial.println("-----END_WAV_BASE64-----");
  free(b64);
}

static void recordAndDumpWav() {
  const uint32_t samplesTarget = REC_SECONDS * SAMPLE_RATE;
  int16_t *pcm = (int16_t *)malloc(samplesTarget * sizeof(int16_t));
  if (!pcm) {
    Serial.println("[REC] OOM (pcm buffer)");
    return;
  }

  Serial.printf("[REC] Recording %ds... (say something)\n", REC_SECONDS);
  uint32_t written = 0;
  while (written < samplesTarget) {
    size_t bytesRead = 0;
    esp_err_t r =
        i2s_channel_read(gMicHandle, gBuf, BUFFER_SAMPLES * sizeof(int32_t),
                         &bytesRead, pdMS_TO_TICKS(200));
    if (r != ESP_OK || bytesRead == 0)
      continue;
    int n = (int)(bytesRead / sizeof(int32_t));
    for (int i = 0; i < n && written < samplesTarget; i++) {
      // 24-bit left-justified; >>16 gives signed 16-bit PCM
      pcm[written++] = (int16_t)(gBuf[i] >> 16);
    }
    yield();
  }
  Serial.println("[REC] Done. Encoding WAV to Base64...");
  dumpWavBase64(pcm, samplesTarget);
  free(pcm);
  Serial.println("[REC] Tip: copy the base64 block and decode to a .wav on "
                 "your computer.");
}

// ─────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n==============================");
  Serial.println("  INMP441 Mic Check — ESP32-S3");
  Serial.println("==============================");
  Serial.println("Pins: SD=10 | WS=11 | SCK=12 | L/R=GND");
  Serial.println("Sample Rate: 16000 Hz | 32-bit | Mono");
  Serial.println("------------------------------");
  Serial.println("Send 'r' to record a WAV and dump Base64.");

  // Initialize new I2S driver
  i2s_chan_config_t chanCfg =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chanCfg.auto_clear = true;

  esp_err_t err = i2s_new_channel(&chanCfg, NULL, &gMicHandle);
  if (err != ESP_OK) {
    Serial.printf("[ERROR] i2s_new_channel: 0x%x\n", err);
    while (true)
      delay(1000);
  }

  i2s_std_config_t stdCfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
      .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                                  I2S_SLOT_MODE_MONO),
      .gpio_cfg = {.mclk = I2S_GPIO_UNUSED,
                   .bclk = (gpio_num_t)MIC_SCK_PIN,
                   .ws = (gpio_num_t)MIC_WS_PIN,
                   .dout = I2S_GPIO_UNUSED,
                   .din = (gpio_num_t)MIC_SD_PIN,
                   .invert_flags = {false, false, false}}};

  err = i2s_channel_init_std_mode(gMicHandle, &stdCfg);
  if (err != ESP_OK) {
    Serial.printf("[ERROR] i2s_channel_init_std_mode: 0x%x\n", err);
    while (true)
      delay(1000);
  }

  err = i2s_channel_enable(gMicHandle);
  if (err != ESP_OK) {
    Serial.printf("[ERROR] i2s_channel_enable: 0x%x\n", err);
    while (true)
      delay(1000);
  }

  Serial.println("[OK] I2S ready! Speak into the mic to check...\n");
}

// ─────────────────────────────────────
void loop() {
  if (Serial.available()) {
    int c = Serial.read();
    if (c == 'r' || c == 'R') {
      recordAndDumpWav();
    }
  }

  size_t bytesRead = 0;
  esp_err_t r =
      i2s_channel_read(gMicHandle, gBuf, BUFFER_SAMPLES * sizeof(int32_t),
                       &bytesRead, pdMS_TO_TICKS(100));

  if (r != ESP_OK || bytesRead == 0)
    return;

  int samplesRead = bytesRead / sizeof(int32_t);

  // Calculate average amplitude and peak
  int64_t sum = 0;
  int32_t peak = 0;
  for (int i = 0; i < samplesRead; i++) {
    // INMP441: 24-bit left-justified in 32-bit word
    int32_t s = gBuf[i] >> 8; // Get significant 24 bits
    int32_t absS = abs(s);
    sum += absS;
    if (absS > peak)
      peak = absS;
  }
  int32_t avg = (int32_t)(sum / samplesRead);

  unsigned long now = millis();
  bool above = (avg > PRINT_THRESHOLD);
  if (!above) {
    gWasAbove = false;
    return;
  }
  // Only print when just above threshold, or after cooldown if still above
  // threshold
  if (gWasAbove && (now - gLastPrint < ABOVE_COOLDOWN_MS))
    return;
  if (!gWasAbove && (now - gLastPrint < PRINT_MS))
    return;
  gLastPrint = now;
  gWasAbove = true;

  // Classify sound level
  const char *label;
  if (avg > 200000)
    label = ">>> LOUD! <<<";
  else if (avg > 80000)
    label = "[Voice]      ";
  else if (avg > 30000)
    label = "[Soft voice] ";
  else if (avg > 5000)
    label = "[Noise]      ";
  else
    label = "[Silent]     ";

  // VU bar (log scale)
  float logAvg = (avg > 0) ? log10f((float)avg) : 0;
  float logMax = log10f(8388608.0f); // 2^23
  int barLen = (int)(logAvg / logMax * VU_WIDTH);
  if (barLen < 0)
    barLen = 0;
  if (barLen > VU_WIDTH)
    barLen = VU_WIDTH;

  // Select bar character based on level
  char barChar = '-';
  if (avg > 200000)
    barChar = '#';
  else if (avg > 80000)
    barChar = '=';
  else if (avg > 30000)
    barChar = '-';
  else
    barChar = '.';

  Serial.print(label);
  Serial.print(" |");
  for (int i = 0; i < VU_WIDTH; i++) {
    Serial.print(i < barLen ? barChar : ' ');
  }
  Serial.print("| avg=");
  Serial.print(avg);
  Serial.print(" peak=");
  Serial.println(peak);
}
