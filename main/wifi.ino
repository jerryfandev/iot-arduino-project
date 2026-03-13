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

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const unsigned long start = millis();
  const unsigned long timeoutMs = 20000;
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi connected. IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi connect timeout (continuing without WiFi).");
    wifiPrintStatus();
  }
}

