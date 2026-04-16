// bbc_stream_test.ino
// BBC World Service live stream test using ESP32-audioI2S library
// Stream URL: https://stream.live.vc.bbcmedia.co.uk/bbc_world_service
//
// Required library: ESP32-audioI2S by schreibfaul1
//   Arduino IDE → Library Manager → search "ESP32-audioI2S" → Install
//   Or: https://github.com/schreibfaul1/ESP32-audioI2S
//
// I2S Pins → MAX98357: BCLK=5, LRC/WS=4, DOUT=7

#include <Audio.h>

// ─── I2S Pins (MAX98357) ─────────────────────────────────────────────────────
#define BBC_I2S_BCLK 5
#define BBC_I2S_LRC 4
#define BBC_I2S_DOUT 7

// ─── Stream URL
// ───────────────────────────────────────────────────────────────
static const char *BBC_STREAM_URL =
    "https://stream.live.vc.bbcmedia.co.uk/bbc_world_service";

// ─── Audio object
// ─────────────────────────────────────────────────────────────
static Audio gBbcAudio;
static bool gBbcStarted = false;

// ─────────────────────────────────────────────────────────────────────────────
// Public API (called from main.ino)
// ─────────────────────────────────────────────────────────────────────────────

void bbcStreamSetup() {
  Serial.println("[BBC] Initialising BBC World Service stream...");
  Serial.printf("[BBC] URL: %s\n", BBC_STREAM_URL);

  gBbcAudio.setPinout(BBC_I2S_BCLK, BBC_I2S_LRC, BBC_I2S_DOUT);
  gBbcAudio.setVolume(15); // 0–21

  bool ok = gBbcAudio.connecttohost(BBC_STREAM_URL);
  if (ok) {
    Serial.println("[BBC] connecttohost() OK — buffering...");
    gBbcStarted = true;
  } else {
    Serial.println("[BBC] connecttohost() FAILED");
    gBbcStarted = false;
  }
}

void bbcStreamLoop() {
  if (gBbcStarted) {
    gBbcAudio.loop();
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Optional callbacks — ESP32-audioI2S will call these automatically
// ─────────────────────────────────────────────────────────────────────────────

void audio_info(const char *info) { Serial.printf("[BBC][INFO] %s\n", info); }

void audio_id3data(const char *info) { Serial.printf("[BBC][ID3] %s\n", info); }

void audio_eof_mp3(const char *info) { Serial.printf("[BBC][EOF] %s\n", info); }

void audio_showstation(const char *info) {
  Serial.printf("[BBC][STATION] %s\n", info);
}

void audio_showstreamtitle(const char *info) {
  Serial.printf("[BBC][TITLE] %s\n", info);
}

void audio_bitrate(const char *info) {
  Serial.printf("[BBC][BITRATE] %s\n", info);
}

void audio_commercial(const char *info) {
  Serial.printf("[BBC][COMMERCIAL] %s\n", info);
}

void audio_icyurl(const char *info) {
  Serial.printf("[BBC][ICY-URL] %s\n", info);
}

void audio_lasthost(const char *info) {
  Serial.printf("[BBC][LAST-HOST] %s\n", info);
}
