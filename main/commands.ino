/**
 * ESP-SR Voice Recognition — ESP32-S3 + INMP441
 * ================================================
 * Uses ESP_SR (WakeNet + MultiNet) for offline wake-word and command
 * recognition via the Arduino ESP_SR library.
 *
 * Wake word: "Hi ESP" (built-in WakeNet model)
 * After wake word, speak a command like:
 *   - "Turn on the light"
 *   - "Turn off the light"
 *
 * The full recognised command is printed to Serial Monitor.
 *
 * Wiring (same as before):
 *   INMP441 SD  → GPIO 10  |  VDD → 3.3V
 *   INMP441 WS  → GPIO 11  |  GND → GND
 *   INMP441 SCK → GPIO 12  |  L/R → GND (left channel)
 *
 * Arduino IDE Settings:
 *   Board:            ESP32S3 Dev Module
 *   PSRAM:            OPI PSRAM
 *   Flash Size:       16MB
 *   Partition Scheme:  ESP SR 16M (3MB APP/7MB SPIFFS/2.9MB MODEL)
 *   Upload Speed:     921600
 */

#include "ESP_I2S.h"
#include <ArduinoJson.h>
#include <ESP_SR.h>
#include <WiFiClientSecure.h>
#include <driver/i2s_std.h>
#include <esp_random.h>
#include <math.h>
#include <mbedtls/base64.h>
#include <pgmspace.h>

#include "failure_message_audio.h"

extern bool gLightOn;
extern bool gNeedsUpdate;
extern unsigned long gLastExternalControlTime;

// ── Pin Definitions (INMP441 on ESP32-S3) ──────────────────────
#define I2S_PIN_BCK 12 // SCK  (serial clock / bit clock)
#define I2S_PIN_WS 11  // WS   (word select / LRCLK)
#define I2S_PIN_DIN 10 // SD   (serial data / data in)

// ── Audio Configuration ────────────────────────────────────────
#define I2S_SAMPLE_RATE 16000

// INMP441 with L/R=GND → data only in LEFT slot; right slot = 0.
// "MN" (Mic + Null) tells ESP-SR to treat [slot0=mic, slot1=null].
// We read BOTH slots in stereo mode so the frame layout matches.
#define SR_INPUT_FORMAT "MN"
#define SR_INPUT_CHANNELS SR_CHANNELS_STEREO
#define I2S_SLOT_MODE I2S_SLOT_MODE_STEREO

// ── Speaker Configuration (MAX98357 for Gemini) ────────────────
#define I2S_BCLK_PIN 5
#define I2S_LRC_PIN 4
#define I2S_DOUT_PIN 7
#define GEMINI_I2S_PORT I2S_NUM_1
#define AUDIO_SAMPLE_RATE 24000

static const char *GEMINI_API_KEY = "AIzaSy_________________cfIKw";
static const char *GEMINI_MODEL = "gemini-3.1-flash-live-preview";
static const char *GEMINI_HOST = "generativelanguage.googleapis.com";
static const int GEMINI_PORT = 443;

static uint8_t *gAudioBuf = nullptr;
static size_t gAudioLen = 0;
static size_t gAudioCap = 0;
static i2s_chan_handle_t gI2sTxHandle = NULL;

static uint8_t *gCommandAudioBuf = nullptr;
static size_t gCommandAudioLen = 0;
static size_t gCommandAudioCap = 0;
static volatile bool gCaptureCommandAudio = false;
static volatile bool gGeminiSetupComplete = false;
static volatile bool gGeminiPendingUtterance = false;
static volatile bool gGeminiWaitingForResponse = false;
static unsigned long gGeminiPendingSince = 0;
static unsigned long gGeminiResponseSince = 0;

// ── I2S Instance ───────────────────────────────────────────────
I2SClass i2s;

// ── Command IDs ────────────────────────────────────────────────
enum {
  SR_CMD_LIGHT_ON,
  SR_CMD_LIGHT_OFF,
};

// ── Command Phrases ────────────────────────────────────────────
// In this specific ESP-SR core version, we must explicitly provide
// the generated phonemes as the third argument in the sr_cmd_t struct.
static const sr_cmd_t sr_commands[] = {
    // Light ON
    {SR_CMD_LIGHT_ON, "Turn on the light"},
    {SR_CMD_LIGHT_ON, "Switch on the light"},

    // Light OFF
    {SR_CMD_LIGHT_OFF, "Turn off the light"},
    {SR_CMD_LIGHT_OFF, "Switch off the light"},
    {SR_CMD_LIGHT_OFF, "Go dark"},
};

// ── Helper: map command ID → name ──────────────────────────────
static const char *commandName(int id) {
  switch (id) {
  case SR_CMD_LIGHT_ON:
    return "TURN ON THE LIGHT";
  case SR_CMD_LIGHT_OFF:
    return "TURN OFF THE LIGHT";
  default:
    return "UNKNOWN";
  }
}

// Forward declarations for Gemini
extern bool isStreaming;
void openGeminiWebSocket();
void closeGeminiWebSocket();
static void resetCommandAudio();
static void onSrAudio(const int16_t *samples, size_t sample_count,
                      uint8_t channels);
static bool sendBufferedCommandToGemini();
static void i2sInitSpeaker();
static void playWakeBeep();
static void playLocalFailureMessage();
static bool wsConnect();
static void wsSendText(const String &json);
static bool wsReadPayload(uint64_t payloadLen, uint8_t **outBuf,
                          size_t *outLen);
static void processGeminiFrames();

// ── SR Event Callback ──────────────────────────────────────────
void onSrEvent(sr_event_t event, int command_id, int phrase_id) {
  switch (event) {

  case SR_EVENT_WAKEWORD:
    Serial.println();
    Serial.println("========================================");
    Serial.println("  Wake word detected! Listening...");
    Serial.println("========================================");
    // ALWAYS switch to command mode here.
    // (With a single mic on a stereo bus, SR_EVENT_WAKEWORD_CHANNEL
    //  may not fire, so we must not rely on it.)
    ESP_SR.setMode(SR_MODE_COMMAND);

    gCaptureCommandAudio = false;
    playWakeBeep();

    // Open websocket to Gemini Live API in parallel
    resetCommandAudio();
    gCaptureCommandAudio = true;
    gGeminiSetupComplete = false;
    gGeminiPendingUtterance = false;
    gGeminiWaitingForResponse = false;
    openGeminiWebSocket();
    isStreaming = true;
    break;

  case SR_EVENT_WAKEWORD_CHANNEL:
    Serial.printf("  Wake word verified on channel %d\n", command_id);
    ESP_SR.setMode(SR_MODE_COMMAND);
    break;

  case SR_EVENT_TIMEOUT:
    Serial.println("  No local command heard -- timeout.");

    // Stop capturing and let commandsLoop send the buffered utterance once the
    // Live API setup handshake has completed.
    gCaptureCommandAudio = false;
    isStreaming = false;
    gGeminiPendingUtterance = true;
    gGeminiPendingSince = millis();
    Serial.printf("  Sending %d bytes of command audio to Gemini...\n",
                  (int)gCommandAudioLen);

    Serial.println("  Waiting for Gemini audio response...");
    Serial.println("----------------------------------------");
    break;

  case SR_EVENT_COMMAND:
    // Local command recognized -> close socket immediately
    gCaptureCommandAudio = false;
    resetCommandAudio();
    gGeminiPendingUtterance = false;
    gGeminiWaitingForResponse = false;
    closeGeminiWebSocket();
    isStreaming = false;

    Serial.println("----------------------------------------");
    Serial.println("  Command recognised!");
    Serial.printf("     Command ID : %d\n", command_id);
    Serial.printf("     Phrase  ID : %d\n", phrase_id);
    Serial.printf("     Command    : %s\n", commandName(command_id));
    if (phrase_id >= 0 &&
        phrase_id < (int)(sizeof(sr_commands) / sizeof(sr_cmd_t))) {
      Serial.printf("     Phrase     : \"%s\"\n", sr_commands[phrase_id].str);
    }

    if (command_id == SR_CMD_LIGHT_ON) {
      if (!gLightOn) {
        gLightOn = true;
        gNeedsUpdate = true;
      }
      gLastExternalControlTime = millis();
      Serial.println("     Action     : Light turned ON via Voice");
    } else if (command_id == SR_CMD_LIGHT_OFF) {
      if (gLightOn) {
        gLightOn = false;
        gNeedsUpdate = true;
      }
      gLastExternalControlTime = millis();
      Serial.println("     Action     : Light turned OFF via Voice");
    }

    Serial.println("----------------------------------------");

    // Local command is complete; require the wake word for the next request.
    ESP_SR.setMode(SR_MODE_WAKEWORD);
    break;

  default:
    Serial.printf("  [SR] Unknown event: %d\n", event);
    break;
  }
}

// ── Setup ──────────────────────────────────────────────────────
void commandsSetup() {

  i2sInitSpeaker(); // Initialize Speaker I2S for Gemini Responses

  Serial.println();
  Serial.println("==============================================");
  Serial.println("  ESP-SR Voice Recognition -- ESP32-S3");
  Serial.println("  Mic: INMP441  (I2S Standard, 16 kHz)");
  Serial.println("  Wake word: \"Hi ESP\"");
  Serial.println("==============================================");
  Serial.printf("Pins: DIN(SD)=%d | WS=%d | BCK(SCK)=%d\n", I2S_PIN_DIN,
                I2S_PIN_WS, I2S_PIN_BCK);
  Serial.println();

  // ── 1. Configure I2S for INMP441 ────────────────────────────
  i2s.setPins(I2S_PIN_BCK, I2S_PIN_WS, -1, I2S_PIN_DIN);
  i2s.setTimeout(1000);

  // INMP441 outputs 32-bit I2S frames. Begin in 32-bit STEREO mode
  // with SLOT_BOTH so the driver reads both L and R slots.
  // INMP441 (L/R=GND) puts audio in LEFT slot, right slot is silent (0).
  // This gives ESP-SR the [mic, null] layout expected by format "MN".
  if (!i2s.begin(I2S_MODE_STD, I2S_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_32BIT,
                 I2S_SLOT_MODE, I2S_STD_SLOT_BOTH)) {
    Serial.println("[ERROR] Failed to initialise I2S!");
    while (true)
      delay(1000);
  }

  i2s.configureRX(I2S_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE,
                  I2S_RX_TRANSFORM_32_TO_16);

  Serial.println("[I2S] Initialised OK (32-bit -> 16-bit transform active)");

  // ── 2. Start ESP_SR ──────────────────────────────────────────
  ESP_SR.onEvent(onSrEvent);
  ESP_SR.onAudio(onSrAudio);

  bool ok =
      ESP_SR.begin(i2s, sr_commands, sizeof(sr_commands) / sizeof(sr_cmd_t),
                   SR_INPUT_CHANNELS, SR_MODE_WAKEWORD, SR_INPUT_FORMAT);
  if (!ok) {
    Serial.println("[ERROR] ESP_SR.begin() failed!");
    Serial.println("  -> Check partition scheme (needs SR model partition)");
    Serial.println("  -> Check PSRAM is enabled (OPI PSRAM)");
    while (true)
      delay(1000);
  }

  Serial.println("[ESP_SR] Started OK");
  Serial.println();
  Serial.println("Say \"Hi ESP\" to wake, then speak a command:");
  Serial.println("  - Turn on/off the light");
  Serial.println();
}

WiFiClientSecure gGeminiClient;
bool isSocketConnected = false;
bool isStreaming = false;

static void resetCommandAudio() { gCommandAudioLen = 0; }

static void appendCommandAudio(const uint8_t *data, size_t len) {
  if (len == 0)
    return;
  if (gCommandAudioLen + len > gCommandAudioCap) {
    size_t newCap = gCommandAudioCap + max(len, (size_t)32768);
    uint8_t *nb = (uint8_t *)realloc(gCommandAudioBuf, newCap);
    if (!nb) {
      Serial.println("[GEMINI] OOM: command audio buffer");
      return;
    }
    gCommandAudioBuf = nb;
    gCommandAudioCap = newCap;
  }
  memcpy(gCommandAudioBuf + gCommandAudioLen, data, len);
  gCommandAudioLen += len;
}

void occupancyProcessAudio(const int16_t *samples, size_t sample_count, uint8_t channels);

static void onSrAudio(const int16_t *samples, size_t sample_count,
                      uint8_t channels) {
  if (!samples || channels == 0)
    return;

  // Feed raw audio to occupancy processing engine
  occupancyProcessAudio(samples, sample_count, channels);

  if (!gCaptureCommandAudio)
    return;

  size_t frames = sample_count / channels;
  const size_t MAX_FRAMES_PER_COPY = 512;
  int16_t mono[MAX_FRAMES_PER_COPY];

  while (frames > 0) {
    size_t batch = min(frames, MAX_FRAMES_PER_COPY);
    for (size_t i = 0; i < batch; i++) {
      mono[i] = samples[i * channels]; // INMP441 mic is the left slot.
    }
    appendCommandAudio((const uint8_t *)mono, batch * sizeof(int16_t));
    samples += batch * channels;
    frames -= batch;
  }
}

static void i2sInitSpeaker() {
  i2s_chan_config_t chan_cfg =
      I2S_CHANNEL_DEFAULT_CONFIG(GEMINI_I2S_PORT, I2S_ROLE_MASTER);
  chan_cfg.auto_clear = true;
  esp_err_t err = i2s_new_channel(&chan_cfg, &gI2sTxHandle, NULL);
  if (err != ESP_OK) {
    Serial.printf("[GEMINI] i2s_new_channel err: 0x%x\n", err);
    return;
  }

  i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
      .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                  I2S_SLOT_MODE_MONO),
      .gpio_cfg =
          {
              .mclk = I2S_GPIO_UNUSED,
              .bclk = (gpio_num_t)I2S_BCLK_PIN,
              .ws = (gpio_num_t)I2S_LRC_PIN,
              .dout = (gpio_num_t)I2S_DOUT_PIN,
              .din = I2S_GPIO_UNUSED,
              .invert_flags = {.mclk_inv = false,
                               .bclk_inv = false,
                               .ws_inv = false},
          },
  };
  i2s_channel_init_std_mode(gI2sTxHandle, &std_cfg);
  i2s_channel_enable(gI2sTxHandle);
}

static void playPCMChunk(const uint8_t *data, size_t len) {
  if (!gI2sTxHandle || len == 0)
    return;
  size_t written = 0;
  i2s_channel_write(gI2sTxHandle, data, len, &written, portMAX_DELAY);
}

static uint16_t readProgmemLe16(const uint8_t *data, size_t offset) {
  return (uint16_t)pgm_read_byte(data + offset) |
         ((uint16_t)pgm_read_byte(data + offset + 1) << 8);
}

static uint32_t readProgmemLe32(const uint8_t *data, size_t offset) {
  return (uint32_t)pgm_read_byte(data + offset) |
         ((uint32_t)pgm_read_byte(data + offset + 1) << 8) |
         ((uint32_t)pgm_read_byte(data + offset + 2) << 16) |
         ((uint32_t)pgm_read_byte(data + offset + 3) << 24);
}

static bool progmemChunkIdEquals(const uint8_t *data, size_t offset,
                                 const char *id) {
  for (size_t i = 0; i < 4; i++) {
    if ((char)pgm_read_byte(data + offset + i) != id[i])
      return false;
  }
  return true;
}

static void playLocalFailureMessage() {
  if (!gI2sTxHandle)
    return;

  if (gFailureMessageWavLen < 44 ||
      !progmemChunkIdEquals(gFailureMessageWav, 0, "RIFF") ||
      !progmemChunkIdEquals(gFailureMessageWav, 8, "WAVE")) {
    Serial.println("[GEMINI] Local failure WAV is invalid");
    return;
  }

  uint16_t audioFormat = 0;
  uint16_t channels = 0;
  uint32_t sampleRate = 0;
  uint16_t bitsPerSample = 0;
  size_t dataOffset = 0;
  size_t dataLen = 0;

  size_t offset = 12;
  while (offset + 8 <= gFailureMessageWavLen) {
    size_t chunkSize = (size_t)readProgmemLe32(gFailureMessageWav, offset + 4);
    size_t chunkData = offset + 8;
    if (chunkData + chunkSize > gFailureMessageWavLen)
      break;

    if (progmemChunkIdEquals(gFailureMessageWav, offset, "fmt ")) {
      audioFormat = readProgmemLe16(gFailureMessageWav, chunkData);
      channels = readProgmemLe16(gFailureMessageWav, chunkData + 2);
      sampleRate = readProgmemLe32(gFailureMessageWav, chunkData + 4);
      bitsPerSample = readProgmemLe16(gFailureMessageWav, chunkData + 14);
    } else if (progmemChunkIdEquals(gFailureMessageWav, offset, "data")) {
      dataOffset = chunkData;
      dataLen = chunkSize;
    }

    offset = chunkData + chunkSize + (chunkSize & 1);
  }

  if (audioFormat != 1 || channels != 1 || sampleRate != AUDIO_SAMPLE_RATE ||
      bitsPerSample != 16 || dataOffset == 0 || dataLen == 0) {
    Serial.println("[GEMINI] Local failure WAV format is unsupported");
    return;
  }

  Serial.println("[GEMINI] Playing local failure message.");
  const size_t chunkSize = 1024;
  uint8_t chunk[chunkSize];
  size_t played = 0;
  while (played < dataLen) {
    size_t toPlay = min(chunkSize, dataLen - played);
    for (size_t i = 0; i < toPlay; i++) {
      chunk[i] = pgm_read_byte(gFailureMessageWav + dataOffset + played + i);
    }
    playPCMChunk(chunk, toPlay);
    played += toPlay;
    yield();
  }
}

void commandsPlayFailureMessage() { playLocalFailureMessage(); }

static void playWakeBeep() {
  if (!gI2sTxHandle)
    return;

  const int durationMs = 80;
  const int frequencyHz = 1200;
  const float amplitude = 5000.0f;
  const float twoPi = 6.28318530718f;
  const size_t totalSamples = (AUDIO_SAMPLE_RATE * durationMs) / 1000;
  const size_t fadeSamples = (AUDIO_SAMPLE_RATE * 5) / 1000;
  const size_t chunkSamples = 128;
  int16_t samples[chunkSamples];

  size_t generated = 0;
  while (generated < totalSamples) {
    size_t count = min(chunkSamples, totalSamples - generated);
    for (size_t i = 0; i < count; i++) {
      size_t sampleIndex = generated + i;
      float envelope = 1.0f;
      if (sampleIndex < fadeSamples) {
        envelope = (float)sampleIndex / fadeSamples;
      } else if (totalSamples - sampleIndex < fadeSamples) {
        envelope = (float)(totalSamples - sampleIndex) / fadeSamples;
      }

      float phase = twoPi * frequencyHz * sampleIndex / AUDIO_SAMPLE_RATE;
      samples[i] = (int16_t)(sinf(phase) * amplitude * envelope);
    }
    playPCMChunk((const uint8_t *)samples, count * sizeof(int16_t));
    generated += count;
    yield();
  }
}

static void appendAudio(const uint8_t *data, size_t len) {
  if (gAudioLen + len > gAudioCap) {
    size_t newCap = gAudioCap + max(len, (size_t)32768);
    uint8_t *nb = (uint8_t *)realloc(gAudioBuf, newCap);
    if (!nb) {
      Serial.println("[GEMINI] OOM: audio buffer");
      return;
    }
    gAudioBuf = nb;
    gAudioCap = newCap;
  }
  memcpy(gAudioBuf + gAudioLen, data, len);
  gAudioLen += len;
}

static void playAllAudio() {
  if (!gAudioBuf || gAudioLen == 0)
    return;
  Serial.printf("[GEMINI] Playing total %d PCM bytes (%.1fs)\n", (int)gAudioLen,
                (float)gAudioLen / (AUDIO_SAMPLE_RATE * 2));
  const size_t CHUNK = 8192;
  size_t offset = 0;
  while (offset < gAudioLen) {
    size_t toPlay = min(CHUNK, gAudioLen - offset);
    playPCMChunk(gAudioBuf + offset, toPlay);
    offset += toPlay;
    yield();
  }
  free(gAudioBuf);
  gAudioBuf = nullptr;
  gAudioLen = 0;
  gAudioCap = 0;
}

void processGeminiResponse(uint8_t *payload) {
  DynamicJsonDocument doc(65536);
  DeserializationError err = deserializeJson(doc, (char *)payload);
  if (err) {
    Serial.printf("[GEMINI] JSON err: %s\n", err.c_str());
    return;
  }

  if (doc.containsKey("setupComplete")) {
    Serial.println("[GEMINI] setupComplete!");
    gGeminiSetupComplete = true;
  }

  if (doc.containsKey("error")) {
    String errJson;
    serializeJson(doc["error"], errJson);
    Serial.printf("[GEMINI] API error: %s\n", errJson.c_str());
    gGeminiPendingUtterance = false;
    gGeminiWaitingForResponse = false;
    resetCommandAudio();
    closeGeminiWebSocket();
    ESP_SR.setMode(SR_MODE_WAKEWORD);
    return;
  }

  if (doc.containsKey("serverContent")) {
    JsonObject sc = doc["serverContent"];

    if (sc.containsKey("modelTurn")) {
      JsonArray parts = sc["modelTurn"]["parts"].as<JsonArray>();
      for (JsonObject part : parts) {
        if (part.containsKey("inlineData")) {
          const char *b64 = part["inlineData"]["data"];
          if (b64 && strlen(b64) > 0) {
            size_t b64len = strlen(b64);
            size_t bufSize = (b64len * 3) / 4 + 4;
            uint8_t *pcm = (uint8_t *)malloc(bufSize);
            if (pcm) {
              size_t outLen = 0;
              mbedtls_base64_decode(pcm, bufSize, &outLen,
                                    (const unsigned char *)b64, b64len);
              if (outLen > 0) {
                Serial.printf("[GEMINI] Buffering %d PCM bytes\n", (int)outLen);
                appendAudio(pcm, outLen);
              }
              free(pcm);
            }
          }
        }
      }
    }

    if (sc["turnComplete"].as<bool>()) {
      Serial.println("[GEMINI] turnComplete!");
      playAllAudio(); // Play all audio at once to prevent stuttering
      gGeminiWaitingForResponse = false;
      closeGeminiWebSocket();
      Serial.println("[GEMINI] Returning to wake-word listening.");
      ESP_SR.setMode(SR_MODE_WAKEWORD);
    }
  }
}

static bool sendBufferedCommandToGemini() {
  if (!isSocketConnected || !gGeminiSetupComplete) {
    return false;
  }

  if (!gCommandAudioBuf || gCommandAudioLen == 0) {
    Serial.println(
        "[GEMINI] No command audio captured; returning to wake word.");
    gGeminiPendingUtterance = false;
    closeGeminiWebSocket();
    ESP_SR.setMode(SR_MODE_WAKEWORD);
    return false;
  }

  const size_t PCM_CHUNK = 12000;
  size_t b64Cap = 4 * ((PCM_CHUNK + 2) / 3) + 1;
  unsigned char *b64 = (unsigned char *)malloc(b64Cap);
  if (!b64) {
    Serial.println("[GEMINI] OOM: base64 command audio");
    gGeminiPendingUtterance = false;
    resetCommandAudio();
    closeGeminiWebSocket();
    ESP_SR.setMode(SR_MODE_WAKEWORD);
    return false;
  }

  Serial.printf("[GEMINI] Sending command audio: %d PCM bytes\n",
                (int)gCommandAudioLen);
  bool ok = true;
  size_t offset = 0;
  while (offset < gCommandAudioLen && ok) {
    size_t chunkLen = min(PCM_CHUNK, gCommandAudioLen - offset);
    size_t b64Len = 0;
    int err = mbedtls_base64_encode(b64, b64Cap, &b64Len,
                                    gCommandAudioBuf + offset, chunkLen);
    if (err != 0) {
      Serial.printf("[GEMINI] base64 encode err: %d\n", err);
      ok = false;
      break;
    }
    b64[b64Len] = 0;

    String json;
    json.reserve(b64Len + 128);
    json = "{\"realtimeInput\":{\"audio\":{\"data\":\"";
    json += (const char *)b64;
    json += "\",\"mimeType\":\"audio/pcm;rate=16000\"}}}";

    wsSendText(json);
    ok = gGeminiClient.connected();
    offset += chunkLen;
    processGeminiFrames();
    yield();
  }
  free(b64);
  resetCommandAudio();

  if (ok) {
    wsSendText("{\"realtimeInput\":{\"audioStreamEnd\":true}}");
    ok = gGeminiClient.connected();
  }

  if (ok) {
    gGeminiPendingUtterance = false;
    gGeminiWaitingForResponse = true;
    gGeminiResponseSince = millis();
    Serial.println(
        "[GEMINI] Command audio sent; waiting for response audio...");
  } else {
    Serial.println("[GEMINI] Failed to send command audio");
    gGeminiPendingUtterance = false;
    playLocalFailureMessage();
    closeGeminiWebSocket();
    ESP_SR.setMode(SR_MODE_WAKEWORD);
  }

  return ok;
}

static bool wsConnect() {
  gGeminiClient.setInsecure();
  Serial.printf("[GEMINI] TCP -> %s:%d\n", GEMINI_HOST, GEMINI_PORT);
  if (!gGeminiClient.connect(GEMINI_HOST, GEMINI_PORT)) {
    Serial.println("[GEMINI] TCP FAILED");
    isSocketConnected = false;
    return false;
  }
  Serial.println("[GEMINI] TCP OK. HTTP Upgrade...");

  String path = "/ws/"
                "google.ai.generativelanguage.v1beta.GenerativeService."
                "BidiGenerateContent?key=";
  path += GEMINI_API_KEY;

  gGeminiClient.print("GET ");
  gGeminiClient.print(path);
  gGeminiClient.println(" HTTP/1.1");
  gGeminiClient.print("Host: ");
  gGeminiClient.println(GEMINI_HOST);
  gGeminiClient.println("Upgrade: websocket");
  gGeminiClient.println("Connection: Upgrade");
  gGeminiClient.println("Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==");
  gGeminiClient.println("Sec-WebSocket-Version: 13");
  gGeminiClient.println();

  unsigned long t = millis();
  while (!gGeminiClient.available() && millis() - t < 10000) {
    delay(10);
  }

  String status = gGeminiClient.readStringUntil('\n');
  status.trim();
  Serial.printf("[GEMINI] HTTP: %s\n", status.c_str());
  if (!status.startsWith("HTTP/1.1 101")) {
    t = millis();
    while (gGeminiClient.connected() && millis() - t < 3000) {
      if (!gGeminiClient.available()) {
        delay(1);
        continue;
      }
      String line = gGeminiClient.readStringUntil('\n');
      line.trim();
      Serial.printf("  %s\n", line.c_str());
      if (line.length() == 0)
        break;
    }
    String body = gGeminiClient.readString();
    if (body.length())
      Serial.printf("[GEMINI] Body: %.500s\n", body.c_str());
    isSocketConnected = false;
    return false;
  }

  t = millis();
  while (gGeminiClient.connected() && millis() - t < 3000) {
    if (!gGeminiClient.available()) {
      delay(1);
      continue;
    }
    String line = gGeminiClient.readStringUntil('\n');
    line.trim();
    if (line.length() == 0)
      break;
  }

  isSocketConnected = true;
  Serial.println("[WS] Connected to Gemini Stream");
  return true;
}

static void wsSendText(const String &json) {
  if (!gGeminiClient.connected()) {
    isSocketConnected = false;
    return;
  }

  size_t len = json.length();
  uint32_t maskRaw = esp_random();
  uint8_t mask[4];
  memcpy(mask, &maskRaw, 4);

  uint8_t hdr[14];
  int hLen = 0;
  hdr[hLen++] = 0x81;
  if (len < 126) {
    hdr[hLen++] = 0x80 | (uint8_t)len;
  } else if (len < 65536) {
    hdr[hLen++] = 0x80 | 126;
    hdr[hLen++] = (len >> 8) & 0xFF;
    hdr[hLen++] = len & 0xFF;
  } else {
    hdr[hLen++] = 0x80 | 127;
    for (int i = 7; i >= 0; i--) {
      hdr[hLen++] = (len >> (8 * i)) & 0xFF;
    }
  }
  hdr[hLen++] = mask[0];
  hdr[hLen++] = mask[1];
  hdr[hLen++] = mask[2];
  hdr[hLen++] = mask[3];
  gGeminiClient.write(hdr, hLen);

  const char *data = json.c_str();
  uint8_t masked[512];
  size_t offset = 0;
  while (offset < len) {
    size_t chunk = min(sizeof(masked), len - offset);
    for (size_t i = 0; i < chunk; i++) {
      masked[i] = (uint8_t)(data[offset + i] ^ mask[(offset + i) & 3]);
    }
    gGeminiClient.write(masked, chunk);
    offset += chunk;
  }
}

static bool wsReadPayload(uint64_t payloadLen, uint8_t **outBuf,
                          size_t *outLen) {
  *outBuf = (uint8_t *)malloc((size_t)payloadLen + 1);
  if (!*outBuf) {
    Serial.printf("[GEMINI] OOM: cannot alloc %llu bytes\n",
                  (unsigned long long)payloadLen);
    return false;
  }

  uint64_t bytesRead = 0;
  unsigned long lastRecv = millis();
  while (bytesRead < payloadLen) {
    int avail = gGeminiClient.available();
    if (avail > 0) {
      size_t toRead = min((size_t)avail, (size_t)(payloadLen - bytesRead));
      int n = gGeminiClient.read(*outBuf + bytesRead, toRead);
      if (n > 0) {
        bytesRead += n;
        lastRecv = millis();
      }
    } else if (!gGeminiClient.connected()) {
      Serial.println("[GEMINI] Disconnected during payload read");
      free(*outBuf);
      *outBuf = nullptr;
      return false;
    } else if (millis() - lastRecv > 15000) {
      Serial.printf("[GEMINI] Payload timeout (%llu/%llu bytes)\n",
                    (unsigned long long)bytesRead,
                    (unsigned long long)payloadLen);
      free(*outBuf);
      *outBuf = nullptr;
      return false;
    }
    delay(1);
    yield();
  }

  (*outBuf)[payloadLen] = 0;
  *outLen = (size_t)payloadLen;
  return true;
}

static void processGeminiFrames() {
  while (gGeminiClient.connected() && gGeminiClient.available() >= 2) {
    uint8_t b1 = gGeminiClient.read();
    uint8_t b2 = gGeminiClient.read();
    uint8_t opcode = b1 & 0x0F;
    uint64_t payloadLen = (uint64_t)(b2 & 0x7F);

    if (payloadLen == 126) {
      unsigned long t = millis();
      while (gGeminiClient.available() < 2 && millis() - t < 2000)
        delay(1);
      payloadLen = ((uint16_t)gGeminiClient.read() << 8) | gGeminiClient.read();
    } else if (payloadLen == 127) {
      unsigned long t = millis();
      while (gGeminiClient.available() < 8 && millis() - t < 2000)
        delay(1);
      payloadLen = 0;
      for (int i = 0; i < 8; i++)
        payloadLen = (payloadLen << 8) | gGeminiClient.read();
    }

    if (opcode == 0x08) {
      uint16_t code = 0;
      String reason = "";
      if (payloadLen >= 2) {
        unsigned long t = millis();
        while ((uint64_t)gGeminiClient.available() < payloadLen &&
               millis() - t < 2000)
          delay(1);
        code = ((uint16_t)gGeminiClient.read() << 8) | gGeminiClient.read();
        for (uint64_t i = 2; i < payloadLen && gGeminiClient.available(); i++)
          reason += (char)gGeminiClient.read();
      }
      Serial.printf("[GEMINI] CLOSE frame: code=%d reason='%s'\n", code,
                    reason.c_str());
      closeGeminiWebSocket();
      if (gGeminiPendingUtterance || gGeminiWaitingForResponse) {
        gGeminiPendingUtterance = false;
        gGeminiWaitingForResponse = false;
        resetCommandAudio();
        ESP_SR.setMode(SR_MODE_WAKEWORD);
      }
      return;
    }

    if (opcode == 0x09) {
      unsigned long t = millis();
      while ((uint64_t)gGeminiClient.available() < payloadLen &&
             millis() - t < 1000)
        delay(1);
      uint8_t *pingData =
          payloadLen > 0 ? (uint8_t *)malloc((size_t)payloadLen) : nullptr;
      if (pingData) {
        for (uint64_t i = 0; i < payloadLen; i++)
          pingData[i] = gGeminiClient.read();
      }

      uint32_t maskRaw = esp_random();
      uint8_t mask[4];
      memcpy(mask, &maskRaw, 4);
      uint8_t pongHdr[6] = {0x8A,    (uint8_t)(0x80 | (uint8_t)payloadLen),
                            mask[0], mask[1],
                            mask[2], mask[3]};
      gGeminiClient.write(pongHdr, 6);
      if (pingData) {
        for (uint64_t i = 0; i < payloadLen; i++)
          gGeminiClient.write(pingData[i] ^ mask[i & 3]);
        free(pingData);
      }
      continue;
    }

    if (opcode != 0x01 && opcode != 0x02 && opcode != 0x00) {
      unsigned long t = millis();
      while ((uint64_t)gGeminiClient.available() < payloadLen &&
             millis() - t < 1000)
        delay(1);
      for (uint64_t i = 0; i < payloadLen && gGeminiClient.available(); i++)
        gGeminiClient.read();
      continue;
    }

    if (payloadLen == 0)
      continue;

    uint8_t *rawBuf = nullptr;
    size_t rawLen = 0;
    if (!wsReadPayload(payloadLen, &rawBuf, &rawLen)) {
      closeGeminiWebSocket();
      ESP_SR.setMode(SR_MODE_WAKEWORD);
      return;
    }

    Serial.printf("[GEMINI][FRAME] %llu bytes: %.300s%s\n",
                  (unsigned long long)payloadLen, (char *)rawBuf,
                  payloadLen > 300 ? "..." : "");
    processGeminiResponse(rawBuf);
    free(rawBuf);
  }

  if (isSocketConnected && !gGeminiClient.connected()) {
    Serial.println("[WS] Disconnected!");
    isSocketConnected = false;
    if (gGeminiPendingUtterance || gGeminiWaitingForResponse) {
      gGeminiPendingUtterance = false;
      gGeminiWaitingForResponse = false;
      resetCommandAudio();
      ESP_SR.setMode(SR_MODE_WAKEWORD);
    }
  }
}

void openGeminiWebSocket() {
  Serial.println("[WS] Opening socket to Gemini Live API...");
  gGeminiSetupComplete = false;

  if (!wsConnect()) {
    Serial.println("[WS] Failed to open Gemini socket.");
    closeGeminiWebSocket();
    return;
  }

  StaticJsonDocument<768> doc;
  JsonObject setup = doc.createNestedObject("setup");
  setup["model"] = String("models/") + GEMINI_MODEL;

  JsonObject systemInstruction = setup.createNestedObject("system_instruction");
  JsonArray siParts = systemInstruction.createNestedArray("parts");
  siParts.createNestedObject()["text"] =
      "MANDATORY RULES:\n"
      "1. Only answer the user's request as short as possible, no more than 30 words.\n"
      "2. Absolutely do not ask follow-up questions.\n"
      "3. After providing the information, end the answer immediately.\n";

  JsonArray tools = setup.createNestedArray("tools");
  tools.createNestedObject().createNestedObject("googleSearch");

  JsonObject genConfig = setup.createNestedObject("generationConfig");
  genConfig.createNestedArray("responseModalities").add("AUDIO");
  genConfig.createNestedObject("speechConfig")
      .createNestedObject("voiceConfig")
      .createNestedObject("prebuiltVoiceConfig")["voiceName"] = "Aoede";

  String json;
  serializeJson(doc, json);
  Serial.printf("[GEMINI] Setup: %s\n", json.c_str());
  wsSendText(json);
}

void closeGeminiWebSocket() {
  Serial.println("[WS] Closing Gemini socket.");
  if (gGeminiClient.connected()) {
    gGeminiClient.stop();
  }
  isSocketConnected = false;
  gGeminiSetupComplete = false;
}

// Hàm gửi dữ liệu âm thanh thô
void streamToGemini(uint8_t *data, size_t len) {
  (void)data;
  (void)len;
}

void commandsLoop() {
  if (isSocketConnected) {
    processGeminiFrames();
  }

  if (gGeminiPendingUtterance) {
    if (isSocketConnected && gGeminiSetupComplete) {
      sendBufferedCommandToGemini();
    } else if (millis() - gGeminiPendingSince > 10000) {
      Serial.println("[GEMINI] Timed out waiting for setupComplete.");
      gGeminiPendingUtterance = false;
      resetCommandAudio();
      closeGeminiWebSocket();
      ESP_SR.setMode(SR_MODE_WAKEWORD);
    }
  }

  if (gGeminiWaitingForResponse && millis() - gGeminiResponseSince > 30000) {
    Serial.println("[GEMINI] Timed out waiting for response audio.");
    gGeminiWaitingForResponse = false;
    closeGeminiWebSocket();
    ESP_SR.setMode(SR_MODE_WAKEWORD);
  }
}
