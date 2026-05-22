/**
 * INMP441 Recording Test — FireBeetle ESP32-E
 * ============================================
 * Wiring:
 *   INMP441 SCK → GPIO 18  |  VDD → 3.3V
 *   INMP441 WS  → GPIO 25  |  GND → GND
 *   INMP441 SD  → GPIO 23  |  L/R → GND
 *
 * Usage:
 *   1. Open Serial Monitor at 115200 baud (set to "Newline" line ending)
 *   2. Type 'r' + Enter to record 5 seconds
 *   3. Copy EVERYTHING between BEGIN and END into audio.txt
 *   4. Run: python -X utf8 decode_wav.py
 *   5. Open normalized_audio.wav and listen
 */

#include <driver/i2s.h>
#include <mbedtls/base64.h>

#define I2S_SCK     18
#define I2S_WS      25
#define I2S_SD      23
#define SAMPLE_RATE 16000
#define REC_SECONDS 5

// Base64 stream buffer (must be divisible by 3)
#define STREAM_BUF_SIZE 3000
static uint8_t g_buf[STREAM_BUF_SIZE];
static int     g_bufIdx   = 0;
static int     g_lineLen  = 0;

// ── Flush 'count' bytes from g_buf as Base64 ──────────────────
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

// ── Write 44-byte WAV header into the stream ──────────────────
static void pushWavHeader(uint32_t dataBytes) {
  const uint32_t byteRate = SAMPLE_RATE * 2;   // 16-bit mono
  const uint32_t riffSize = 36 + dataBytes;
  uint8_t h[44] = {
    'R','I','F','F',
    (uint8_t)(riffSize),(uint8_t)(riffSize>>8),(uint8_t)(riffSize>>16),(uint8_t)(riffSize>>24),
    'W','A','V','E',
    'f','m','t',' ',
    16,0,0,0,            // fmt chunk size
    1,0,                 // PCM
    1,0,                 // mono
    (uint8_t)(SAMPLE_RATE),(uint8_t)(SAMPLE_RATE>>8),(uint8_t)(SAMPLE_RATE>>16),(uint8_t)(SAMPLE_RATE>>24),
    (uint8_t)(byteRate),(uint8_t)(byteRate>>8),(uint8_t)(byteRate>>16),(uint8_t)(byteRate>>24),
    2,0,                 // block align
    16,0,                // bits per sample
    'd','a','t','a',
    (uint8_t)(dataBytes),(uint8_t)(dataBytes>>8),(uint8_t)(dataBytes>>16),(uint8_t)(dataBytes>>24)
  };
  for (int i = 0; i < 44; i++) pushByte(h[i]);
}

// ── I2S Init ──────────────────────────────────────────────────
void initI2S() {
  i2s_config_t cfg = {
    .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate          = SAMPLE_RATE,
    .bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count        = 4,
    .dma_buf_len          = 256,
    .use_apll             = false,
  };
  i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);

  i2s_pin_config_t pins = {
    .bck_io_num   = I2S_SCK,
    .ws_io_num    = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num  = I2S_SD,
  };
  i2s_set_pin(I2S_NUM_0, &pins);

  // Warm up
  int32_t dummy[64]; size_t b;
  for (int i = 0; i < 8; i++)
    i2s_read(I2S_NUM_0, dummy, sizeof(dummy), &b, 100);
}

// ── Record REC_SECONDS and stream as Base64 WAV ───────────────
void doRecord() {
  const uint32_t totalSamples = SAMPLE_RATE * REC_SECONDS;
  const uint32_t dataBytes    = totalSamples * sizeof(int16_t);

  g_bufIdx  = 0;
  g_lineLen = 0;

  Serial.println("\n-----BEGIN_WAV_BASE64-----");
  pushWavHeader(dataBytes);

  Serial.printf("// Recording %d seconds... speak now!\n", REC_SECONDS);

  uint32_t written = 0;
  int32_t  raw[64];
  size_t   bytesRead;

  while (written < totalSamples) {
    i2s_read(I2S_NUM_0, (void *)raw, sizeof(raw), &bytesRead, 200);
    int n = bytesRead / sizeof(int32_t);
    for (int i = 0; i < n && written < totalSamples; i++) {
      int16_t s = (int16_t)(raw[i] >> 16);
      pushByte((uint8_t)(s & 0xFF));
      pushByte((uint8_t)((s >> 8) & 0xFF));
      written++;
    }
  }

  flushB64(g_bufIdx);  // flush remaining bytes
  g_bufIdx = 0;
  if (g_lineLen > 0) Serial.println();
  Serial.println("-----END_WAV_BASE64-----");
  Serial.println("Done! Copy the block above into audio.txt, then run: python -X utf8 decode_wav.py");
}

// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(921600);
  delay(500);
  initI2S();
  Serial.println("Mic ready! Type 'r' + Enter to record 5 seconds.");
  Serial.println("(Baud: 921600 — make sure Serial Monitor matches!)");
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'r' || c == 'R') {
      doRecord();
    }
  }
}