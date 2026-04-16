// gemini.ino
// Gemini 3.1 Flash Live API — raw WebSocket via WiFiClientSecure
// Avoiding WebSocketsClient, manual implementation of WS frame RFC 6455 to
// avoid library bugs.
//
// Required library: ArduinoJson (Benoit Blanchon)
// I2S Pins → MAX98357: BCLK=5, LRC/WS=4, DOUT=7

#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <driver/i2s_std.h>
#include <esp_random.h>
#include <mbedtls/base64.h>

// ─── Config
// ───────────────────────────────────────────────────────────────────
static const char *GEMINI_API_KEY =
    "AIzaS__________________________________IKw";
static const char *GEMINI_MODEL = "gemini-3.1-flash-live-preview";
static const char *GEMINI_HOST = "generativelanguage.googleapis.com";
static const int GEMINI_PORT = 443;
// static const char *TEST_PROMPT = "How is the weather in Perth today?";
static const char *TEST_PROMPT = "Thời tiết hôm nay ở Perth thế nào?";

// ─── I2S (MAX98357)
// ───────────────────────────────────────────────────────────
#define I2S_BCLK_PIN 5
#define I2S_LRC_PIN 4
#define I2S_DOUT_PIN 7
#define GEMINI_I2S_PORT I2S_NUM_1 // NUM_0 is occupied by Audio.h
#define AUDIO_SAMPLE_RATE 24000

// ─── State
// ────────────────────────────────────────────────────────────────────
enum GeminiState { GS_IDLE, GS_RUNNING, GS_DONE, GS_ERROR };

static WiFiClientSecure gClient;
static GeminiState gState = GS_IDLE;
static i2s_chan_handle_t gI2sTxHandle = NULL;
static bool gSetupComplete = false;
static bool gTextSent = false;

// Buffer to accumulate all PCM audio before playing → prevents I2S underrun
// between WS frames
static uint8_t *gAudioBuf = nullptr;
static size_t gAudioLen = 0;
static size_t gAudioCap = 0;

// ─── Forward declarations
// ─────────────────────────────────────────────────────
static bool wsConnect();
static void wsSendText(const String &json);
static void processFrames();
static void appendAudio(const uint8_t *data, size_t len);
static void playAllAudio();
static void i2sInit();
static void i2sDeinit();
static void playPCMChunk(const uint8_t *data, size_t len);

// ─────────────────────────────────────────────────────────────────────────────
// Public API (called from main.ino)
// ─────────────────────────────────────────────────────────────────────────────

void geminiTestSetup() {
  Serial.println("[GEMINI] Init I2S...");
  i2sInit();

  if (!wsConnect()) {
    Serial.println("[GEMINI] WS connect FAILED");
    gState = GS_ERROR;
    return;
  }

  // Send setup config right after handshake
  // Raw WS API: top-level key = "setup" (not "config")
  // responseModalities is inside generationConfig
  StaticJsonDocument<768> doc;
  JsonObject setup = doc.createNestedObject("setup");
  setup["model"] = String("models/") + GEMINI_MODEL;

  // --- SYSTEM INSTRUCTION ---
  JsonObject systemInstruction = setup.createNestedObject("system_instruction");
  JsonArray siParts = systemInstruction.createNestedArray("parts");
  JsonObject siTextPart = siParts.createNestedObject();
  // Vietnamese sample instruction
  // siTextPart["text"] = "QUY TẮC BẮT BUỘC: \n"
  //                    "1. Chỉ trả lời thông tin được hỏi. \n"
  //                    "2. TUYỆT ĐỐI KHÔNG hỏi lại, không gợi ý, không chào hỏi
  //                    dư thừa. \n" "3. Sau khi đưa ra thông tin, kết thúc câu
  //                    trả lời ngay lập tức. \n" "4. Trả lời bằng giọng miền
  //                    Nam Việt Nam.";
  siTextPart["text"] =
      "MANDATORY RULES: \n"
      "1. Only answer the questions asked for information. \n"
      "2. Absolutely do not ask follow-up questions. \n"
      "3. After providing the information, end the answer immediately. \n";
  // -------------------------------------

  // Google Search grounding — camelCase per proto3 JSON encoding
  JsonArray tools = setup.createNestedArray("tools");
  tools.createNestedObject().createNestedObject("googleSearch");

  // Generation Config including AUDIO modality and Aoede voice
  JsonObject genConfig = setup.createNestedObject("generationConfig");
  genConfig.createNestedArray("responseModalities").add("AUDIO");
  genConfig.createNestedObject("speechConfig")
      .createNestedObject("voiceConfig")
      .createNestedObject("prebuiltVoiceConfig")["voiceName"] = "Aoede";

  String json;
  serializeJson(doc, json);
  Serial.printf("[GEMINI] Setup: %s\n", json.c_str());
  wsSendText(json);

  gState = GS_RUNNING;
}

void geminiTestLoop() {
  if (gState == GS_DONE || gState == GS_ERROR || gState == GS_IDLE)
    return;

  if (!gClient.connected()) {
    Serial.println("[GEMINI] Connection lost");
    gState = GS_ERROR;
    return;
  }

  processFrames();

  // After setupComplete → send text prompt once
  if (gSetupComplete && !gTextSent) {
    StaticJsonDocument<256> doc;
    doc.createNestedObject("realtimeInput")["text"] = TEST_PROMPT;
    String json;
    serializeJson(doc, json);
    Serial.printf("[GEMINI] Prompt: %s\n", json.c_str());
    wsSendText(json);
    gTextSent = true;
    Serial.println("[GEMINI] Waiting for audio...");
  }

  if (gState == GS_DONE) {
    gClient.stop();
    i2sDeinit();
    Serial.println("[GEMINI] Done!");
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// WebSocket handshake (RFC 6455 over TLS)
// ─────────────────────────────────────────────────────────────────────────────

static bool wsConnect() {
  gClient.setInsecure(); // Skip cert verification for test
  Serial.printf("[GEMINI] TCP → %s:%d\n", GEMINI_HOST, GEMINI_PORT);
  if (!gClient.connect(GEMINI_HOST, GEMINI_PORT)) {
    Serial.println("[GEMINI] TCP FAILED");
    return false;
  }
  Serial.println("[GEMINI] TCP OK. HTTP Upgrade...");

  String path = "/ws/"
                "google.ai.generativelanguage.v1beta.GenerativeService."
                "BidiGenerateContent?key=";
  path += GEMINI_API_KEY;

  // HTTP/1.1 WebSocket upgrade request
  gClient.print("GET ");
  gClient.print(path);
  gClient.println(" HTTP/1.1");
  gClient.print("Host: ");
  gClient.println(GEMINI_HOST);
  gClient.println("Upgrade: websocket");
  gClient.println("Connection: Upgrade");
  // Sec-WebSocket-Key: base64 of 16 random bytes (using fixed string for test)
  gClient.println("Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==");
  gClient.println("Sec-WebSocket-Version: 13");
  gClient.println(); // end of headers

  // Waiting for response
  unsigned long t = millis();
  while (!gClient.available() && millis() - t < 10000)
    delay(10);

  String status = gClient.readStringUntil('\n');
  status.trim();
  Serial.printf("[GEMINI] HTTP: %s\n", status.c_str());

  if (!status.startsWith("HTTP/1.1 101")) {
    // Dump entire response for debugging
    t = millis();
    while (gClient.connected() && millis() - t < 3000) {
      if (!gClient.available()) {
        delay(1);
        continue;
      }
      String line = gClient.readStringUntil('\n');
      line.trim();
      Serial.printf("  %s\n", line.c_str());
      if (line.length() == 0)
        break;
    }
    String body = gClient.readString();
    if (body.length())
      Serial.printf("[GEMINI] Body: %.500s\n", body.c_str());
    return false;
  }

  // Skip remaining HTTP headers
  t = millis();
  while (gClient.connected() && millis() - t < 3000) {
    if (!gClient.available()) {
      delay(1);
      continue;
    }
    String line = gClient.readStringUntil('\n');
    line.trim();
    if (line.length() == 0)
      break;
  }

  Serial.println("[GEMINI] WS handshake OK!");
  return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Send WebSocket text frame (client→server MUST have mask per RFC 6455)
// ─────────────────────────────────────────────────────────────────────────────

static void wsSendText(const String &json) {
  size_t len = json.length();
  uint32_t maskRaw = esp_random();
  uint8_t mask[4];
  memcpy(mask, &maskRaw, 4);

  // Header
  uint8_t hdr[14];
  int hLen = 0;
  hdr[hLen++] = 0x81; // FIN=1, opcode=TEXT(1)
  if (len < 126) {
    hdr[hLen++] = 0x80 | (uint8_t)len;
  } else if (len < 65536) {
    hdr[hLen++] = 0x80 | 126;
    hdr[hLen++] = (len >> 8) & 0xFF;
    hdr[hLen++] = len & 0xFF;
  } else {
    hdr[hLen++] = 0x80 | 127;
    for (int i = 7; i >= 0; i--)
      hdr[hLen++] = (len >> (8 * i)) & 0xFF;
  }
  hdr[hLen++] = mask[0];
  hdr[hLen++] = mask[1];
  hdr[hLen++] = mask[2];
  hdr[hLen++] = mask[3];
  gClient.write(hdr, hLen);

  // Payload (XOR with mask)
  const char *data = json.c_str();
  for (size_t i = 0; i < len; i++) {
    gClient.write((uint8_t)(data[i] ^ mask[i & 3]));
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Read payload streaming — SSL data arrives in chunks, cannot rely on
// available() Returns false if timeout or disconnect
// ─────────────────────────────────────────────────────────────────────────────

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
    int avail = gClient.available();
    if (avail > 0) {
      size_t toRead = min((size_t)avail, (size_t)(payloadLen - bytesRead));
      int n = gClient.read(*outBuf + bytesRead, toRead);
      if (n > 0) {
        bytesRead += n;
        lastRecv = millis();
      }
    } else if (!gClient.connected()) {
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
  (*outBuf)[payloadLen] = 0; // null-terminate for JSON parsing
  *outLen = (size_t)payloadLen;
  return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Read and process all available WS frames
// ─────────────────────────────────────────────────────────────────────────────

static void processFrames() {
  while (gClient.connected() && gClient.available() >= 2) {

    uint8_t b1 = gClient.read();
    uint8_t b2 = gClient.read();
    uint8_t opcode = b1 & 0x0F;
    uint64_t payloadLen = (uint64_t)(b2 & 0x7F);

    // Extended length
    if (payloadLen == 126) {
      unsigned long t = millis();
      while (gClient.available() < 2 && millis() - t < 2000)
        delay(1);
      payloadLen = ((uint16_t)gClient.read() << 8) | gClient.read();
    } else if (payloadLen == 127) {
      unsigned long t = millis();
      while (gClient.available() < 8 && millis() - t < 2000)
        delay(1);
      payloadLen = 0;
      for (int i = 0; i < 8; i++)
        payloadLen = (payloadLen << 8) | gClient.read();
    }

    // CLOSE frame → log code+reason
    if (opcode == 0x08) {
      uint16_t code = 0;
      String reason = "";
      if (payloadLen >= 2) {
        unsigned long t = millis();
        while ((uint64_t)gClient.available() < payloadLen &&
               millis() - t < 2000)
          delay(1);
        code = ((uint16_t)gClient.read() << 8) | gClient.read();
        for (uint64_t i = 2; i < payloadLen && gClient.available(); i++)
          reason += (char)gClient.read();
      }
      Serial.printf("[GEMINI] CLOSE frame: code=%d reason='%s'\n", code,
                    reason.c_str());
      gState = GS_DONE;
      return;
    }

    // PING → PONG
    if (opcode == 0x09) {
      unsigned long t = millis();
      while ((uint64_t)gClient.available() < payloadLen && millis() - t < 1000)
        delay(1);
      uint8_t *pingData =
          payloadLen > 0 ? (uint8_t *)malloc(payloadLen) : nullptr;
      if (pingData) {
        for (uint64_t i = 0; i < payloadLen; i++)
          pingData[i] = gClient.read();
      }
      uint32_t mr = esp_random();
      uint8_t mk[4];
      memcpy(mk, &mr, 4);
      uint8_t pongHdr[6] = {0x8A,  (uint8_t)(0x80 | (uint8_t)payloadLen),
                            mk[0], mk[1],
                            mk[2], mk[3]};
      gClient.write(pongHdr, 6);
      if (pingData) {
        for (uint64_t i = 0; i < payloadLen; i++)
          gClient.write(pingData[i] ^ mk[i & 3]);
        free(pingData);
      }
      continue;
    }

    // Skip frames that are not text/binary/continuation
    if (opcode != 0x01 && opcode != 0x02 && opcode != 0x00) {
      unsigned long t = millis();
      while ((uint64_t)gClient.available() < payloadLen && millis() - t < 1000)
        delay(1);
      for (uint64_t i = 0; i < payloadLen && gClient.available(); i++)
        gClient.read();
      continue;
    }

    if (payloadLen == 0)
      continue;

    // Read payload streaming (SSL data arrives in chunks)
    uint8_t *rawBuf = nullptr;
    size_t rawLen = 0;
    if (!wsReadPayload(payloadLen, &rawBuf, &rawLen)) {
      gState = GS_ERROR;
      return;
    }

    // Log initial content for debugging
    Serial.printf("[GEMINI][FRAME] %llu bytes: %.300s%s\n",
                  (unsigned long long)payloadLen, (char *)rawBuf,
                  payloadLen > 300 ? "..." : "");

    // Parse JSON directly from buffer (avoid copying into String)
    DynamicJsonDocument doc(65536);
    DeserializationError err = deserializeJson(doc, (char *)rawBuf, rawLen);
    free(rawBuf);
    rawBuf = nullptr;
    if (err) {
      Serial.printf("[GEMINI] JSON err: %s\n", err.c_str());
      continue;
    }

    // setupComplete
    if (doc.containsKey("setupComplete")) {
      Serial.println("[GEMINI] setupComplete!");
      gSetupComplete = true;
    }

    // serverContent → audio + turnComplete
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
                  Serial.printf("[GEMINI] Buffering %d PCM bytes\n",
                                (int)outLen);
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
        gState = GS_DONE;
        return;
      }
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Audio Buffering
// ─────────────────────────────────────────────────────────────────────────────

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

// Play entire buffer continuously without gaps between frames
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

// ─────────────────────────────────────────────────────────────────────────────
// I2S (new driver, to avoid conflict with Audio.h using I2S_NUM_0)
// ─────────────────────────────────────────────────────────────────────────────

static void i2sInit() {
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
  err = i2s_channel_init_std_mode(gI2sTxHandle, &std_cfg);
  if (err != ESP_OK) {
    Serial.printf("[GEMINI] i2s_init err: 0x%x\n", err);
    return;
  }

  err = i2s_channel_enable(gI2sTxHandle);
  if (err != ESP_OK) {
    Serial.printf("[GEMINI] i2s_enable err: 0x%x\n", err);
    return;
  }

  Serial.println("[GEMINI] I2S OK (24kHz, 16-bit, mono, I2S_NUM_1)");
}

static void i2sDeinit() {
  if (gI2sTxHandle) {
    i2s_channel_disable(gI2sTxHandle);
    i2s_del_channel(gI2sTxHandle);
    gI2sTxHandle = NULL;
  }
}

static void playPCMChunk(const uint8_t *data, size_t len) {
  if (!gI2sTxHandle || len == 0)
    return;
  size_t written = 0;
  i2s_channel_write(gI2sTxHandle, data, len, &written, portMAX_DELAY);
}
