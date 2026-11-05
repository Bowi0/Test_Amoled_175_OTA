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

---
