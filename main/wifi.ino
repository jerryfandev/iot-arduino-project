#include <HTTPClient.h>
#include <WiFi.h>
#include <time.h>

// Fill these in (or replace with your own credential loading method).
static const char *WIFI_SSID = "Jerry's iPhone";
static const char *WIFI_PASSWORD = "1122334455";
static const char *DEVICE_TZ = "AWST-8"; // POSIX TZ for Perth/GMT+8.

void checkInternet() {
  Serial.print("Checking Internet connection...");
  HTTPClient http;

  // Try to ping a reliable address (Google) with a short timeout
  http.begin("http://www.google.com");
  http.setTimeout(3000); // 3 seconds

  int httpCode = http.GET();

  if (httpCode > 0) {
    Serial.println(" OK! Internet is available.");
  } else {
    Serial.print(" FAILED. Error: ");
    Serial.println(http.errorToString(httpCode).c_str());
  }

  http.end();
}

void wifiConnect() {
  if (!Serial) {
    Serial.begin(115200);
    delay(50);
  }

  // 1. Switch WiFi mode to Station (connect to router)
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(1000);

  // By default, it runs near ~19.5dBm which draws excessive current
  WiFi.setTxPower(WIFI_POWER_8_5dBm);

  // 2. Begin connection
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi...");

  // 3. Wait until connection is established
  while (WiFi.status() != WL_CONNECTED) {
    delay(2000);
    Serial.print(".");
  }

  // 4. Print information upon successful connection
  Serial.print("\nSSID: ");
  Serial.println(WiFi.SSID());

  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  long rssi = WiFi.RSSI();
  Serial.print("Signal strength (RSSI): ");
  Serial.print(rssi);
  Serial.println(" dBm");

  // Check Internet connection immediately after connecting to WiFi
  Serial.println("-------------------------");
  checkInternet();
  Serial.println("-------------------------");
}

void timeSetup() {
  setenv("TZ", DEVICE_TZ, 1);
  tzset();
  configTzTime(DEVICE_TZ, "pool.ntp.org", "time.google.com",
               "time.nist.gov");

  Serial.print("Syncing time for Perth/GMT+8");
  struct tm timeInfo;
  for (int i = 0; i < 15; i++) {
    if (getLocalTime(&timeInfo, 1000)) {
      char buf[32];
      strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", &timeInfo);
      Serial.print("\nTime synced: ");
      Serial.println(buf);
      return;
    }
    Serial.print(".");
  }

  Serial.println("\nTime sync not ready yet; scheduled HH:mm commands will wait "
                 "for NTP.");
}
