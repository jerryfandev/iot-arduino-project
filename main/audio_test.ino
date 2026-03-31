#include <WiFi.h>
#include "Audio.h"

// I2S → MAX98357: BCLK, LRCLK/WS, DIN (order matches gAudio.setPinout)
// ESP32-S3: avoid GPIO19/20 (native USB), GPIO0 (BOOT). GPIO6 is lightSensorPin in main.ino.
#define I2S_LRC       4
#define I2S_BCLK      5
#define I2S_DOUT      7

static Audio gAudio;
static bool gAudioStarted = false;
static unsigned long gLastConnectAttemptMs = 0;
static const unsigned long kConnectRetryMs = 5000;

// static const char* kTestStreamUrl = "http://stream.zeno.fm/0r0xa792kwzuv";
static const char* kTestStreamUrl = "https://stream.live.vc.bbcmedia.co.uk/bbc_world_service";

void audioTestSetup() {
  // Configure I2S pins for MAX98357
  gAudio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  gAudio.setVolume(18); // 0..21 (depends on library build)
}

void audioTestLoop() {
  // Only start streaming after WiFi is connected.
  if (WiFi.status() != WL_CONNECTED) {
    gAudioStarted = false;
    return;
  }

  if (!gAudioStarted) {
    const unsigned long now = millis();
    if (now - gLastConnectAttemptMs >= kConnectRetryMs) {
      gLastConnectAttemptMs = now;
      gAudioStarted = gAudio.connecttohost(kTestStreamUrl);
      Serial.println(gAudioStarted ? "[AUDIO] Stream started" : "[AUDIO] Failed to start stream, will retry");
    }
    return;
  }

  gAudio.loop();
}