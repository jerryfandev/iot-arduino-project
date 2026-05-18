# ESP32-S3 Smart Light, Voice, and MQTT Project

This project runs on an ESP32-S3 and combines a WiFi/MQTT smart light, local ESP-SR voice commands, Gemini Live fallback audio, scheduled timers, and frontend state synchronization.

The main firmware lives in `main/`:

- `main.ino`: LED output, brightness fading, ambient light sensor handling, and setup loop.
- `mqtt.ino`: MQTT broker connection, frontend command handling, retained state publishing, and ON/OFF timers.
- `commands.ino`: ESP-SR wake word and local light voice commands, plus Gemini Live WebSocket handling.
- `wifi.ino`: WiFi connection, internet check, and Perth/AWST NTP setup.

## Features

- MQTT light control through `iotsmartlight.space:1883`.
- Retained MQTT light state publishing so the frontend toggle can stay in sync after voice commands, timers, reconnects, and direct frontend actions.
- OFF timer by duration in seconds.
- ON timer by local `HH:mm` clock time in Perth/AWST.
- Retained timer state topics that expose the absolute trigger time for frontend countdowns and schedule displays.
- Optional ambient light sensor mode that dims the LEDs when the environment is brighter.
- Local offline voice commands using ESP-SR:
  - Wake word: `Hi ESP`
  - Commands: `Turn on the light`, `Switch on the light`, `Turn off the light`, `Switch off the light`, `Go dark`
- Gemini Live fallback for utterances that are not matched as local ESP-SR commands.
- Smooth LED brightness transitions.

## Hardware

| Component | GPIO | Notes |
| --- | ---: | --- |
| WS2812/NeoPixel LED strip | 2 | `NUM_LEDS` is 5 in `main.ino`. |
| Ambient light sensor | 6 | Read with `analogRead()` when sensor mode is enabled. |
| INMP441 SD | 10 | I2S microphone data input. |
| INMP441 WS | 11 | I2S word select / LRCLK. |
| INMP441 SCK | 12 | I2S bit clock. |
| MAX98357 BCLK | 5 | Gemini audio output. |
| MAX98357 LRC/WS | 4 | Gemini audio output. |
| MAX98357 DIN | 7 | Gemini audio output. |

INMP441 wiring notes:

- VDD -> 3.3V
- GND -> GND
- L/R -> GND for left-channel data

## MQTT Topics

Broker:

```text
iotsmartlight.space:1883
```

### Light Command and State

Topic:

```text
home/livingroom/light
```

Accepted command payloads:

```text
ON
OFF
```

The device also publishes retained `ON` or `OFF` to the same topic whenever the real light state changes. This is how the frontend toggle should initialize and resync after voice commands, timer events, and device reconnects.

### Ambient Sensor Mode

Topic:

```text
home/livingroom/light-sensor
```

Accepted payloads:

```text
ON
OFF
```

When enabled and the light is ON, the firmware maps the ambient sensor value from GPIO 6 to LED brightness. Brighter ambient readings produce dimmer LEDs.

### OFF Timer Command

Topic:

```text
home/livingroom/light/off-timer
```

Payload to turn the light OFF after a duration:

```json
{"duration":300,"at_time":null}
```

Payload to cancel the OFF timer:

```json
{"duration":null,"at_time":null}
```

### OFF Timer State

Topic:

```text
home/livingroom/light/off-timer/state
```

The device publishes this as a retained payload whenever the OFF timer is created, cancelled, fired, or republished after reconnect.

Active example:

```json
{
  "active": true,
  "duration": 300,
  "trigger_epoch": 1779081234,
  "trigger_at": "2026-05-18T14:33:54+0800"
}
```

Inactive example:

```json
{"active":false,"duration":null,"trigger_epoch":null,"trigger_at":null}
```

If NTP time is not synced yet, the OFF timer still runs from `millis()`, but `trigger_epoch` and `trigger_at` are published as `null`.

### ON Timer Command

Topic:

```text
home/livingroom/light/on-timer
```

Payload to turn the light ON at a Perth/AWST local time:

```json
{"duration":null,"at_time":"14:30"}
```

Payload to cancel the ON timer:

```json
{"duration":null,"at_time":null}
```

If the requested `HH:mm` has already passed today, the firmware schedules it for the next day.

### ON Timer State

Topic:

```text
home/livingroom/light/on-timer/state
```

The device publishes this as a retained payload whenever the ON timer is created, cancelled, fired, or republished after reconnect.

Active example:

```json
{
  "active": true,
  "at_time": "14:30",
  "trigger_epoch": 1779085800,
  "trigger_at": "2026-05-18T14:30:00+0800"
}
```

Inactive example:

```json
{"active":false,"at_time":null,"trigger_epoch":null,"trigger_at":null}
```

## Frontend Integration Notes

- Publish light toggle commands to `home/livingroom/light`.
- Subscribe to retained `home/livingroom/light` to display the authoritative light state.
- Publish OFF timer commands to `home/livingroom/light/off-timer`.
- Subscribe to retained `home/livingroom/light/off-timer/state` to show the OFF trigger time or countdown.
- Publish ON timer commands to `home/livingroom/light/on-timer`.
- Subscribe to retained `home/livingroom/light/on-timer/state` to show the ON trigger time.
- Treat `active:false` timer state payloads as cleared timers.

## Board and Toolchain Setup

1. Install Arduino IDE 2.x.
2. Add the Espressif boards URL in Arduino IDE preferences:

   ```text
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```

3. Install `ESP32 by Espressif Systems` from Boards Manager.
4. Use these Arduino IDE settings:

   | Setting | Value |
   | --- | --- |
   | Board | ESP32S3 Dev Module |
   | USB CDC On Boot | Enabled |
   | PSRAM | OPI PSRAM |
   | Flash Size | 16MB |
   | Partition Scheme | ESP SR 16M / model-capable partition |
   | Upload Speed | 921600 |

## Required Libraries

Install these libraries through Arduino IDE Library Manager or provide them locally:

- `ESP_SR` for ESP-SR wake word and command recognition.
- `ArduinoJson` for Gemini Live JSON messages.
- `Adafruit NeoPixel` for WS2812/NeoPixel LED control.
- `PubSubClient` for MQTT.

The firmware defines `MQTT_MAX_PACKET_SIZE` as `512` before including `PubSubClient.h` so retained timer JSON payloads fit in MQTT publish packets.

## Configuration

- Set WiFi credentials in `main/wifi.ino`.
- Confirm `DEVICE_TZ` is `AWST-8` for Perth/GMT+8 scheduling.
- Set the Gemini API key and model constants in `main/commands.ino`.
- Use Serial Monitor at `115200` baud.

## Serial Logs

Useful MQTT logs include:

```text
[MQTT] Light => ON
[MQTT] Published light state => ON
[MQTT] Light will turn OFF after 300 seconds at 2026-05-18 14:33:54 AWST
[MQTT] Published OFF timer state => {...}
[MQTT] Light will turn ON at 2026-05-19 15:00:00 AWST
[MQTT] Published ON timer state => {...}
```

## Troubleshooting

- If the frontend toggle is stale, check that it subscribes to retained `home/livingroom/light` and does not only trust local UI state.
- If timer trigger times do not appear, check the `/state` topics and confirm the ESP serial log shows `Published OFF timer state` or `Published ON timer state`.
- If an ON timer is rejected, ensure NTP has synced; scheduled `HH:mm` commands require valid local time.
- If retained timer JSON fails to publish, confirm the firmware includes the `MQTT_MAX_PACKET_SIZE 512` definition before `PubSubClient.h`.
- If Gemini audio conflicts with another audio library, keep Gemini output on `I2S_NUM_1`; some audio libraries use `I2S_NUM_0` by default.
