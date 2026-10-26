# M5Stack M5Paper v1.0 / v1.1 — shared experimental CrossStore target

Status: **build/host validation passed; original M5Paper hardware bring-up in progress (2026-10-26)**.
This is the original ESP32 M5Paper, not the ESP32-S3 PaperS3. Do not exchange their images.

## Original M5Paper compatibility

The [original Paper specification](https://docs.m5stack.com/en/core/Paper) and
[Paper v1.1 specification](https://docs.m5stack.com/en/core/Paper_v1.1) list the same
ESP32-D0WDQ6-V3, 16 MB flash, 8 MB PSRAM, IT8951E display controller, GT911, SD SPI,
wheel/power GPIOs and BM8563. M5Stack explicitly describes the v1.1 change as a
flexible screen replacing the original rigid screen with equivalent functionality.
Both documentation pages link the same schematic and M5EPD driver. The original
panel is ED047TC1; v1.1 lists ET047TC1. The IT8951 owns the panel-specific waveform;
CrossMux retains factory VCOM rather than programming a different panel's value.

The same `FREEINK_DEVICE_M5PAPER` image therefore covers both revisions. Existing
`m5paper_v11` / `m5paper_v11_store` environment names, `m5paper-v11` asset prefix,
and `m5paper` firmware tag are preserved for compatibility. These identifiers do
not detect the physical revision. The release manifest declares both
`m5paper_v10` and `m5paper_v11`. No separate board profile or framebuffer is needed.
The first physical boot exposed an existing IT8951 handshake defect; the shared
driver correction below applies to both models and adds no allocations.

An unmarked/original unit was connected through a CP2104 UART bridge on
2026-10-26. Read-only identification confirmed ESP32-D0WDQ6-V3 revision 3.0 and
16 MB flash. Its existing December 2021 `epub-reader` firmware reported 8 MB
physical PSRAM (4 MB heap mapping), initialized M5Paper display successfully,
and timed out mounting SD while no TF card was inserted. This confirms the
board family, not the exact PCB revision. The complete 16 MiB original flash was
backed up and independently matched against the device digest before flashing.
The private backup and restore instructions are retained locally.

## First physical boot and IT8951 correction

The published `8000d1b4e520e13bf45cadef7278171bd54a9c03` image booted without a
panic and found the external RTC, but its IT8951 initialization logged HRDY/LUT
timeouts. The stock M5EPD implementation waits for HRDY after command/data/read
preambles and after the read dummy word, and sends SYS_RUN before initialization.
The previous CrossMux driver omitted those handshakes and the initial SYS_RUN.

`It8951Driver` now centralizes the preamble handshake and bus release, retains
bounded waits, closes CS and the SPI transaction on every timeout, and explicitly
wakes the controller before querying/configuring it. B/W, text-gray and native
16-gray uploads use the same transfer helper. No framebuffer or heap allocation
was added; factory VCOM and refresh waveforms are unchanged.

The recording-controller test now asserts that data cannot follow a preamble or
read dummy while HRDY is low, requires SYS_RUN before device-info reads, and checks
bus release on failures at both points. It failed against the previous driver and
passes with the correction, alongside the existing gray/timeout/reading tests.

The corrected local build on `codex/m5paper-original` (SDK base
`a26ae8db1327540a67c9039fca4c7d7e4be72671` plus the uncommitted driver/test changes)
was written at 0x10000 and independently verified. Its boot logs no longer show
IT8951 timeouts, and the user confirmed a clear SD-error message on the physical
panel. The absent TF card follows the expected SD-error path.
The supplied 4 GB card could not be mounted on macOS
after either MBR/FAT32 or whole-device FAT32 initialization; reader/card diagnosis
is separate from the device's SPI SD acceptance.

A replacement 16 GB card already contained an exFAT Ventoy volume and a system
image. Its existing data and partition layout were preserved. CrossMux mounted
that volume, saved settings after the user selected Chinese, wrote its bundled
Chinese guide, and built/read the EPUB spine/TOC cache. Touch events reached the
language selection, Apps menu and Store activities. The USB connection then
disappeared without a captured panic. The user subsequently confirmed that touch,
wheel page turning and progress after leaving/reopening the guide work correctly.
Networking and grayscale-image quality remain pending. The first sleep test was
reported to return to Home despite starting from a book. A subsequent monitored
cycle, with no intervening code change or reflash, saved spine 5/page 0, entered
Sleep, rebooted with `DEEPSLEEP_RESET`, and opened EpubReader directly with the
same spine/page. The user confirmed that resume now works. This establishes one
successful captured cycle, not the cause or a fix for the earlier report. The
local evidence is `sleep-resume-observation.log`; repeated-cycle/power acceptance
remains pending.

## Hardware contract and integration

The [vendor specification](https://docs.m5stack.com/en/core/Paper_v1.1) describes
ESP32-D0WDQ6-V3 (dual LX6, 240 MHz), 16 MiB Flash, 8 MiB physical PSRAM,
540 × 960 ED047TC1 (original) / ET047TC1 (v1.1), IT8951E with 16 grays,
GT911, TF slot and BM8563 RTC.
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
The corrected no-card boot reported 199,200 bytes free internal RAM, historical
minimum 168,180 and largest block 110,580. PSRAM reported 3,924,712 bytes free and
largest block 3,866,612; its reported historical minimum was zero and is not a
usable headroom measurement. Reading/app/sleep-cycle memory remains unmeasured.

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
| `pio run -e m5paper_v11_store` | Passed; corrected build static RAM 76,820 bytes, Flash 5,808,264 / 6,553,600 bytes (88.6%) |
| `test/test_m5paper_v11_port.py` | Passed; actual board/tag and driver, ASan/UBSan, including text-page cadence |
| Release packaging tests | Passed; 14 tests covering the 13-target matrix, both classic M5Paper models, classic boot offsets/frequency and chip/model/SHA mismatch rejection |
| Shared firmware regression | Passed; format, cppcheck and all 8 `ci-check` firmware targets (default, Sticky, X4 Pro, PaperMono, EEGO A4, Murphy M4, Waveshare 3.97, Read Pico) |
| Host CTest suite | Passed: 1,142 tests; 2 marked skipped (`ToxLinkage`, `WifiIdentityQr`) |
| Complete `./bin/ci-check` command | Failed at the subsequent Epdiy transaction harness: macOS `ld` does not support GNU `--wrap=calloc`. Local Docker daemon unavailable. This is not an IT8951 failure; the remaining PaperS3 and HAL power checks were run separately and passed |
| Original device identification | Passed; ESP32-D0WDQ6-V3 revision 3.0, 16 MB Flash, 8 MB physical PSRAM reported by existing firmware. Exact PCB revision unconfirmed |
| Original flash backup and firmware write | Passed; full 16 MiB backup, independent device digest, then app write and independent digest verification |
| Corrected CrossMux boot | Passed with and without the card; no captured panic or IT8951 timeout, RTC detected |
| Basic physical display | Passed; user confirmed a clear SD-error message on the panel |
| TF read/write and first-use guide | Passed with replacement 16 GB exFAT card; settings/guide/cache written and guide metadata parsed; existing files preserved |
| Initial touch navigation | Passed for language selection, Apps and Store entry according to the event/activity logs; all-edge accuracy still pending |
| Basic reading, touch, wheel and progress | Passed; user confirmed forward/back paging and leaving/reopening the guide |
| Reader sleep/resume | Passed for one captured cycle: deep-sleep wake reopened spine 5/page 0 directly; user confirmed. Earlier return-to-Home report was not reproduced and its cause remains unknown; no sleep-specific fix was applied |
| Extended display, RTC persistence, networking, repeated sleep and power | Pending; do not infer repeated-cycle or power acceptance from the single captured resume |

## Acceptance still pending

An original M5Paper is now available; CrossMux physical acceptance is still pending. Required device checks:
boot identity/memory; full/fast/16-gray display and transitions; all touch edges,
wheel gestures and release barriers; TF read/write plus EPUB progress; Wi-Fi
store/download and ESP-NOW; RTC persistence; battery/USB operation and sleep/wake.
Record heap minimum/largest blocks after repeated reading and app cycles, physical
ghosting and measured current. Compilation cannot establish any of these results.

PaperS3's independent stale-touch fix ships in the same fork release. Its prior
hardware acceptance does not establish acceptance of M5Paper or of the new touch fix.
