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
#include <ESP_SR.h>

extern bool gLightOn;

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
    break;

  case SR_EVENT_WAKEWORD_CHANNEL:
    Serial.printf("  Wake word verified on channel %d\n", command_id);
    ESP_SR.setMode(SR_MODE_COMMAND);
    break;

  case SR_EVENT_TIMEOUT:
    Serial.println("  No local command heard -- timeout.");

    // TODO: Check voice activity here

    Serial.println("  Returning to wake-word listening...");
    Serial.println("----------------------------------------");
    ESP_SR.setMode(SR_MODE_WAKEWORD);
    break;

  case SR_EVENT_COMMAND:
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
      gLightOn = true;
      Serial.println("     Action     : Light turned ON via Voice");
    } else if (command_id == SR_CMD_LIGHT_OFF) {
      gLightOn = false;
      Serial.println("     Action     : Light turned OFF via Voice");
    }

    Serial.println("----------------------------------------");

    // Stay in command mode for more commands (until timeout)
    ESP_SR.setMode(SR_MODE_COMMAND);
    break;

  default:
    Serial.printf("  [SR] Unknown event: %d\n", event);
    break;
  }
}

// ── Setup ──────────────────────────────────────────────────────
void commandsSetup() {

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
