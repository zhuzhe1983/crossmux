# M5Stack PaperS3 development target

`papers3` is an experimental, separate ESP32-S3 build for standard M5Stack
PaperS3 (SKU C139). It is not the Paper Mono or original ESP32 M5Paper image.
It does not join the Nightly/OTA registry. Hardware CI is configured to compile this target;
compilation and host tests do not establish hardware acceptance.

## Hardware contract

Sources: [M5Stack specifications](https://docs.m5stack.com/en/core/PaperS3),
[official V1.0 schematic](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/517/sch_papers3_V1.0.pdf),
the pinned [SDK profile](../../freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h),
and [SDK PaperS3 notes](../../freeink-sdk/docs/m5stack-papers3-support.md).
The connected device is confirmed by its owner as a standard PaperS3 v1.0 with
a TF card inserted. Acceptance applies to that revision only.

| Area | Contract |
| --- | --- |
| MCU/memory | ESP32-S3R8, 16 MiB quad Flash, 8 MiB Octal PSRAM; `qio_opi` prebuilt core and QIO Flash |
| Partitions | Existing `partitions.csv`: two 0x640000-byte OTA slots; app0 0x10000, app1 0x650000 |
| Display | ED047TC1, 960x540 native landscape, 120 bytes/row, 64,800-byte B/W frame; parallel LGFX bus at 16 MHz |
| Panel data | D0..D7 = GPIO6/14/7/12/9/11/8/10; SPH13, CL16, LE15, SPV17, CKV18, OE45, PWR46 |
| Touch | GT911, SDA41/SCL42, INT48, no ESP-controlled reset; shared internal I2C bus with RTC/IMU |
| Buttons | Side key belongs to PMS150G; no firmware-readable navigation/power GPIO; use existing touch navigation |
| SD | SPI: SCLK39, MISO40, MOSI38, CS47; no switched SD rail; SDK default clock pending card tests |
| Power | PMS150G self-latches; SDK `BoardPaperS3::powerOff()` pulses GPIO44 five times at 50 ms low/high |
| Battery/USB | Battery ADC3 with 2:1 divider; charging GPIO4 active low; USB detect GPIO5; readings pending measurement |
| RTC | BM8563 at 0x51, PCF8563-compatible; shared SDA41/SCL42; interrupt goes to PMS150G, not ESP |
| Other | No frontlight. BMI270 backend/pickup wake is not implemented in the pinned SDK. Buzzer exists; this port adds no new audio behavior. USB MSC remains disabled. |

The SDK's touch coordinate layout, swap/flip and panel rotation are inherited
pending corner tests. Do not treat its provisional calibration comments as
physical evidence. GPIO48 is not an S3 RTC wake pin; do not arm EXT1 touch wake.
The V1.0 schematic resolves the naming difference with the web pin table:
GPIO45's `EPD_PWR` net drives panel XOE/VDD/MODE, while GPIO46's `RST_EN`
enables the switched boost supply. These match SDK `pinOe=45` and `pinPwr=46`.
The schematic also shows equal 22 kohm battery-divider resistors; ADC accuracy
still requires measurement on the actual board.
The SDK supplies panel rail timing through M5GFX's stock `Bus_EPD::powerControl`;
CrossMux adds no GPIO sequencing or copied waveforms.

## Integration

- `platformio.ini`: `papers3_hardware` uses `FREEINK_DEVICE_PAPERS3`, SDK
  `BoardPaperS3`, and M5GFX Git tag `0.2.20`, matching the existing LGFX integration.
  The registry form `m5stack/M5GFX @ 0.2.20` is not used. It inherits the existing
  S3 PSRAM BLE configuration and LTO; no custom Arduino core is requested.
- `lib/hal/HalPowerManager.cpp`: installs the existing SDK host-shutdown hook
  with `BoardPaperS3::powerOff`. Sleep first quiesces display/storage/input using
  the existing application path. If USB prevents a power cut, the SDK idles
  after the hook returns instead of entering deep sleep without a wake source.
  Hardware reset/power cycling is required to leave that fallback.
- `src/main.cpp`: keeps USB cold boots awake, avoiding an immediate shutdown
  pulse/restart cycle after flash or cable insertion.
- `src/network/FirmwareBoardTag.cpp`: embeds `papers3`; tagged Paper Mono,
  LilyGo and X3/X4 updates are rejected by the existing scanner.
- `FREEINK_CAP_USB_MSC=1` enables File transfer → USB disk on the prebuilt
  `qio_opi` core. Existing HalStorage/UsbDriveActivity detach the SPI SD filesystem
  before handing raw sectors to the host. Host eject ends the session and reboots
  to remount; no simultaneous host/reader access is allowed.
- SDK `M5PAPER_S3.grayscaleLevels=16` and the LGFX native transaction implement
  sixteen-level output. Touch, SD, RTC, battery and panel access remain behind HAL.
  The SDK gitlink is unchanged pending a separate SDK contribution.

PaperS3 has no software-readable side key. Firmware can save state and power
off on inactivity; the hardware side key can cut power independently and cannot
be intercepted to save the current page. Standby-display light sleep and RTC
alarm wake are not added by this port.

### Current grayscale and UI limits

The app menu now uses `SemanticAppIcons` for both grid and list layouts.
PaperS3 uses preprocessed g16/32/plain list icons and g16/96/plain grid icons;
compact grid cells fit that source to 64 or 32 pixels. Native transactions use
M5GFX's stock `epd_quality` waveform, not the fast B/W waveform. The existing
518,400-pixel PSRAM canvas lends its first half as packed 4bpp storage, then
expands backward in place to 8bpp on commit. Cancel invalidates the host canvas;
a later B/W frame rebuilds it. No new full-screen buffer or waveform is added.
The native renderer clears/clips both the proxy and native frame consistently.

Other four-level boards select g4/32/plain and g4/64/dither, convert values via
`mapTwoBitPixel`, and upload icon-only strips using a 256-byte stack row. The
512-byte bounded placement table belongs to AppsMenuActivity and is reused;
there are no per-frame heap allocations. Night mode, unsupported strip targets,
and a failed native loan use g2/32/plain or g2/64/dither. Chrome icons elsewhere
retain their existing design. RF/Wi-Fi chat currently reuse Transfer/WeRead art.

2026-10-19 implementation: real LGFX driver host checks (16-tone order, repeated
commits, cancellation, gray→B/W/overlay, unsupported board, allocation failure)
and renderer native-image regression checks passed. New firmware build, USB
host transfer/eject and physical grayscale acceptance are recorded below once
performed. A serial screenshot only represents the B/W proxy and cannot verify
optical gray levels.

OPDS testing also exposed missing characters in book titles with only the
built-in Chinese subset installed. The captured boot log found no families in
`/.fonts` or `/fonts`; the built-in CJK coverage contains 3,517 ideographs.
Sample feed titles contained valid Unicode characters absent from the compiled
font intervals. This is a font-coverage issue, separate from grayscale. The
existing S3 UI fallback can use an installed, selected SD font family with
matching 8/10/12pt files; see [SD font installation](../sd-card-fonts.md).
Visual confirmation after installing a complete Chinese family remains pending.

## Memory budget

The icon/native-display changes add no per-frame heap allocation or second UI framebuffer. The
existing LGFX path allocates persistent, checked PSRAM storage: an 8-bit
960x540 canvas (518,404 bytes including M5GFX padding) and two 64,800-byte
grayscale planes. These are too large for stack or C3 internal RAM and are
linked only for selected S3 LGFX targets. M5GFX additionally owns panel step
and current-pixel buffers, DMA scanlines, waveform LUT, task and queue; see the
[pinned M5GFX audit](lilygo-t5s3.md#pinned-m5gfx-allocation-audit) for allocation
mechanisms. PaperS3 uses stock quality/text/fast/fastest LUT lengths 32/32/10/7,
plus four erase steps: 85 x 256 x 2 = 43,520 DMA bytes. The two DMA scanlines
request 248 bytes each, step/current-pixel buffers request 1,036,800/259,200
PSRAM bytes, and the display task requests a 4,096-byte stack plus an eight-entry
queue. Known canvas/planes/panel PSRAM requests total 1,944,004 bytes; the SDK
also owns the single 64,800-byte B/W framebuffer (PSRAM-first with a checked
internal allocation fallback). Allocator overhead and bus/task metadata are extra.

Measure internal heap, internal DMA and PSRAM free/largest blocks separately at
boot, EPUB open, repeated turns, grayscale, Wi-Fi/BLE start/stop and sleep.
Allocator totals are not runtime headroom or measured power savings.

## Reproduce and verify

```sh
git submodule update --init --recursive
python -m pip install pioarduino==6.2.0
python scripts/patch_pioarduino_cache.py --prepare-platform
pio run -e papers3
python3 test/test_papers3_port.py
python3 test/test_hal_power_sources.py
python3 -m unittest discover -s scripts/tests -p 'test_ble_c3_config.py'
python3 -m unittest discover -s scripts/tests -p 'test_configure_nimble_psram.py'
./bin/ci-check
```

Initial installation needs this board's bootloader and partition table, not
only `firmware.bin`. After identifying the port and verifying a full Flash
backup, `pio run -e papers3 -t upload --upload-port <port>` uses the build's
actual offsets. Do not erase or overwrite factory firmware before backup.
Restore a verified 16 MiB raw backup with esptool `write-flash 0 <backup.bin>`.
Keep that backup outside the repository; it may contain private settings.

## Acceptance record

Source baseline: CrossMux `d6c1512e46e3d26add10839a272df1294cebcab4` plus the
uncommitted PaperS3 port; SDK `9aa7e95e02c6374a23a562c883eb302552ff24f1`.

| Check | Status | Evidence / remaining work |
| --- | --- | --- |
| Connected device inventory | passed | 2026-10-18, esptool: ESP32-S3 rev v0.2, embedded 8 MiB PSRAM, 16 MiB Flash, USB Serial/JTAG |
| Original Flash backup | passed | Full 16 MiB read; esptool `verify-flash` digest matched; local recovery record kept beside backup |
| Shutdown and board guard host tests | passed | `test_papers3_port.py`; actual SDK pulse implementation, HAL order, chunked matching/wrong-board tags |
| USB boot / S3 BLE configuration checks | passed | `test_hal_power_sources.py`, `test_ble_c3_config.py`, `test_configure_nimble_psram.py` |
| PaperS3 firmware build | passed | `pio run -e papers3`; static RAM 81,988/327,680 B; Flash 5,965,915/6,553,600 B. Runtime heap remains a separate measurement. |
| Host suite | failed | macOS Apple Clang: 1,044/1,048 passed; WeReadConnection, ReadingUiRegressions, UiFontFallback and FileBrowserImages fail at compile-time on warning-as-error/compiler-option compatibility. See local `ctest.log`. |
| EpdiyLcd transaction host check | failed | macOS linker rejects GNU `--wrap=calloc` used by the existing SDK harness; test binary cannot link on this host |
| Shared X3/X4 and S3 regression builds | passed | `./bin/ci-check` built `default`, `sticky`, `x4pro`, `papermono`, `eego_a4`, `murphy_m4`, `waveshare_epaper_397` and `readpico`; local `ci.log` |
| Repository checks | failed | Formatting and cppcheck passed. `./bin/ci-check` then stopped at the same four host-test compile failures listed above (1,044/1,048 passed), so it did not reach its trailing standalone tests. PaperS3/USB tests passed separately; EpdiyLcd linker failure is recorded separately. |
| Hosted hardware CI | pending | Workflow includes `papers3`; no hosted run submitted |
| Firmware write/hash | passed | Full factory image written at 0x0; esptool verified written data hash; local `flash.log` |
| Boot | passed | Physical unplug/off/on after flashing boots CrossMux; owner confirmed UI. Passive `runtime.log`: `SPI_FAST_FLASH_BOOT`, detected `m5paper_s3`, RTC/SD/display initialized; no panic/OOM observed. USB automatic reset alone had remained in ROM download mode. |
| Basic display/touch/reading | passed | Owner confirmed screen direction, touch positions, about ten EPUB forward/back turns and menu/return. Runtime log records page renders, cache writes and progress saves. |
| Extended display/input | pending | All four orientations, corner taps, gestures, full/fast/gray transitions and ghosting. The later semantic-icon/native16 acceptance is recorded below. |
| Sleep recovery / progress persistence | passed | Owner completed two battery-powered automatic sleep / side-key-on cycles, confirmed normal recovery and retained reading progress, and explicitly accepted two cycles for this round instead of a third. This records two cycles only. |
| SD failure cases / settings persistence | pending | Missing/unreadable card and settings persistence; basic mount/read/cache-write and reading-progress persistence passed |
| Power measurements | pending | Battery/USB reading accuracy and measured current; sleep recovery is recorded separately above |
| Reading memory sample | passed | After repeated page rendering: internal free 149,812 B, historical minimum 124,268 B, largest block 106,484 B; PSRAM free 6,345,176 B, minimum 6,307,316 B, largest block 6,291,444 B. End-of-capture idle samples stable for 30 seconds; this is not a long-duration leak test. |
| Extended memory testing | pending | Internal DMA-specific headroom, repeated network start/stop and sleep cycles |

Do not mark physical acceptance passed from a successful build or a boot log.

Initial application image SHA-256:
`4674bd3e4894faeb9d915eb908de4d66c54ea755142dc278a48aa71e59fdf907`.
Factory image (bootloader + partitions + boot_app0 + application) SHA-256:
`14976716883e89bedd9ed7ea5554f3255070578ee1749a143dabe440e4f8c338`.
Build log and images are retained locally under `build/papers3-validation/`.
The build reports dependency macro/ODR warnings, including M5GFX's documented
include-order-dependent filesystem base; these are not new driver edits and
are not suppressed by this port.

### USB disk and semantic-icon candidate — 2026-10-19

`papers3_chat` built successfully with USB MSC and the native 16-level app menu.
Application binary: 6,457,568 bytes, SHA-256
`3ff38481014864e50147393e94615ef897a9d901ee85b609892d8ace786c1a9a`.
Static RAM: 118,548 / 327,680 bytes; linked Flash: 6,452,359 / 6,553,600 bytes.
The app binary leaves 96,032 bytes in its slot. Linked icon arrays occupy 126,720
bytes of mapped Flash on PaperS3; the C3 image contains 34,560 bytes of 2/4-level
assets and no g16 arrays, verified with toolchain `nm`.

Host checks: native renderer and LGFX driver passed; semantic icon renderer
matched the source PNGs in 14 cases; FreeInkUI passed 296,071 checks including
custom icon geometry. On Apple clang, the SDK's stock test runner fails on
pre-existing unused keyboard-layout constants under `-Werror`; running the same
sources with only `-Wno-unused-const-variable` passed. Formatting and cppcheck
passed. Full multi-target CI is recorded separately after completion.

The candidate was written to the identified PaperS3 application slot and
independently verified. The owner confirmed the new icons and visible grayscale
look correct. Runtime samples after menu rendering reported internal free
100,592 B, historical minimum 87,484 B, largest block 47,092 B; PSRAM free
6,134,444 B, minimum 6,120,256 B, largest block 6,029,300 B. No panic or OOM was
observed in this capture; longer-duration memory and ghosting checks remain open.

macOS enumerated `CrossMux-PaperS3` / `FreeInk SD Card Media` and mounted the
15.9 GB FAT32 volume `EDC`. The owner supplied a Finder screenshot showing the
existing folders and EPUB files. This confirms enumeration and directory reads;
write/readback, safe eject and return to the reader are **not yet verified**.
The device disappeared from the host inventory before those checks; the cause
is not yet known.

The owner also reported missing-glyph squares on the USB page. The page kept
using SD font faces after handing the raw card to the host. `UsbDriveActivity`
now releases bitmap and vector SD fonts under the render lock before its first
paint and before storage handoff, and restores the built-in UI fallback maps.
Saved font settings are retained and loaded normally on reboot. The new
`test_usb_font_handoff.py` checks ordering, both font configurations and startup
failure. It passes. The existing six UI font fallback tests also pass when the
Apple Clang-only `-Wnon-c-typedef-for-linkage` warning is disabled; their stock
runner still fails on the pre-existing anonymous `EpdFontData` typedef warning.
The list regression seams were updated for the frame hooks and icon painter.
All 16 reading/UI regression cases pass with only the pre-existing Clang
`non-c-typedef-for-linkage`, `mismatched-tags` and `unused-variable` warnings
disabled in a local compiler wrapper. Production compiler flags are unchanged.

The font fix builds for `papers3_chat` and `papermono_chat`. The new PaperS3
application is 6,458,464 bytes (95,136 B slot headroom), SHA-256
`f9c37be44d2617fbd8f1a95cf100ff821d52b79d2bb0402b2069e9612a54eef5`.
Static RAM remains 118,548 B; linked Flash is 6,453,263 B. This follow-up is
**not yet flashed**. Images and logs are retained in
`build/papers3-validation/update-20261019-usb-font-final`; the previously flashed
candidate remains in `build/papers3-validation/update-20261019-gray-usb`, and
the earlier rollback image in `build/papers3-validation/update-20261019`.

Final local checks: formatting, cppcheck and all eight `ci-check` firmware
targets passed (`default`, `sticky`, `x4pro`, `eego_a4`, `murphy_m4`,
`waveshare_epaper_397`, `papermono`, `readpico`). The host run completed 1,070
tests: 1,065 passed, four failed at compile time on the same pre-existing macOS
harness issues (`WeReadConnection`, `ReadingUiRegressions`, `UiFontFallback`,
`FileBrowserImages`), and `WifiIdentityQr` was marked skipped because its
optional decoding check was unavailable in the CMake-selected Python.
`UsbFontHandoff` passed in this run. Full CI therefore still exits nonzero and
does not reach its trailing standalone tests; see
`.cache/papers3-usb-font-ci-final.log`. The font/UI checks with local compiler
compatibility wrappers are supplementary, not a claim that stock CI passed.
The trailing PaperS3 port and HAL power-source checks were run separately and
passed; `.cache/papers3-usb-font-port-tests.log` records their output.
