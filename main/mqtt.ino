#include <WiFi.h>
#include <PubSubClient.h>

// VPS MQTT broker (TCP)
static const char* MQTT_HOST = "iotsmartlight.space";
static const uint16_t MQTT_PORT = 1883;

// Topic must match Web dashboard
static const char* MQTT_TOPIC = "home/livingroom/light";

static WiFiClient gNetClient;
static PubSubClient gMqtt(gNetClient);

extern bool gLightOn;

static void mqttOnMessage(char* topic, byte* payload, unsigned int length) {
  if (!topic) return;
  if (String(topic) != String(MQTT_TOPIC)) return;

  String msg;
  msg.reserve(length + 1);
  for (unsigned int i = 0; i < length; i++) {
    msg += static_cast<char>(payload[i]);
  }
  msg.trim();
  msg.toUpperCase();

  if (msg == "ON") {
    gLightOn = true;
    Serial.println("[MQTT] Light => ON");
  } else if (msg == "OFF") {
    gLightOn = false;
    Serial.println("[MQTT] Light => OFF");
  } else {
    Serial.print("[MQTT] Ignored payload: ");
    Serial.println(msg);
    return;
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
}
