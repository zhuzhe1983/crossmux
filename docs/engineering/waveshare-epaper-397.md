# Waveshare ESP32-S3 ePaper 3.97

Experimental hardware target: ESP32-S3, 16 MiB flash,
8 MiB PSRAM, 800×480 SSD1677, 4-bit SDMMC, AXP2101, PCF85063A,
and ES8311/NS4150B audio.

The current vendor page labels the PMIC TG28; its published firmware still uses
the AXP2101 driver. See the dated hardware-evidence note below before accepting
a different board revision.

```bash
pio run -e waveshare_epaper_397
pio run -e waveshare_epaper_397 -t upload --upload-port /dev/tty.usbmodem101
pio device monitor --port /dev/tty.usbmodem101 --baud 115200
```

## Hardware contract

| Function | Wiring |
|---|---|
| EPD SPI | SCLK 11, MOSI 12, CS 10, DC 9, RST 46, BUSY 3; 20 MHz |
| SDMMC | CLK 16, CMD 17, D0/D1/D2/D3 15/7/8/18; 4-bit |
| I²C | SDA 41, SCL 42; 400 kHz |
| RTC | PCF85063A at `0x51` |
| PMIC | AXP2101 at `0x34`; IRQ on GPIO38; ALDO1/2 supply audio and ALDO3 supplies the EPD |
| Audio | ES8311 at `0x18`; MCLK 13, BCLK 14, WS 47, DOUT 48; NS4150B enable 39 |
| Buttons | Back 0, Left 4, Function 5, Right 6; active-low |

Function single-click emits Confirm after the 300 ms double-click window. A
second short click starting within that window emits Back. Holding either press
for 300 ms instead holds Confirm from its physical press timestamp, so existing
business long-press actions work and GPIO5 never emits Power.

The side Power key is connected to the AXP2101 rather than a GPIO button. Its
GPIO38 IRQ is mapped to the standard Power input, including the existing short
Power action and hold-to-sleep behavior. Hold it for about one second to power
on; the boot gesture is ignored until its first release, so it may remain held
until the first screen is visible. A new runtime hold uses the existing 400 ms
software shutdown threshold (about 10 ms when short Power is set to Sleep),
while a continuous 4-second hold remains the PMIC hard-power-off fallback.
QMI8658 and SHTC3 are deliberately not initialized by this target.

System Settings exposes **Sound Feedback** with Off/Low/Medium/High levels;
Medium is the default on this target. Low/Medium map to the previous Medium/High
ES8311 volume values 85/100. High keeps codec volume 100 and applies saturating
1.75x PCM gain (about +4.9 dB). Debounced physical Left/Right presses play
`select`; physical Confirm, Back, and Power presses play `tap` immediately,
before click/double-click/hold business classification. A hold produces no extra
cue. The two 16 kHz mono PCM WAVs are embedded only in this build.

The build opts into `CROSSPOINT_CAP_SOUND_FEEDBACK`. `SoundFeedback` owns cue
selection, settings interpretation, RIFF data-chunk parsing, calibration, and
PCM gain; `HalAudioOutput` owns the AudioManager and board power lifecycle; the
FreeInk AudioManager owns codec/I²S streaming. Adding another speaker board
requires a reviewed calibration and capability opt-in, not a copy of the cue
player. The embedded WAV data chunks are located at runtime, so playback does
not assume a 44-byte header.

When a cue is already playing, the next physical press interrupts and replaces
it at the next PCM buffer boundary; input handling never waits for playback.
The I²S line is primed before the NS4150B is raised, followed by a Waveshare-only
10 ms amp-settle interval so the 10 ms `select` waveform is not clipped. Off
stops playback and powers down the codec, amplifier, and AXP2101 audio rails.
The wake gesture remains absorbed. A screenshot chord may play the constituent
physical-key cues, but screenshot recognition adds no cue of its own.

GPIO4/GPIO6 short presses emit Left/Right on release. Holding either key for
650 ms instead emits and holds Up/Down respectively; releasing it produces only
the matching Up/Down release.

The panel uses the shared SSD1677 driver with a Waveshare-specific configuration.
FULL, HALF, and FAST select the controller's `0xF7`, `0xD7`, and `0xFF`
sequences respectively; HALF writes temperature `0x6A`. The shared asynchronous,
shadow-buffer, and window-refresh paths remain enabled. Normal antialiased page
turns first apply the `0xFF` B/W partial baseline, power the analog rails with a
separate `0xC0` activation, then drive only gray selector pixels through the
Waveshare-owned custom LUT and `0xCC`. The LUT initially matches X4 but remains
independent for panel-specific VCOM/VSH1 calibration. This path uses the existing
strip scratch and framebuffer only—no full-screen buffer or heap allocation.

The first page and periodic cleanup may still use HALF and visibly flash. Normal
pages use only the selector path and do not run a four-gray `0xD7`, 500 ms
settle, or hard-reset sequence. Deep sleep sends `0x10/0x01`; the existing
AXP2101 shutdown path then removes system power.

### 商店图标的黑白底图与灰阶提交

商店中先出现偏黑的图标，再恢复灰阶，是当前四阶图像的两阶段提交。图标文件没有先选二阶再换四阶：
[`IconBundle.h`](../../src/crossstore/IconBundle.h) 的 `iconVariant()` 已按设备能力选择 CSI 中的
`g4` 两位像素资源。商店也不是 `UiListActivity` 子类；
[`AppStoreGrid.cpp`](../../src/activities/apps/appstore/AppStoreGrid.cpp) 的 `render()` 只通过
`PackageIcons::displayFrame()` 提交这一帧，预热字体和 `UiAppHost::renderUi()` 不另行刷新屏幕。

具体链路在 [`PackageIcons.cpp`](../../src/crossstore/PackageIcons.cpp)：`draw()` 在 BW 阶段把所有非白
图标像素画黑，`displayFrame()` 先调用 `displayGrayscaleBase()`，再逐行上传 LSB/MSB 灰阶选择平面，
最后调用 `displayGrayBuffer()`。[`GfxRenderer.cpp`](../../lib/GfxRenderer/GfxRenderer.cpp) 和
[`HalDisplay.cpp`](../../lib/hal/HalDisplay.cpp) 将这两次提交转交显示驱动。
[`CrossMuxSsd1677Driver.h`](../../freeink-sdk/libs/display/FreeInkDisplay/src/driver/CrossMuxSsd1677Driver.h)
对 Overlay 明确声明 `GrayscaleBase::Separate`；对应
[`CrossMuxSsd1677Driver.cpp`](../../freeink-sdk/libs/display/FreeInkDisplay/src/driver/CrossMuxSsd1677Driver.cpp)
的 `displayGrayscaleBaseWithContext()` 会同步刷新 BW 底图。因此，缓存完整、没有联网时也会出现
这段黑→灰过程。常规商店帧的底图采用 HALF `0xD7`，随后灰阶采用外部 LUT `0xCC`；中间可能需要的
`powerOn()` `0xC0` 只开启时钟和模拟电源，没有图像启动位。末尾 `cleanupGrayscaleBuffers()`
仅回写 RED 基线，不是第三次图像刷新。

优化时须区分“缩短中间画面”和“取消中间画面”。当前条带缓存为 256 字节，物理行宽为 100 字节，
可在不增加缓存的前提下探索每次两行的绘制/上传，减少窗口设置和条带遍历；是否改善可见停顿要实测，
它不会减少两次图像激活。真正合并为一次完整四阶提交，需要验证本机面板的 Absolute 或 Combined
波形，并将文字、背景、图标及角标编码到完整目标平面。当前 `crossmuxSsd1677Waveshare397Config()`
明确关闭未经实机验证的 `absoluteGrayscale`，不能只修改能力标志。
[`FreeInkDisplay.cpp`](../../freeink-sdk/libs/display/FreeInkDisplay/src/FreeInkDisplay.cpp) 的现有
`TextOnlyAntiAliasing` 合并路由使用
[`PaperMonoDriver.cpp`](../../freeink-sdk/libs/display/FreeInkDisplay/src/driver/PaperMonoDriver.cpp)
的单个中间灰，不是完整四阶图像替代方案。直接删除 BW 底图、改变其灰像素阈值或伪装为文字上下文，
都会破坏现有 Overlay 的起始墨态或灰阶等级，可能产生灰度错误和残影。

验证应先使用已缓存的同一页排除网络更新，再记录底图、条带上传和最终灰阶的耗时与实际图像激活次数，
同时检查文字、四种灰色、安装角标及翻页后的残影。截图只能检查最终像素，不能证明过渡已消失。
目录、图标或状态真正变化时产生的另一次页面提交由
[`AppStoreNetwork.cpp`](../../src/activities/apps/appstore/AppStoreNetwork.cpp) 的 `pumpRefresh()` 管理；
它与上述单帧内部的黑白/灰阶两阶段不同。

## Physical acceptance gate

- Confirm boot without panic/OOM and successful PSRAM, AXP2101, SDMMC, and RTC initialization.
- Visually check full, fast/windowed, half, and four-gray refreshes; repeat sleep/wake three times.
- Repeat Back, Left, Right, Function single/double/hold, and side Power gestures three times; one gesture must
  produce one action.
- With Sound Feedback at its fresh-install Medium default, confirm physical Left/Right presses play audible `select`
  and Confirm/Back/Power presses play `tap` without waiting for release or gesture classification. Confirm long Power
  plays once at press, while a wake-held Power button remains silent.
- Check Off/Low/Medium/High persistence across reboot and verify the three audible levels are clearly distinct.
- Press buttons rapidly 100 times and switch Sound Feedback levels ten times. Confirm each new cue interrupts the prior
  cue without blocking input, the amplifier does not pop or stutter, and serial free-heap/largest-block readings do not
  trend downward.
- Open an EPUB from SD, turn pages, and confirm progress/settings writes survive reboot.
- Set the RTC, reboot and fully power-cycle, then confirm restored time.
- Check battery percentage and charging; unplug USB, run on battery, shut down, then hold the side key until the first
  screen is visible before releasing it. Repeat three times and confirm the boot gesture never triggers shutdown.
- Confirm USB Serial/JTAG logging and flashing still work after a normal boot. Open File Transfer > USB Drive and
  verify a host can mount the SD card, copy, rename, delete, and read a large file. Safely eject or disconnect while
  idle, then confirm the device reboots Home, remounts the SD card, and opens a transferred EPUB. Repeat three times.
- Enter USB Drive without a connected host, cancel, and confirm the device reboots Home with the SD card mounted.
  Record `ESP.getFreeHeap()`, `ESP.getMinFreeHeap()`, and `ESP.getMaxAllocHeap()` before entry and after reboot; use
  external UART to record the same values while MSC is active when available.

Automated builds and serial logs do not substitute for the visual, button, or
battery checks above. Record incomplete checks as pending rather than accepted.

## AppStore 构建与能力边界（2026-10-24）

`waveshare_epaper_397_store` 复用上述板卡配置，加入 CrossStore Lua 应用宿主、
Tox 和 ESP-NOW 后端，不替换现有驱动，也不改变普通版、nightly 的功能组合。

```bash
python3 scripts/tests/test_waveshare_store_config.py
pio run -e waveshare_epaper_397_store
```

应用包仍使用通用 Lua 包，由 manifest 的 API 版本、`requires` 和权限决定能否安装及访问服务，
不需要另做 Waveshare 专用 Lua 二进制。固件镜像必须使用本机型构建；其发布标识仍为
`waveshare_epaper_397`，固件版本后缀为 `-waveshare-epaper-397`。

| 项目 | 此构建的实际范围 |
|---|---|
| 应用、存档与图标 | 保存在 TF 卡，需可读写的 FAT32 卡；启动后实际挂载成功才提供 `storage` 能力 |
| 显示与输入 | 800×480、4 阶灰阶，沿用按键手势；没有触摸或补光。图标按渲染器能力选用包内灰阶资源 |
| Wi-Fi / wifi_chat / esp_chat | 编译对应后端；Tox 仍检查 PSRAM 总容量至少 4 MiB，联网与后台监听受现有权限控制 |
| rf_chat / LoRa | 此板没有已接入的 LoRa 模组；不启用 LoRa 后端，不向商店声明 `lora` |
| 系统声音 | 保留 ES8311/NS4150B 的物理按键音效；这与 Lua 声音接口是两条路径 |
| Lua 短音、AirTune | `app.sound()` 仍调用 HalBuzzer，未接入此板。Store 构建启用 `CROSSMUX_RADIO`，通过 ES8311/NS4150B 播放 24 kHz 连续 PCM；编译开启且 HAL/PSRAM 可用时声明 `radio_audio`，允许 AirTune 安装。实机音质验收见下文 |
| RTC / IMU / 麦克风 | PCF85063 RTC 初始化成功后可用；板载 QMI8658、SHTC3 和麦克风尚未接入当前应用能力，不声明可采集或可用 IMU |

硬件 `audio` 能力代表已有 codec 驱动，不能据此推断所有 Lua 播放入口已经接通。
对应代码为 [`Package.cpp`](../../src/crossstore/Package.cpp) 的 `deviceCapabilities()`、
[`LuaAppActivity.cpp`](../../src/activities/apps/appstore/LuaAppActivity.cpp) 的 `sound()`、
[`HalAudioOutput.cpp`](../../lib/hal/HalAudioOutput.cpp) 的 Wave `HalRadioAudio` 实现。
未增加录音能力；安装 AirTune 后仍需授予其网络和声音权限。此次按用户要求直接安装
0.1.2 时，仅授予该应用网络与声音权限，保留存档，未授予后台监听或保持唤醒权限。

Flash 仍使用 [`partitions.csv`](../../partitions.csv)：16 MiB，总共两个 0x640000 字节
（6.25 MiB）应用槽，app0 位于 `0x10000`，app1 位于 `0x650000`。
新增环境不分配新 framebuffer 或常驻后台任务；Lua/Tox/ESP-NOW 和电台播放器
沿用已有按需生命周期及资源上限。电台复用 64 KiB PSRAM PCM 环形缓冲及
32 KiB 解码/联网、4 KiB 输出任务栈；新增 Wave 输出只使用 256 字节栈上转换块和
IDF 分配的 1,920 字节 DMA 样本区（另有驱动描述符开销），停止时释放 I²S 通道。
Lua 主脚本上限 32 KiB、VM 上限 128 KiB，
包上限 1 MiB、目录 JSON 上限 24 KiB。固件是否能装入槽、运行时内部堆余量及 PSRAM
占用，分别以目标构建结果和实机日志为准，不能由 8 MiB PSRAM 标称值代替。

### 硬件证据与待验项

核对型号为官方 [ESP32-S3-ePaper-3.97](https://docs.waveshare.com/ESP32-S3-ePaper-3.97)，
不是其他 Waveshare 3.97 英寸模块。该产品页和
[原理图](https://files.waveshare.com/wiki/ESP32-S3-ePaper-3.97/ESP32-S3_e-Paper-3.97-schematic.pdf)
确认 N16R8、屏幕、SDMMC、codec 与外围芯片；当前材料把 PMIC 标为 TG28。
厂商仓库固定提交 `9b12d40731a80213b927ee8a421cae4082952819` 的
[axp_prot.cpp](https://github.com/waveshareteam/ESP32-S3-ePaper-3.97/blob/9b12d40731a80213b927ee8a421cae4082952819/ESP-IDF/08_ESP32-S3_e-Paper-3.97/components/axpPower/axp_prot.cpp)
仍使用 XPowers AXP2101，设置 ALDO1/2/3 为 3.3 V、按住 1 秒开机和 4 秒强制关机；
[AXP2101Constants.h](https://github.com/waveshareteam/ESP32-S3-ePaper-3.97/blob/9b12d40731a80213b927ee8a421cae4082952819/ESP-IDF/08_ESP32-S3_e-Paper-3.97/components/axpPower/src/REG/AXP2101Constants.h)
定义地址 `0x34`、ID 寄存器 `0x03` 和预期值 `0x4A`，与
[`Waveshare397Power.cpp`](../../lib/hal/Waveshare397Power.cpp) 的现有初始化约束一致。
这说明驱动选择有官方代码依据，不证明所有 TG28 批次完全等同 AXP2101；本次不放宽 ID
检测，不修改电压或上电顺序。实机需记录 PCB 版本并确认 PMIC 初始化日志。

本次配置回归检查环境继承、单一设备标志、USB/SDMMC 宏、chat 构建脚本、系统音效资源、
显式固件版本和 16 MiB 分区布局。硬件验证仍需完成上面的 Physical acceptance gate，
另外检查：商店已缓存列表与离线启动、安装/升级后启动应用、4 阶图标与按键导航、
wifi_chat / esp_chat 双机收发，以及后台通知退出与休眠后的资源释放。
2026-10-24 本地 `waveshare_epaper_397_store` 构建通过，固件文件为 6,306,272 字节，
距单个应用槽上限还剩 247,328 字节；构建报告静态 RAM 使用 119,412 字节。
这些数字不代表运行时内部堆余量。已对用户连接的设备（MAC 尾号 `F6:A8:64`）
完整读取并独立校验 16 MiB Flash，原镜像是已有 CrossPoint/CrossMux 分区布局；
重新连接后已通过 USB 自动进入下载模式，核对 MAC 及分区表后仅写入 `app0`（`0x10000`），
并独立校验通过，保留 NVS、OTA 状态和 TF 卡内容。此次验证镜像 SHA-256 为
`2d69ad25c577832b430b9f3863829fd3e78d6f337206cd9f8f5cc9d4310a18b8`。
该镜像早于后续格式与静态检查收尾；正式构建另以发布清单记录源码和镜像版本。

启动日志确认 AXP2101、外接供电及充电、外部 RTC、硬件型号和 SSD1677 初始化；
屏幕完成刷新，串口截图显示 `SD card error`，未观察到 panic。
SDMMC 可以读取卡的第 0 扇区且有 `55AA` 签名，但卷挂载失败
（第一分区类型 `0x00`、起始 LBA `0`）。读卡器检查确认这是一张整卡 FAT32
（superfloppy），不是有 MBR 分区的卡；macOS 正常读取文件，簇大小为 8 KiB。
当前 SDK 只尝试 SdFat 默认的第一个分区，缺少 `part=0` 的整卡挂载路径。
已只读备份并校验用户文件，未格式化或修复卡。

SDK 提交 `f2a8599b4e0d2c6b5dfc04dc334846718ce16e0c` 保留 MBR 优先，失败时再由
SdFat 验证整卡文件系统；新增真实 SdFat 2.3.1 测试覆盖两种布局、MBR 优先、文件读取、
重新挂载和 14 个无效介质案例。修复不新增堆分配、缓存或任务，不改变 SPI 卡路径。
修复镜像为 6,307,040 字节，SHA-256 为
`373f00bd1ba45797f20304f52bd1dbe32036f1e10d105ca74221dc90cd334e9e`，
同样仅写入 `app0` 并独立校验通过。原卡随后成功挂载；用户确认商店、按键和 EPUB
阅读均正常。串口也观察到 Home、AppsMenu、AppStore，以及目录取得成功。
Home 内部空闲堆为 148,328 字节、历史最低 136,528 字节、最大块 102,388 字节，
PSRAM 空闲 6,834,108 字节；这些是该次运行的检查点，不代表长期峰值。
音效、聊天双机收发、USB 文件传输压力与休眠循环仍待专项验收。

全仓格式检查、cppcheck 及原有 8 个目标构建通过；宿主测试初次发现 3 个过时测试接口，
修正后相关测试复测通过。PaperS3 端口与电源回归通过。Read Pico 事务测试依赖 GNU
链接器的 `--wrap`，macOS 无法执行该项，三设备发布工作流在 Linux 上单独检查。
三个 Store 环境均已显式构建通过；构建或校验通过不能替代未完成的实机项目。

### AirTune 扬声器输出（2026-10-24）

`HalRadioAudio::OutputKind` 区分 Mono 蜂鸣器与此板的扬声器；Player 分别选择
Buzzer 和 Standard 调理曲线。此板保留 120 Hz 高通、2.75 倍补偿及峰值限制，
随后以 0–100% 线性音量送入固定 0 dB 的 ES8311，不改变声音时长、语速或音高。
实际 I²S 输出为 24 kHz、16 位，单声道样本复制到左右声道。16 kHz 按键音与
24 kHz 电台切换时同步设置 MCLK 和 codec DAC OSR；依据及寄存器见
[SDK 音频说明](../../freeink-sdk/docs/waveshare-audio-stream.md)。

短音和电台共用 `HalAudioOutput` 所有权。电台开始前终止短音，播放中按键音不会抢占
I²S；电台停止后释放通道、关闭功放和音频电源轨。无法停止的旧任务保留其通道与电源，
避免另一任务在它仍访问硬件时拆除资源。PMIC、RTC 与 codec 控制使用静态递归总线锁，
保护组合 I²C 操作及电源寄存器的读改写；每块 PCM 写入不持有 I²C 锁。

实际 SDK AudioManager、HAL/PMIC 和 Player 宿主测试覆盖音量、部分写入、超时、
初始化失败、流中短音拒绝、停止与恢复、并发 PMIC 电源读改写，以及两种调理曲线；
ASan/UBSan 检查通过。测试不能证明实机音质或功耗。

本轮候选固件为 6,337,360 字节，单槽剩余 216,240 字节，静态 RAM 使用 119,508 字节；
SHA-256 为 `0346ff8b76a89392e700e6e47883a556e11ed2f69c1239b1c60ec3e4629e4e16`。
已仅写入 `app0`，独立读取校验通过，并正常进入简体中文 Home。
AirTune 0.1.2 直接安装到 TF 卡并校验，保留应用数据。用户已确认此前物理按键音可闻；
本轮在 AirTune 100% 音量下反馈“主要是声音小，声音基本干净”。因此电台输出链路已
打通，响度验收未通过，不能宣称已解决音量问题。串口首次播放日志显示输出成功、无
underflow，停止 error=0；输出任务剩余栈 2,280/4,096 字节、解码任务 10,272/32,768
字节，运行期内部堆历史最低 64,716 字节。它们是单次检查点，不是长期压力测试。

针对低响度，复核[官方原理图](https://files.waveshare.com/wiki/ESP32-S3-ePaper-3.97/ESP32-S3_e-Paper-3.97-schematic.pdf)
确认功放接主 VCC3V3，GPIO39 只控制启停，输入两路串联 150 kΩ；未发现可由软件调节
的模拟增益接口。[官方产品规格](https://www.waveshare.com/esp32-s3-epaper-3.97.htm)
标配 8 Ω / 1 W 扬声器，额定承受功率不等于板上实测输出功率。
当前上电、HP drive、EQ bypass 与官方例程一致；官方默认 DAC 音量 70% 对应
`0xB2`（−6.5 dB），当前流使用 `0xBF`（0 dB）。未找到可证实的模拟路径配置错误，
也未测得扬声器端电压或声压，不能断言已经达到硬件上限。

对一段 9.9265 秒公开电台流作临时诊断，真实解码/重采样/DSP 共输出 238,237 个
24 kHz 样本；Standard 将 RMS 从 −20.66 提至 −13.53 dBFS，输出峰值 0.720 FS，
无硬削波，输入输出样本数一致。临时把补偿从 2.75 倍提高至 4 倍、保留现有限幅参数，
RMS 再提高 1.73 dB，但限幅涉及样本从 13.1% 增至 45.1%。它只证明该片段的电平变化，
不证明实机声压、无压缩副作用或所有电台都能改善，因此未把临时参数写入生产代码。
原广播与 PCM 不随源码发布。后续应结合实际扬声器输出测量与同源试听判断，不能通过
提高供电电压或强行增加 DAC 正增益来代替诊断。

音频集成后的 `./bin/ci-check` 已通过全仓格式、默认目标 cppcheck、8 个硬件构建与
1,143 项宿主测试中的 1,142 项；`WifiIdentityQr` 因缺少可选解码器跳过。
脚本随后仍因 macOS 链接器不支持 Read Pico 测试的 `--wrap=calloc` 而退出，此项由
发布工作流的 Linux prepare 阶段检查，不记作本地全部通过。Wave 专项 cppcheck
另报告 6 项已有低级风格提示，均位于未改动的 HalClock/Player 逻辑；未发现新增高/中级项。
