# iot-arduino-project
IoT Smart LED Project

PROJECT: ESP32-S3 IoT Demo (Lonely Binary TinkerBlock)

BOARD & TOOLCHAIN SETUP
- Install Arduino IDE 2.x (see official Arduino website).
- Add ESP32 core using the Espressif package URL from the Lonely Binary guide:
  https://lonelybinary.com/blogs/tinkerblock-esp32-s3-starter-kit/01_4_arduino_ide_setup_guide

- In Arduino IDE, select:
  - File → Preferences, in Additional Board Manager URLs, add this URL: https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
  - Go to Tools → Board → Boards Manager, type "esp32", install "ESP32 by Espressif Systems"
  - Board: **ESP32S3 Dev Module**
  - PSRAM: OPI PSRAM
  - Flash Size: 16MB (128Mb)
  - Flash Mode: QIO 80MHz
  - Partition Scheme: 16M Flash (3MB APP/9.9MB FATFS)
  - USB CDC On Boot: Enabled (if using USB for upload/Serial)
  - Upload Mode: UART0 / Hardware CDC
  - USB Mode: Hardware CDC and JTAG

SERIAL MONITOR
- Baud rate: 115200

ONBOARD RGB LED (IO48)
- The onboard RGB LED on IO48 is treated as a NeoPixel / WS2812-style addressable LED.
- Required Arduino library:
  - Adafruit NeoPixel (by Adafruit)
    - Install via: Sketch → Include Library → Manage Libraries… → search "Adafruit NeoPixel" → Install

LIBRARIES USED BY THIS PROJECT (CURRENT)
- Adafruit NeoPixel

NOTES
- If compilation fails with "Adafruit_NeoPixel.h: No such file or directory", install the Adafruit NeoPixel library as described above.
- If upload fails, double-check Board, Port, and USB CDC settings against the Lonely Binary setup guide.