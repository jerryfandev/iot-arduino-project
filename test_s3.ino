/**
 * SUCCESS!!!!!
 * INMP441 Recording Test — ESP32-S3
 * ===================================
 * Uses NEW I2S driver (i2s_std) with Philips slot mode — required on ESP32-S3.
 * (The old driver/i2s.h is deprecated and missing on ESP32-S3 IDF.)
 *
 * Wiring:
 *   INMP441 SD  → GPIO 10  |  VDD → 3.3V
 *   INMP441 WS  → GPIO 11  |  GND → GND
 *   INMP441 SCK → GPIO 12  |  L/R → GND
 *
 * Usage:
 *   1. Open Serial Monitor at 921600 baud
 *   2. Type 'r' + Enter to record 5 seconds
 *   3. Copy EVERYTHING between BEGIN and END into audio.txt
 *   4. Run: python -X utf8 decode_wav.py
 *   5. Open normalized_audio.wav and listen
 *
 * WHY 921600 BAUD?
 *   16kHz × 16-bit = ~42 KB/s of Base64 output.
 *   At 115200 baud only ~8.6 KB/s is possible → 5s audio takes ~20s.
 *   At 921600 baud throughput exceeds 42 KB/s → real-time streaming works.
 *
 * WHY i2s_std + PHILIPS?
 *   ESP32-S3 uses the new IDF I2S API (i2s_std). The old i2s.h is not
 *   available. Philips slot mode matches INMP441's I2S protocol exactly;
 *   MSB/Left-Justified mode shifts data by 1 clock causing distortion.
 */

#include <driver/i2s_std.h>
#include <mbedtls/base64.h>

// ── Pins (ESP32-S3) ────────────────────────────────────────────
#define MIC_SD_PIN  10
#define MIC_WS_PIN  11
#define MIC_SCK_PIN 12

// ── Config ─────────────────────────────────────────────────────
#define SAMPLE_RATE  16000
#define REC_SECONDS  5

// ── I2S Handle ─────────────────────────────────────────────────
static i2s_chan_handle_t gMicHandle = nullptr;

// ── Base64 Stream Buffer (divisible by 3 for clean encoding) ───
#define STREAM_BUF_SIZE 3000
static uint8_t g_buf[STREAM_BUF_SIZE];
static int     g_bufIdx  = 0;
static int     g_lineLen = 0;

static void flushB64(int count) {
  if (count == 0) return;
  size_t outLen = 0;
  size_t cap    = ((count + 2) / 3) * 4 + 1;
  unsigned char *out = (unsigned char *)malloc(cap);
  if (!out) return;
  mbedtls_base64_encode(out, cap, &outLen, g_buf, count);
  for (size_t i = 0; i < outLen; i++) {
    Serial.print((char)out[i]);
    if (++g_lineLen >= 76) { Serial.println(); g_lineLen = 0; }
  }
  free(out);
}

static void pushByte(uint8_t b) {
  g_buf[g_bufIdx++] = b;
  if (g_bufIdx == STREAM_BUF_SIZE) {
    flushB64(STREAM_BUF_SIZE);
    g_bufIdx = 0;
  }
}

// ── WAV Header (44 bytes, 16-bit mono PCM) ─────────────────────
static void pushWavHeader(uint32_t dataBytes) {
  const uint32_t byteRate = SAMPLE_RATE * 2;   // 16-bit mono: rate * 1ch * 2bytes
  const uint32_t riffSize = 36 + dataBytes;
  uint8_t h[44] = {
    'R','I','F','F',
    (uint8_t)(riffSize),(uint8_t)(riffSize>>8),(uint8_t)(riffSize>>16),(uint8_t)(riffSize>>24),
    'W','A','V','E',
    'f','m','t',' ',
    16,0,0,0,            // fmt chunk size = 16
    1,0,                 // PCM format
    1,0,                 // mono
    (uint8_t)(SAMPLE_RATE),(uint8_t)(SAMPLE_RATE>>8),(uint8_t)(SAMPLE_RATE>>16),(uint8_t)(SAMPLE_RATE>>24),
    (uint8_t)(byteRate),(uint8_t)(byteRate>>8),(uint8_t)(byteRate>>16),(uint8_t)(byteRate>>24),
    2,0,                 // block align = 2 bytes
    16,0,                // bits per sample
    'd','a','t','a',
    (uint8_t)(dataBytes),(uint8_t)(dataBytes>>8),(uint8_t)(dataBytes>>16),(uint8_t)(dataBytes>>24)
  };
  for (int i = 0; i < 44; i++) pushByte(h[i]);
}

// ── I2S Init (new ESP-IDF i2s_std driver, Philips mode) ────────
static void initI2S() {
  // 1. Create RX channel on I2S_NUM_0
  i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chanCfg.auto_clear = true;

  esp_err_t err = i2s_new_channel(&chanCfg, NULL, &gMicHandle);
  if (err != ESP_OK) {
    Serial.printf("[ERROR] i2s_new_channel: 0x%x\n", err);
    while (true) delay(1000);
  }

  // 2. Configure in Philips Standard I2S mode (correct for INMP441).
  //    IMPORTANT — use STEREO, not MONO.
  //    On ESP32-S3 the new i2s_std driver DMA buffer is always interleaved:
  //      [LEFT_word, RIGHT_word, LEFT_word, RIGHT_word, ...]
  //    even when MONO is selected. INMP441 with L/R=GND only outputs on the
  //    LEFT channel (WS=LOW period); the RIGHT channel words are zero/garbage.
  //    Using MONO + reading every word → [voice, 0, voice, 0...] comb filter
  //    at 8 kHz → robotic "autobot" distortion.
  //    Fix: STEREO mode + i+=2 in the sample loop → only LEFT words are used.
  i2s_std_config_t stdCfg = {
    .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                                    I2S_SLOT_MODE_STEREO),  // ← STEREO
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t)MIC_SCK_PIN,
      .ws   = (gpio_num_t)MIC_WS_PIN,
      .dout = I2S_GPIO_UNUSED,
      .din  = (gpio_num_t)MIC_SD_PIN,
      .invert_flags = { false, false, false }
    }
  };

  err = i2s_channel_init_std_mode(gMicHandle, &stdCfg);
  if (err != ESP_OK) {
    Serial.printf("[ERROR] i2s_channel_init_std_mode: 0x%x\n", err);
    while (true) delay(1000);
  }

  err = i2s_channel_enable(gMicHandle);
  if (err != ESP_OK) {
    Serial.printf("[ERROR] i2s_channel_enable: 0x%x\n", err);
    while (true) delay(1000);
  }

  // 3. Discard first 10 reads — INMP441 takes ~250ms to stabilize after power-on
  Serial.print("[I2S] Warming up");
  int32_t dummy[64]; size_t dummyBytes;
  for (int i = 0; i < 10; i++) {
    i2s_channel_read(gMicHandle, dummy, sizeof(dummy), &dummyBytes, pdMS_TO_TICKS(100));
    delay(10);
    Serial.print(".");
  }
  Serial.println(" Ready!");
}

// ── Record REC_SECONDS and stream as Base64 WAV ────────────────
static void doRecord() {
  const uint32_t totalSamples = SAMPLE_RATE * REC_SECONDS;
  const uint32_t dataBytes    = totalSamples * sizeof(int16_t);

  g_bufIdx  = 0;
  g_lineLen = 0;

  Serial.printf("\n[REC] Recording %d seconds... speak now!\n", REC_SECONDS);
  Serial.println("-----BEGIN_WAV_BASE64-----");

  pushWavHeader(dataBytes);

  uint32_t written = 0;
  int32_t  raw[64];
  size_t   bytesRead;

  while (written < totalSamples) {
    esp_err_t r = i2s_channel_read(gMicHandle, raw, sizeof(raw), &bytesRead, pdMS_TO_TICKS(200));
    if (r != ESP_OK || bytesRead == 0) continue;

    int n = (int)(bytesRead / sizeof(int32_t));
    // i += 2: DMA buffer is [LEFT, RIGHT, LEFT, RIGHT...] in STEREO mode.
    // INMP441 (L/R=GND) outputs on LEFT channel only → take even indices,
    // skip odd indices (right channel = zeros/garbage).
    for (int i = 0; i < n && written < totalSamples; i += 2) {
      // INMP441: 24-bit left-justified in 32-bit word → >> 16 gives top 16 bits.
      int16_t s = (int16_t)(raw[i] >> 16);
      pushByte((uint8_t)(s & 0xFF));           // little-endian LSB
      pushByte((uint8_t)((s >> 8) & 0xFF));    // little-endian MSB
      written++;
    }
    yield();
  }

  flushB64(g_bufIdx);   // flush any remaining bytes in the buffer
  g_bufIdx = 0;
  if (g_lineLen > 0) Serial.println();
  Serial.println("-----END_WAV_BASE64-----");
  Serial.println("[REC] Done! Copy the block above into audio.txt, then run:");
  Serial.println("      python -X utf8 decode_wav.py");
}

// ── Setup ───────────────────────────────────────────────────────
void setup() {
  Serial.begin(921600);
  delay(500);

  Serial.println("\n==============================");
  Serial.println("  INMP441 Mic Test — ESP32-S3");
  Serial.println("==============================");
  Serial.println("Pins: SD=10 | WS=11 | SCK=12 | L/R=GND");
  Serial.println("Baud: 921600 — make sure Serial Monitor matches!");
  Serial.println("------------------------------");

  initI2S();

  Serial.printf("Ready! Type 'r' + Enter to record %d seconds.\n", REC_SECONDS);
}

// ── Loop ────────────────────────────────────────────────────────
void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'r' || c == 'R') doRecord();
  }
}
