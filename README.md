# ESP32-S3 Gemini Live & IoT Project

This project demonstrated an integrated IoT platform using the ESP32-S3 (Lonely Binary TinkerBlock) that features real-time AI interaction via the **Gemini 3.1 Flash Live API**, MQTT remote control, and I2S audio output.

## Features
- **Gemini 3.1 Flash Live API**: Real-time bidirectional communication via WebSocket (WiFiClientSecure).
  - Receives 24kHz raw PCM 16-bit mono audio.
  - Supports Google Search grounding for real-time information retrieval.
- **MQTT Integration**: Remote monitoring and control using the `iotsmartlight.space` broker.
- **I2S Audio Output**: High-quality audio streaming to a MAX98357A I2S amplifier.
- **Onboard RGB LED**: Status indication using the WS2812B (NeoPixel) at GPIO 48.
- **WiFi Connectivity**: Robust network management for cloud services.

## Hardware Components
- **Microcontroller**: ESP32-S3 (Lonely Binary TinkerBlock)
- **Audio Amplifier**: MAX98357 I2S DAC/Amplifier
- **Output**: 8 Ohm / 3W Speaker
- **Display/LED**: Onboard WS2812B NeoPixel

## Pin Configuration
| Component | Pin (GPIO) | Function |
| :--- | :---: | :--- |
| **I2S BCLK** | 5 | Bit Clock Line |
| **I2S LRC/WS** | 4 | Word Select / Left-Right Clock |
| **I2S DOUT** | 7 | Data Out Line |
| **Onboard RGB** | 48 | Web-controlled NeoPixel |

## Board & Toolchain Setup
1.  **Install Arduino IDE 2.x**.
2.  **ESP32 Core Setup**:
    - Add the following URL to *File → Preferences → Additional Board Manager URLs*:
      `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
    - Go to *Tools → Board → Boards Manager*, search for "esp32", and install **ESP32 by Espressif Systems**.
3.  **Arduino IDE Settings**:
    - **Board**: ESP32S3 Dev Module
    - **USB CDC On Boot**: Enabled (for Serial output)
    - **Flash Size**: 16MB
    - **PSRAM**: OPI PSRAM
    - **Partition Scheme**: 16M Flash (3MB APP/9.9MB FATFS)

## Required Libraries
Install the following libraries via the Arduino IDE Library Manager (*Sketch → Include Library → Manage Libraries...*):

1.  **ArduinoJson** (by Benoit Blanchon)
    - Essential for processing Gemini Live API WebSocket messages and MQTT payloads.
2.  **Adafruit NeoPixel** (by Adafruit)
    - Used to control the onboard addressable LED on GPIO48.
3.  **PubSubClient** (by Nick O'Leary)
    - Handles MQTT connectivity to the broker.
4.  **ESP32-audioI2S** (by Wolle)
    - Provides the `Audio.h` library used for general audio testing (`audio_test.ino`).

## Usage
- **WiFi Configuration**: Set your SSID and Password in `wifi.ino`.
- **Gemini API Key**: Add your valid Google AI API key to `gemini.ino`.
- **Serial Monitor**: Set the baud rate to **115200** to view project logs and Gemini responses.

## Troubleshooting
- **I2S Conflict**: The project uses `I2S_NUM_1` for Gemini audio to avoid conflicts with the `Audio.h` library which occupies `I2S_NUM_0` by default.
- **No Available Channel**: Ensure `Audio.h` is not attempting to initialize the same I2S port as the Gemini implementation.
- **WebSocket Disconnection (1007)**: If the Gemini API returns a 1007 error, check the JSON structure of the `setup` message (ensure camelCase field names).