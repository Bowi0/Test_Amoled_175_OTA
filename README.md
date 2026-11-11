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
reported over Serial. The temporary intro DMA buffer is freed and SPIFFS is
unmounted after playback, including when playback is skipped or fails.

The display uses QSPI on GPIO 4–7, GPIO 38 (clock), GPIO 12 (chip select),
and GPIO 39 (reset). Touch uses I²C on GPIO 15/14, with reset on GPIO 40 and
interrupt on GPIO 11. The pin assignments and controller selection match
[Waveshare's 1.75" board reference](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75/blob/main/HARDWARE_REFERENCE.md).

## Wi-Fi and manual OTA updates

Screen4 presents available Wi-Fi networks in a scrollable list with signal
strength indicators. The list refreshes automatically while Screen4 is open,
and previous results remain visible during a refresh. A spinner indicates a
search; when no results are available, the page displays the search message
instead of claiming that no networks were found. If a connection attempt or
update is in progress when the page opens, the scan waits for it to finish and
the page displays the reason for the wait.
The connected network appears first, even before it is present in scan results;
its name is displayed in the selected theme color.
The selected network has a matching border, including when it is connected.
Select a network and tap Connect; previously saved networks use their saved
password, while new networks open the password entry view. If a saved
connection fails, the password can be entered again. Up to ten networks are
stored; no existing network is overwritten when the store is full. After
restart, the last connected network is tried first, followed by the other
saved networks if needed. The touch keyboard is shown only for password
entry. The keyboard is centered on the round display, and pressing a
character shows an enlarged preview above the touch point. Credentials are
saved locally in NVS on the ESP32 and are not part
of the repository. After the intro, a loading indicator is shown while the
screens are created. Screen1 is then drawn before saved wireless connections
are restored.
After a successful connection, the confirmation closes automatically after one
second; connection errors remain visible until acknowledged.
Selecting the connected network changes the button to “Trennen”. Disconnecting
keeps the saved credentials but prevents automatic reconnection until the user
connects again or the device restarts.
Swipe down to open Screen6's quick settings; swipe up to return to the
previous screen. Tap WLAN or Bluetooth to toggle them. The WLAN tile shows
signal strength when connected and animates during connection attempts. Its
center dot remains lit while WLAN is enabled; the arcs are muted when
disconnected, and the whole symbol is muted and crossed out when WLAN is disabled.
The Bluetooth symbol uses the theme color
when enabled and is muted when disabled. Hold WLAN to enable it
if necessary and open
Screen4's network list, where the Zurück button returns to Screen6. The brightness slider is
also available on Screen6 and ranges from 10% to 100%; older saved values below
10% are raised to 10% at startup. WLAN, Bluetooth
and brightness settings are saved in NVS; saved WLAN credentials are retained
when WLAN is disabled. After a restart, enabled WLAN connects using saved
credentials and enabled Bluetooth resumes advertising.

The Farbthema tile, marked with a color-drop symbol, opens square color tiles
for blue, brown, silver, gold, or bronze. A check mark identifies the
selected color.
The selected theme is saved in NVS and changes controls and accent colors on
the settings, Wi-Fi, and update pages; the brew-pressure gauge is unchanged.

Swipe horizontally from Screen1 to open the four-output control page. Swipe
left there for the analog clock and date, or right to return to Screen1.
The clock uses Europe/Berlin time, including
automatic daylight saving changes. It shows “Warte auf Uhrzeit” until the
device has obtained a valid time through Wi-Fi and NTP. The time continues
running without Wi-Fi until the device restarts.

The output page controls GPIO 1–4 on the companion ESP32-S3 over 115200 baud
UART. Connect display GPIO43 (TX) to companion GPIO44 (RX), display GPIO44 (RX)
to companion GPIO43 (TX), and connect GND to GND. Both use 3.3 V logic.
Commands and acknowledgements have CRC16-CCITT-FALSE; CRC detects corruption
but does not authenticate a device. Outputs default to LOW and are forced LOW
after a two-second communication timeout. Use a series resistor per LED and
observe board boot-strapping requirements; do not connect loads directly.
The output page shows confirmed states and waits for acknowledgement before
accepting another toggle. After a lost connection, the requested state is
cleared rather than restored automatically.

For the current display test, the companion reads a potentiometer on GPIO5
and streams an approximately scaled 0–10 bar value roughly every 16 ms over
CRC-protected UART frames. The direct 0–3.3 V ADC reading is not accurate over
the entire range, particularly at the upper end; see the companion README.
The display uses this value for the brew-pressure needle and relays it over
Bluetooth. On link loss, the needle holds the last received value. The stream
is suspended during Mainboard OTA. An older companion firmware does not
provide these values.

The existing pressure frame is brew-pressure. Additional CRC-protected frame
types 9, 10 and 11 are reserved for steam-pressure, brew-temperature and
brewhead-temperature. The two payload bytes are big-endian: unsigned centibar
for steam-pressure and signed two's-complement hundredths of a degree Celsius
for temperatures. Values are invalid until received and cleared on link loss or
Mainboard OTA; the Zero does not yet send these three values.

Screen1 includes a temporary simulated silver steam-pressure needle. It starts
108 pixels from the center, extends to 30 pixels before the edge and has a
4-pixel width. It moves linearly from 219° (0 bar) to 141°
(3 bar) in 5 seconds and back in 5 seconds, independently of incoming
steam-pressure values. The separate 80-pixel, 3-pixel-wide temperature needle
starts at the center and simulates 75 °C at 230° to 100 °C at 130°, moving
clockwise through 360° in 5 seconds and returning in 5 seconds. The
brew-pressure needle starts 108 pixels from the center and ends 60 pixels
before the edge, with the same 4-pixel width. It uses the Zero's
pressure value, mapping 0 bar to 254° and 15 bar to 106° clockwise through
360°; values outside 0–15 bar are clamped for display. The Zero currently
sends only 0–10 bar. Angles use 0° at 12 o'clock and increase clockwise.

The update page provides separate Display and Mainboard buttons. Mainboard
updates are downloaded over HTTPS from `Bowi0/ESP32-S3-Zero-KM` and checked
against the release manifest's SHA-256 before transmission over UART. The
Mainboard verifies the hash and image again before switching OTA partitions.
Its outputs remain off during transfer. UART itself is not authenticated.
The installed Mainboard version is read over UART and compared to the
manifest. Older Mainboard firmware cannot report its version; in that case
the display shows an unknown version and offers the available update without
claiming that it is newer. An OTA-capable Mainboard
firmware must be installed over USB once, and a release and manifest must be
published before the button can retrieve an update.

The host-side UART simulation in `test\test_uart_link` exercises the shared
protocol and both firmware state machines. With a C++17 compiler available,
run it from this project directory (the sibling companion project must exist):

```powershell
c++ -std=c++17 -Itest\test_uart_link -Iinclude -Isrc -I..\ESP32-S3-Zero-KM\include test\test_uart_link\test_uart_link.cpp src\uart_switch.cpp -o uart-test.exe
.\uart-test.exe
```

The Update symbol opens Screen5, which shows update status and firmware
version. Use Zurück or swipe horizontally on Screen5 to return to Screen6.
The Wi-Fi page is opened by holding WLAN on Screen6, rather than by swiping
horizontally from Screen1. Brightness and Bluetooth are controlled from Screen6.
The 14, 18 and 20 px UI fonts use generated Montserrat Medium glyphs for
Latin-1 characters, including German umlauts and ß. LVGL's built-in fonts
remain the fallback for interface symbols.

The firmware checks the repository's OTA manifest only when the user presses
“Nach Updates suchen...” on Screen5. If a newer release is found, Screen5 asks
whether to install it; the firmware downloads it only after confirmation and
shows the download percentage. It
verifies HTTPS certificates using the ESP-IDF certificate bundle (after
synchronizing the system clock with NTP) and checks the firmware SHA-256 before
switching to the new app slot and rebooting. Hold the BOOT button for at least
one second and release it to restart the device without erasing saved data.
Restart is ignored while a firmware update is being installed. Releasing BOOT
before the restart keeps GPIO0 from selecting the download mode.
Bluetooth initializes only when enabled, or restored after a restart, to
preserve internal RAM for HTTPS updates when it is off.
The display uses two 20-row DMA buffers to leave more internal RAM available
for Wi-Fi and OTA; full-screen refreshes may require more transfers.

The partition table reserves two 6 MiB OTA app slots while keeping the existing
SPIFFS region unchanged. The first installation of this OTA-capable layout
requires a USB upload of the firmware and partition table. To publish an OTA
release, push a version tag such as `v1.0.0`; GitHub Actions builds the firmware,
creates the release asset, and publishes its version and SHA-256 to
`ota/manifest.json`.

---
