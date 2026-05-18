#include <WiFi.h>
#include <PubSubClient.h>
#include <limits.h>
#include <time.h>

// VPS MQTT broker (TCP)
static const char* MQTT_HOST = "iotsmartlight.space";
static const uint16_t MQTT_PORT = 1883;

// Topic must match Web dashboard
static const char* MQTT_TOPIC = "home/livingroom/light";
static const char* MQTT_TOPIC_SENSOR = "home/livingroom/light-sensor";
// Frontend timer payloads:
// off-timer: {"duration":300,"at_time":null} where duration is seconds.
// on-timer:  {"duration":null,"at_time":"14:30"} in Perth local time.
static const char* MQTT_TOPIC_OFF_TIMER = "home/livingroom/light/off-timer";
static const char* MQTT_TOPIC_ON_TIMER = "home/livingroom/light/on-timer";

static WiFiClient gNetClient;
static PubSubClient gMqtt(gNetClient);

extern bool gLightOn;
extern bool gNeedsUpdate;
extern bool gLightSensorEnabled;

static bool gOffAfterActive = false;
static unsigned long gOffAfterAtMillis = 0;
static bool gOnAtActive = false;
static time_t gOnAtEpoch = 0;

static bool isSyncedTime() {
  return time(nullptr) > 1700000000; // Well past ESP32's 1970 default.
}

static bool parseUnsignedLong(const String& text, unsigned long& out) {
  if (text.length() == 0) return false;
  for (unsigned int i = 0; i < text.length(); i++) {
    if (!isDigit(text.charAt(i))) return false;
  }

  char* end = nullptr;
  unsigned long value = strtoul(text.c_str(), &end, 10);
  if (*end != '\0') return false;

  out = value;
  return true;
}

static bool parseClock(const String& msg, uint8_t& hour, uint8_t& minute) {
  String text = msg;
  text.trim();

  int colon = text.indexOf(':');
  if (colon <= 0 || colon != text.lastIndexOf(':')) return false;

  unsigned long parsedHour = 0;
  unsigned long parsedMinute = 0;
  if (!parseUnsignedLong(text.substring(0, colon), parsedHour) ||
      !parseUnsignedLong(text.substring(colon + 1), parsedMinute)) {
    return false;
  }

  if (parsedHour > 23 || parsedMinute > 59) return false;

  hour = static_cast<uint8_t>(parsedHour);
  minute = static_cast<uint8_t>(parsedMinute);
  return true;
}

static time_t nextLocalClockEpoch(uint8_t hour, uint8_t minute) {
  if (!isSyncedTime()) return 0;

  time_t now = time(nullptr);
  struct tm localTime;
  localtime_r(&now, &localTime);
  localTime.tm_hour = hour;
  localTime.tm_min = minute;
  localTime.tm_sec = 0;

  time_t target = mktime(&localTime);
  if (target <= now) {
    target += 24 * 60 * 60;
  }

  return target;
}

static void printLocalSchedule(time_t target) {
  struct tm localTime;
  localtime_r(&target, &localTime);

  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", &localTime);
  Serial.println(buf);
}

static void clearOffAfterTimer(const char* reason) {
  if (!gOffAfterActive) return;

  gOffAfterActive = false;
  Serial.print("[MQTT] OFF timer cancelled");
  if (reason) {
    Serial.print(": ");
    Serial.print(reason);
  }
  Serial.println();
}

static void clearOnAtTimer(const char* reason) {
  if (!gOnAtActive) return;

  gOnAtActive = false;
  gOnAtEpoch = 0;
  Serial.print("[MQTT] ON schedule cancelled");
  if (reason) {
    Serial.print(": ");
    Serial.print(reason);
  }
  Serial.println();
}

static bool readJsonValue(const String& msg, const char* key, String& value) {
  String quotedKey = String("\"") + key + "\"";
  int keyPos = msg.indexOf(quotedKey);
  if (keyPos < 0) return false;

  int colonPos = msg.indexOf(':', keyPos + quotedKey.length());
  if (colonPos < 0) return false;

  int start = colonPos + 1;
  while (start < msg.length() && isSpace(msg.charAt(start))) {
    start++;
  }

  if (start >= msg.length()) return false;

  if (msg.charAt(start) == '"') {
    int end = msg.indexOf('"', start + 1);
    if (end < 0) return false;
    value = msg.substring(start + 1, end);
    value.trim();
    return true;
  }

  int end = start;
  while (end < msg.length() && msg.charAt(end) != ',' &&
         msg.charAt(end) != '}') {
    end++;
  }

  value = msg.substring(start, end);
  value.trim();
  return true;
}

static bool isJsonNull(const String& value) {
  String text = value;
  text.trim();
  text.toUpperCase();
  return text == "NULL";
}

static bool readDurationSeconds(const String& value, unsigned long& seconds) {
  String durationText = value;
  durationText.trim();
  return parseUnsignedLong(durationText, seconds);
}

static void handleOffTimerPayload(const String& msg) {
  Serial.print("[MQTT] OFF timer object received: ");
  Serial.println(msg);

  String duration;
  if (!readJsonValue(msg, "duration", duration)) {
    Serial.println("[MQTT] OFF timer missing duration");
    return;
  }

  if (isJsonNull(duration)) {
    clearOffAfterTimer("duration is null");
    return;
  }

  unsigned long durationSeconds = 0;
  if (!readDurationSeconds(duration, durationSeconds) || durationSeconds == 0 ||
      durationSeconds > ULONG_MAX / 1000UL) {
    Serial.print("[MQTT] Invalid OFF timer duration: ");
    Serial.println(duration);
    return;
  }

  gOffAfterAtMillis = millis() + durationSeconds * 1000UL;
  gOffAfterActive = true;
  Serial.print("[MQTT] Light will turn OFF after ");
  Serial.print(durationSeconds);
  Serial.println(" seconds");
}

static void handleOnTimerPayload(const String& msg) {
  Serial.print("[MQTT] ON timer object received: ");
  Serial.println(msg);

  String atTime;
  if (!readJsonValue(msg, "at_time", atTime)) {
    Serial.println("[MQTT] ON timer missing at_time");
    return;
  }

  if (isJsonNull(atTime)) {
    clearOnAtTimer("at_time is null");
    return;
  }

  uint8_t hour = 0;
  uint8_t minute = 0;
  if (!parseClock(atTime, hour, minute)) {
    Serial.print("[MQTT] Invalid ON timer at_time: ");
    Serial.println(atTime);
    return;
  }

  time_t target = nextLocalClockEpoch(hour, minute);
  if (target == 0) {
    Serial.println("[MQTT] Cannot schedule ON until NTP time is synced");
    return;
  }

  gOnAtEpoch = target;
  gOnAtActive = true;
  Serial.print("[MQTT] Light will turn ON at ");
  printLocalSchedule(target);
}

static void mqttOnMessage(char* topic, byte* payload, unsigned int length) {
  if (!topic) return;

  String topicStr = String(topic);
  String msg;
  msg.reserve(length + 1);
  for (unsigned int i = 0; i < length; i++) {
    msg += static_cast<char>(payload[i]);
  }
  msg.trim();

  String commandMsg = msg;
  commandMsg.toUpperCase();

  if (topicStr == MQTT_TOPIC) {
    if (commandMsg == "ON") {
      if (!gLightOn) {
        gLightOn = true;
        gNeedsUpdate = true;
        Serial.println("[MQTT] Light => ON");
      }
    } else if (commandMsg == "OFF") {
      if (gLightOn) {
        gLightOn = false;
        gNeedsUpdate = true;
        Serial.println("[MQTT] Light => OFF");
      }
    }
  } else if (topicStr == MQTT_TOPIC_SENSOR) {
    if (commandMsg == "ON") {
      if (!gLightSensorEnabled) {
        gLightSensorEnabled = true;
        Serial.println("[MQTT] Light Sensor => ON");
      }
    } else if (commandMsg == "OFF") {
      if (gLightSensorEnabled) {
        gLightSensorEnabled = false;
        Serial.println("[MQTT] Light Sensor => OFF");
      }
    }
  } else if (topicStr == MQTT_TOPIC_OFF_TIMER) {
    handleOffTimerPayload(msg);
  } else if (topicStr == MQTT_TOPIC_ON_TIMER) {
    handleOnTimerPayload(msg);
  } else {
    Serial.print("[MQTT] Ignored topic/payload: ");
    Serial.print(topicStr);
    Serial.print(" ");
    Serial.println(commandMsg);
  }
}

static void mqttEnsureConnected() {
  if (gMqtt.connected()) return;
  if (WiFi.status() != WL_CONNECTED) return;

  const String clientId =
    String("esp32_") + String((uint32_t)ESP.getEfuseMac(), HEX) + "_" + String(millis());

  Serial.print("[MQTT] Connecting to ");
  Serial.print(MQTT_HOST);
  Serial.print(":");
  Serial.print(MQTT_PORT);
  Serial.print(" as ");
  Serial.println(clientId);

  // No username/password (adjust if your broker requires auth)
  if (gMqtt.connect(clientId.c_str())) {
    Serial.println("[MQTT] Connected");
    gMqtt.subscribe(MQTT_TOPIC);
    gMqtt.subscribe(MQTT_TOPIC_SENSOR);
    gMqtt.subscribe(MQTT_TOPIC_OFF_TIMER);
    gMqtt.subscribe(MQTT_TOPIC_ON_TIMER);
    return;
  }

  Serial.print("[MQTT] Connect failed, rc=");
  Serial.print(gMqtt.state());
  Serial.println(" (will retry later)");
}

void mqttSetup() {
  gMqtt.setServer(MQTT_HOST, MQTT_PORT);
  gMqtt.setCallback(mqttOnMessage);
  mqttEnsureConnected();
}

void mqttLoop() {
  mqttEnsureConnected();
  if (gMqtt.connected()) {
    gMqtt.loop();
  }

  if (gOffAfterActive &&
      static_cast<long>(millis() - gOffAfterAtMillis) >= 0) {
    gOffAfterActive = false;
    gLightOn = false;
    gNeedsUpdate = true;
    Serial.println("[MQTT] OFF timer fired");
  }

  if (gOnAtActive && isSyncedTime() && time(nullptr) >= gOnAtEpoch) {
    gOnAtActive = false;
    gOnAtEpoch = 0;
    gLightOn = true;
    gNeedsUpdate = true;
    Serial.println("[MQTT] ON schedule fired");
  }
}
