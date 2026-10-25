# M5Stack M5Paper v1.1 — experimental CrossStore target

Status: **build/host validation only; no physical board available (2026-10-26)**.
This is the original ESP32 M5Paper, not the ESP32-S3 PaperS3. Do not exchange their images.

## Hardware contract and integration

The [vendor specification](https://docs.m5stack.com/en/core/Paper_v1.1) describes
ESP32-D0WDQ6-V3 (dual LX6, 240 MHz), 16 MiB Flash, 8 MiB physical PSRAM,
540 × 960 ED047TC1 / IT8951E with 16 grays, GT911, TF slot and BM8563 RTC.
USB is a CP2104/CH9102 UART bridge: USB disk mode is unavailable. No built-in
speaker, microphone, LoRa or frontlight capability is advertised.

The existing SDK `BoardConfig::M5PAPER_V11` and IT8951 driver are reused:

| Area | Wiring / decision |
| --- | --- |
| Display | SPI SCLK14/MOSI12/MISO13, CS15, HRDY27, EPD power23; landscape 960 × 540 framebuffer, automatic controller rotation |
| TF card | Same global SPI bus; CS4, 20 MHz. HAL storage serialization remains in use |
| Touch | GT911, SDA21/SCL22, IRQ36, 0x5D (alternate 0x14); existing swapXY/flipY mapping retained pending physical acceptance |
| Buttons / power | Wheel GPIO37/38/39; 38 confirms/wakes. Main power latch GPIO2 retained through deep sleep |
| RTC | BM8563 at 0x51 on shared I2C21/22, using existing PCF8563-compatible SDK implementation |
| Battery | Existing GPIO35 ADC and 2:1 divider; accuracy pending measurement |

Reference: [M5EPD initialization](https://github.com/m5stack/M5EPD/blob/master/src/M5EPD.cpp)
and [IT8951 transfer implementation](https://github.com/m5stack/M5EPD/blob/master/src/M5EPD_Driver.cpp).
No panel VCOM calibration is overwritten. SHT30 support is outside this port.

`m5paper_v11` is the reader development target; `m5paper_v11_store` adds
CrossStore and ESP-NOW. Lua source packages are shared with other supported boards;
manifest capabilities determine install eligibility. Tox `wifi_chat` is disabled
until internal/PSRAM budgets are tested. Wi-Fi catalogue access remains enabled.
The classic ESP32 temperature API is guarded by the actual SoC capability.
BLE is not enabled by this profile.

## Display and memory

Native image rendering borrows a 259,200-byte 4bpp PSRAM buffer, converts
nibble order through a 480-byte stack row, uploads it and uses GC16. The loan is
freed on commit, cancel, failure or driver destruction. A full native frame is
required by the existing renderer contract; internal RAM/stack cannot hold it.
The existing three 64,800-byte overlay/base PSRAM buffers are unchanged. Text
antialiasing still uses the existing four-level overlay path. Returning from
native gray to B/W uses GC16 before differential B/W updates resume.

HRDY/LUT waits are bounded. A timeout leaves optical state unknown, aborts further
image/refresh/sleep commands in that operation, and requires a healthy readiness
check before retry. Host command traces cover consecutive gray frames, B/W and
overlay transitions, OOM, cancellation, timeout and recovery. They do not verify
physical waveform quality or controller timing on an actual panel.

Classic ESP32 maps at most 4 MiB PSRAM into the ordinary address space; the 8 MiB
physical part does not mean 8 MiB is available to normal heap allocations.
No Himem paging is introduced. See [Espressif Himem](https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32/api-reference/system/himem.html).
Runtime free/largest internal and PSRAM blocks remain unmeasured.

## Build and installation

```sh
pio run -e m5paper_v11_store
python3 test/test_m5paper_v11_port.py
python3 -m unittest discover -s scripts/tests -p test_fork_store_release.py
```

Uses `esp32dev`, QIO, 40 MHz Flash and `partitions.csv` (two 6.25 MiB app slots).
The merged release `m5paper-v11-full-install.bin` is flashed at **0x0**; it contains
an erased 0x1000 prefix. For separate images the **bootloader is at 0x1000**,
partitions 0x8000, boot_app0 0xE000, application 0x10000. S3 bootloaders instead
start at 0x0. Both the packager and website check chip IDs and offsets.
Full installation replaces Flash settings; TF data is not part of the image.
Back up existing Flash before first installation. Use a matching application
image for subsequent updates on the same partition layout.

## Validation record (2026-10-26)

| Check | Result |
| --- | --- |
| `pio run -e m5paper_v11_store` | Passed; first build static RAM 76,820 bytes, Flash 5,808,192 / 6,553,600 bytes (88.6%). Runtime heap not measured |
| `test/test_m5paper_v11_port.py` | Passed; actual board/tag and driver, ASan/UBSan, including text-page cadence |
| Release packaging tests | Passed; four targets, classic boot offsets/frequency, chip/model/SHA mismatch rejection |
| Physical display, input, TF, RTC, networking, sleep and power | Pending; no hardware available |

## Acceptance still pending

No M5Paper has been flashed or observed for this port. Required device checks:
boot identity/memory; full/fast/16-gray display and transitions; all touch edges,
wheel gestures and release barriers; TF read/write plus EPUB progress; Wi-Fi
store/download and ESP-NOW; RTC persistence; battery/USB operation and sleep/wake.
Record heap minimum/largest blocks after repeated reading and app cycles, physical
ghosting and measured current. Compilation cannot establish any of these results.

PaperS3's independent stale-touch fix ships in the same fork release. Its prior
hardware acceptance does not establish acceptance of M5Paper or of the new touch fix.
