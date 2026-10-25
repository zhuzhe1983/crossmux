# CrossStore device release matrix

The fork publishes separate Store images for every hardware target integrated in
CrossMux. This does not imply support for every board profile present only in
FreeInk SDK. X3/X4 share one C3 image; all other rows use separate images.

Only PaperMono, PaperS3 and Waveshare 3.97 have prior physical test records from
the CrossStore maintainer. Those records do not validate every subsequent binary.
Every other row is **Alpha — not physically tested by this maintainer**. Build
success and upstream hardware records are recorded separately from local physical
acceptance. Report bugs at https://github.com/zhuzhe1983/crossmux/issues/new with
model, release/version, reproduction steps and logs/photos. The website prefills
that context; a report does not require contacting upstream CrossMux maintainers.

| Model | Environment | Chip | Maintainer hardware status |
| --- | --- | --- | --- |
| PaperMono | `papermono_store` | S3 | Prior test record |
| PaperS3 | `papers3_store` | S3 | Prior test record; new touch fix acceptance pending |
| Waveshare ESP32-S3-ePaper-3.97 | `waveshare_epaper_397_store` | S3 | Prior test record; AirTune loudness unresolved |
| M5Paper v1.1 | `m5paper_v11_store` | ESP32 | Alpha |
| Xteink X3 / X4 | `xteink_store` | C3 | Alpha |
| Seeed Sticky | `sticky_store` | S3 | Alpha |
| Xteink X4 Pro | `x4pro_store` | S3 | Alpha |
| Xteink X4 Classic | `x4c_store` | S3 | Alpha |
| EEGO A4 | `eego_a4_store` | S3 | Alpha |
| Murphy M4 | `murphy_m4_store` | S3 | Alpha |
| Metalio E-Ink 4 (SPI 3.97, not Plus) | `metalio_eink4_store` | S3 | Alpha |
| MindReset Read Pico | `readpico_store` | S3 | Alpha |
| LilyGo T5 S3 | `lilygo_t5s3_store` | S3 | Alpha |

All share the 16 MiB dual-OTA layout but retain their own bootloader, board tag
and image. The release packager checks the chip and Flash header, partition table,
image offsets, source/SDK revisions and hashes. Manifest `validationStatus` is
`prior-hardware-record` for the first three rows, `alpha-unverified` otherwise.
Neither means the particular release has passed physical acceptance.

C3 has no PSRAM: the package manifest rejects >64 KiB Lua budgets without a PSRAM
requirement, and PSRAM-dependent packages are incompatible. Its initial Store
profile does not enable chat services. S3 profiles compile ESP-NOW/Tox, with the
existing runtime PSRAM gate for Tox; LoRa remains PaperMono-specific and audio
streaming is exposed only by supported audio backends. M5Paper's separate limits
are in [m5paper-v11.md](m5paper-v11.md). Applications still declare permissions
and hardware requirements; a shared Lua package does not imply every app can run
on every board.

## Ink4 Plus with ML307 — not a flashable target yet

The user confirmed an ML307 cellular module. Vendor source at
`CloudZao/Metalio-E-INK4-Plus@ad8edf4150a688737c0d7c00cae4413079b262ef`
identifies the host as ESP32-S31 and a 1216×684 parallel panel. Its README says
Wi-Fi only, but the actual board derives from `DualNetworkBoard`, using pins
TX58/RX59/MRDY60/SRDY3; TCA P1.2 shares touch/modem reset and P1.4 shares modem,
Bluetooth and PA power. The vendored `components/esp-ml307` includes ML307 AT
transports. The README is not sufficient evidence that the hardware lacks 4G.

Sources:
- [Board implementation](https://github.com/CloudZao/Metalio-E-INK4-Plus/blob/ad8edf4150a688737c0d7c00cae4413079b262ef/main/boards/metalio-e-ink4-plus/metalio_e_ink4_plus_board.cc)
- [Pins and shared rails](https://github.com/CloudZao/Metalio-E-INK4-Plus/blob/ad8edf4150a688737c0d7c00cae4413079b262ef/main/boards/metalio-e-ink4-plus/config.h)
- [Modem component](https://github.com/CloudZao/Metalio-E-INK4-Plus/tree/ad8edf4150a688737c0d7c00cae4413079b262ef/components/esp-ml307)

Current CrossMux uses Arduino 3.3.11 / IDF 5.5.5; its pinned platform and FreeInk
SDK contain no S31 target. Plus needs a separate S31/IDF6.1 platform port, parallel
display/PMIC integration, shared-rail ownership and a HAL network backend for
cellular. Modem AT HTTP/TCP objects are not a transparent substitute for the
current Wi-Fi/lwIP client interfaces. No S3 image may be offered for this board.
The website lists it as **pending adaptation**, with ML307/4G explicitly pending,
and exposes an issue link rather than a flash button. Alpha labels apply only
after a matching firmware builds; they do not stand in for an unimplemented port.
