# Waveshare ESP32-S3 Touch AMOLED 1.75" – PlatformIO

This project provides a PlatformIO setup for the Waveshare ESP32-S3 Touch AMOLED 1.75".

The goal of this repository is **not** to demonstrate features or applications, but simply to:

- Bring the board up cleanly in **PlatformIO**
- Initialize the CO5300 AMOLED display
- Enable **LVGL (v8.x)** rendering
- Enable **CST9217 capacitive touch**
- Support **SquareLine Studio–generated UI projects**
- Serve as a stable foundation for future projects

---

## Hardware

- **Board:** Waveshare ESP32-S3 Touch AMOLED 1.75"
- **Display:** 466 × 466 AMOLED (CO5300)
- **Touch Controller:** CST9217
- **Interface:** QSPI (display), I²C (touch)

---

## Software Stack

- **PlatformIO**
- **Arduino framework (ESP32-S3)**
- **LVGL 8.x**
- **Waveshare-compatible BSP + drivers**
- **SquareLine Studio compatibility**

This project intentionally uses the **Waveshare driver and LVGL files directly**, rather than relying on external LVGL display libraries, to avoid artifacts and timing issues.

## Intro video

After waking the board with the BOOT button, the firmware plays
`data/intro.avi` once, scaled 2× to fill the 466 × 466 display, before opening
the existing UI. The AVI contains 233 × 233 MJPEG frames at 25 fps and is
stored in SPIFFS; upload it with `pio run -e waveshare_amoled_175 -t uploadfs`.
The firmware can then be built and uploaded with
`pio run -e waveshare_amoled_175 -t upload`.

The MJPEG frames are decoded in software with a locally tuned TJpgDec build
(RGB565 output, fast Huffman lookup), with the AVI cached in PSRAM to avoid
filesystem reads during playback. Frames are scaled by duplicating each pixel
into 2 × 2 pixels in DMA buffers and sent directly to the panel as RGB565 to
avoid LVGL redraw overhead. Decoding runs on the other ESP32-S3 core while the
previous frame is transferred to the display. The ESP32-S3 has SIMD/vector
instructions for DSP workloads, but TJpgDec does not use them. On the tested
board, playback reached 22.9 fps; per-stage timings and the achieved rate are
reported over Serial.

The display uses QSPI on GPIO 4–7, GPIO 38 (clock), GPIO 12 (chip select),
and GPIO 39 (reset). Touch uses I²C on GPIO 15/14, with reset on GPIO 40 and
interrupt on GPIO 11. The pin assignments and controller selection match
[Waveshare's 1.75" board reference](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75/blob/main/HARDWARE_REFERENCE.md).

## Wi-Fi and automatic OTA updates

Screen4 lets you scan for Wi-Fi networks, select an SSID, enter its password
using the touch keyboard, and connect. Credentials are saved locally in NVS on
the ESP32 and are not part of the repository. The device starts on Screen1.
Wi-Fi scanning and connection are only started after Screen4 is opened (swipe
left from Screen3); saved credentials are then used automatically. Screen4
shows the connected network's signal quality as a percentage, mapped from
-100 dBm (0%) to -50 dBm or better (100%).

The firmware stays awake and checks the repository's OTA manifest every
10 seconds while connected. When a newer release is found, the device downloads it
automatically and shows the download percentage on Screen4. It verifies HTTPS
certificates using the ESP-IDF certificate bundle (after synchronizing the
system clock with NTP) and checks the firmware SHA-256 before switching to the
new app slot and rebooting. The BOOT button no longer enters deep sleep.

The partition table reserves two 6 MiB OTA app slots while keeping the existing
SPIFFS region unchanged. The first installation of this OTA-capable layout
requires a USB upload of the firmware and partition table. To publish an OTA
release, push a version tag such as `v1.0.0`; GitHub Actions builds the firmware,
creates the release asset, and publishes its version and SHA-256 to
`ota/manifest.json`.

---
