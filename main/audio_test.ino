#include <WiFi.h>
#include "Audio.h"

// I2S pin mapping for MAX98357
#define I2S_LRC       45
#define I2S_BCLK      47
#define I2S_DOUT      21

static Audio gAudio;
static bool gAudioStarted = false;
static unsigned long gLastConnectAttemptMs = 0;
static const unsigned long kConnectRetryMs = 5000;

static const char* kTestStreamUrl = "http://stream.zeno.fm/0r0xa792kwzuv";

void audioTestSetup() {
  // Configure I2S pins for MAX98357
  gAudio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  gAudio.setVolume(12); // 0..21 (depends on library build)
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