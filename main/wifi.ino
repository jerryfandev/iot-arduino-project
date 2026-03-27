#include <WiFi.h>

// Fill these in (or replace with your own credential loading method).
static const char* WIFI_SSID = "WiFi-ERVQP";
static const char* WIFI_PASSWORD = "Study406Swag";

static void wifiPrintStatus() {
  Serial.print("WiFi status=");
  Serial.print(static_cast<int>(WiFi.status()));
  Serial.print(" ip=");
  Serial.println(WiFi.isConnected() ? WiFi.localIP().toString() : String("-"));
}

void wifiConnect() {
  if (!Serial) {
    Serial.begin(115200);
    delay(50);
  }

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);

  Serial.print("Connecting to WiFi SSID: ");
  Serial.println(WIFI_SSID);

  // Retry forever until connected.
  uint32_t attempt = 0;
  while (WiFi.status() != WL_CONNECTED) {
    attempt++;
    Serial.print("WiFi connect attempt #");
    Serial.println(attempt);

    WiFi.disconnect(true /* wifioff */);
    delay(200);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    const unsigned long attemptStart = millis();
    const unsigned long attemptWindowMs = 15000;
    while (WiFi.status() != WL_CONNECTED && (millis() - attemptStart) < attemptWindowMs) {
      delay(300);
      Serial.print(".");
    }
    Serial.println();
    wifiPrintStatus();

    if (WiFi.status() != WL_CONNECTED) {
      delay(1000);
    }
  }

  Serial.print("WiFi connected. IP: ");
  Serial.println(WiFi.localIP());
}

