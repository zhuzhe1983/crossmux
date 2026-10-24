# Native chat apps (experimental)

`rf_chat`, `wifi_chat` and `esp_chat` are compiled-in, opt-in apps. Build `papermono_chat`
for PaperMono with SX1262, or `papers3_chat` for Wi-Fi/ESP-NOW chat on PaperS3. Ordinary
release/C3 builds do not fetch or link either protocol library. The app catalog
checks hardware requirements as well as the user's visibility preference;
unsupported apps cannot be launched by their ActivityManager routes.

## Protocol and UI

- `rf_chat`: single-hop public room, nearby discovery, addressed text and delivery
  ACKs. **No encryption or peer authentication**, including addressed messages.
  It does not speak the reference MonoCard gist's wire protocol, LoRaWAN, mesh,
  or Meshtastic. Default bench profile: 868 MHz, BW125, SF9, CR4:5, sync 0x12,
  10 dBm, 12-symbol preamble; `CROSSMUX_RF_FREQUENCY_MHZ` selects another matching
  board/antenna profile. Addressed packets have at most three attempts, with
  randomized 4–5 second intervals; broadcast only means "sent", not delivered.
- `wifi_chat`: pinned native c-toxcore 0.2.23, one-to-one text, LAN discovery and
  public bootstrap nodes. On the same Wi-Fi, open **Discover & settings → Discover
  nearby devices** on both devices, compare the displayed eight-character device
  codes, and send a friend request. The recipient reviews and accepts it on the
  conversation list. A manual full-ID entry remains a fallback. No automatic
  acceptance. Existing Tox clients
  should be interoperability-tested before claiming compatibility. Voice, files,
  groups and Chinese composition are outside this
  initial version. The existing keyboard can enter ASCII; received UTF-8 text
  uses the configured content font.

All three apps require a writable TF card. Open them from Apps. Tap "My ID" to see the
address. Message length is 192 UTF-8 bytes (64 ordinary Chinese characters).
Only eight contacts and the latest 24 messages are held in each session. The
conversation list shows short device aliases and latest previews. Threads use
left/right bubbles, a fixed compose entry, older/newer controls and touch swipes.
Full IDs live in details; tapping a failed message retries the existing record.
A rejected send retains its text as an editable draft for the current thread.
RF public-room transmissions explicitly say they have no delivery receipt.

## ESP-NOW public and group chat

`esp_chat` uses the chip's existing ESP-NOW support, without a router, internet,
Tox or LoRa. The initial version is single-hop, plaintext and unauthenticated;
group numbers are routing labels, not passwords. All peers must run this
CrossMux protocol on the same Wi-Fi channel. A newly started offline STA uses
channel 6. If Wi-Fi is already running, its channel is preserved; when connected
to an AP, ESP-NOW follows that AP. The home/thread status shows the actual
channel and recent nearby count. This version has no channel selector.

The public room is always joined. In **Discover & settings → Create / join group**,
enter the same number (1–999999) on each device. Up to eight joined groups persist
on TF; the first version has no leave-group action. Group histories show the
sender's short MAC alias. The full 12-digit local STA MAC is available under
My ID. Presence is advisory, expires after 45 seconds, and tracks at most eight
devices per session.

Packets have a 32-byte `CE`, version-1 header and at most 192 UTF-8 payload bytes
(224 bytes total, within ESP-NOW v1's 250-byte limit). Header fields are type,
room, source MAC, boot nonce, message sequence and ACK recipient. Receivers
validate the actual source MAC, group membership, frame bounds and UTF-8.
This comparison detects malformed/mismatched frames, not a malicious spoofed MAC.
Duplicate suppression retains the latest 32 source/boot/sequence tuples.
Receipts are sent only after persistence; duplicates re-ACK without adding a
second record. ACKs are jittered 80–399 ms and queued in eight fixed slots.

Group sends retry up to three times, 1.4–1.8 seconds apart. Public sends transmit
once. A successful MAC transmission without receipts is only “broadcast sent”
for public messages; an unacknowledged group message fails and can be retried.
Up to eight distinct matching ACK senders are counted and displayed explicitly.
That count does not imply that every group member received the message.
No forwarding, retained offline delivery or wake-on-radio is provided.

`HalEspNow` owns the SDK boundary. Its callbacks only copy into a fixed eight-frame
queue under a short critical section; the worker does storage and UI-facing work.
The ESP backend is a single checked allocation of about 4.3 KiB, freed on stop,
plus an 8 KiB worker stack and the existing bounded chat session. A stack backend
would consume most of the worker stack; embedding it into every RF/Tox session
would waste RAM. Per-message sender/receipt metadata adds 12 bytes per record
(288 bytes per 24-message snapshot). This app is enabled only in S3 experimental
chat targets; C3 release builds do not start a listener or allocate a session.

Host tests compile the production ESP backend against mock transport/storage/
session boundaries and cover room filtering, ACK matching, duplicate handling,
write failure, retransmission, callback timeout, channel loss and group persistence.
Hardware acceptance must separately check public/group bidirectional messages,
background notification navigation and normal sleep.

Wi-Fi **My ID** displays a black/white QR code with `tox:` followed by the complete
76-character address, matching [qTox's profile QR format](https://github.com/TokTok/qTox/blob/master/src/widget/form/profileform.cpp).
The complete ID is also available as a single noninteractive text block with a
line-wrap explanation. The encoder is the existing pinned QRCode library,
version 5 / medium error correction, integer module scaling and a four-module
white quiet zone. The 172-byte module buffer is activity-owned and reused;
encoding runs only when opening My ID. No new dependency or per-frame allocation
is added. Camera-less readers use nearby pairing rather than scanning each other.

Nearby pairing is a CrossMux discovery extension on UDP 33446. An explicit search
opens one nonblocking socket for 60 seconds, broadcasts the public Tox address,
DHT key and transport port once a second, and receives at most four 78-byte
datagrams per worker tick. Eight peers fit in a fixed directory; entries expire
after 15 seconds. Packet version, exact length, address checksum, nonzero keys
and port are validated. The sender's actual IPv4 address seeds Tox's DHT so a
LAN-only pair can connect without public bootstrap access. Search does not add
friends and stops at timeout or transport shutdown, including sleep. The fixed
directory adds 356 bytes to each session and its foreground snapshot; temporary
packet buffers live on the existing 16 KiB worker stack. Broadcast visibility is
limited to the search window. Router client isolation can prevent discovery.
The short code is a convenience identifier, not a cryptographic authentication
code; use the complete public-key identity when stronger verification is needed.

RF listeners emit a heartbeat every 15–16 seconds, announce normal exit with a
best-effort `Bye` frame, and expire presence after 45 seconds without traffic.
Known-offline addressed sends fail immediately. An unanswered addressed send
fails after three attempts; its retry uses the original sequence so a peer that
already received it can re-ACK without showing it twice within that boot session.
The 28-byte protocol v1 header now accepts type 4 (`Bye`, no payload); update both
peers. Existing hello/text/ACK encoding is unchanged.

## Optional background reception and system messages

Open **Discover & settings → Listen in background** inside any chat app. Default
is off. Three flags (RF, Tox, ESP-NOW) are saved atomically as
`/.crosspoint/chat-background-v2.txt` (three ASCII 0/1 flags plus newline).
When v2 is absent, the two v1 RF/Tox flags are loaded with ESP disabled;
malformed/missing files mean off. The service
owns at most one session per protocol; foreground activities borrow it. Closing
an app releases the view while an enabled listener continues receiving during
reading. Child keyboards also keep receiving. Disabling background reception
closes the listener when the last view exits.

System messages coalesce incoming messages and friend requests by app and peer
into at most eight notices. Friend-request notices use translated UI text and
open the explicit acceptance screen after the contact snapshot is loaded. The shared header and reader status band show a tappable unread badge.
It opens the system-message list; selecting a notice opens the matching app and
conversation, with Back returning through the list to the prior activity. The
current conversation is marked read; other conversations remain unread. Notices
are RAM-only and clear on reboot/deep sleep; message history remains on TF.
The badge overlays the center of its status/header band. Incoming bursts request
at most one global repaint per five seconds; no auto-navigation, wake-up or idle
sleep suppression is added. The existing e-ink page render is reused, so a notice
can cause a page refresh rather than a hardware partial-region refresh.

The common sleep hook joins/deletes all workers, powers RF down, saves Tox and
releases transport memory **before** storage/power teardown. USB Drive also stops
listeners before granting exclusive SD ownership. Wake reloads enabled flags;
Wi-Fi reconnects to the last saved network without blocking, at most three
attempts spaced 30 seconds apart. It avoids AP mode and only initiates reconnect
from reading/Home/Apps (including INX Recent); other foreground network owners
can interrupt Wi-Fi. The first reconnect loads the bounded credential store if
it is not already in memory; boot otherwise leaves that store unloaded.
The UI reports loss of connectivity instead of promising background delivery.
Normal idle sleep remains enabled. No messages can be received during sleep.
The service keeps its owned Wi-Fi radio active while either Tox or ESP-NOW still
has a listener. Stopping one does not turn off the other's radio.

## Storage and resources

- RF/Tox: `/.crosspoint/{rf_chat,wifi_chat}/messages-v1.txt`, append-only text
  with bounded records and a 128 KiB rotation threshold. One previous log is
  retained. Only complete valid records in the current file are restored;
  interrupted pending sends become failed. Message contents are plaintext on SD.
- ESP-NOW: `/.crosspoint/esp_chat/messages-v2.txt` adds source MAC and receipt
  count; `groups-v1.txt` stores joined groups. Limits/rotation match the RF/Tox
  logs. See [file-formats.md](../file-formats.md#experimental-chat-persistence).
- Wi-Fi identity: `/.crosspoint/wifi_chat/identity.tox`, `.part` during writes,
  `.previous` retained across replacement. Identity and contacts are sensitive.
  Recover from the previous file after an interrupted rename; never silently
  generate a different key over an invalid existing checkpoint. Save/load buffers
  use at most 256 KiB PSRAM and are released after each checkpoint.
- A session and activity each hold bounded message/contact/list buffers; allocated
  once with `makeUniqueNoThrow`, not static because reading must reclaim disabled
  sessions. Background RF+Wi-Fi may coexist: an 8 KiB RF stack, a 16 KiB Wi-Fi stack and two
  fixed session windows, and the capped Tox heap. Notification storage is a fixed
  approximately 2.2 KiB in opt-in builds. Opening a notification stacks its small
  list and a chat view over the reader, so runtime largest-block validation is
  required for that peak. Ordinary/C3 images exclude the service and inbox.
  Worker stacks use internal RAM. Tox uses an allocator capped at 3 MiB
  **PSRAM only**, including realloc's old+new peak, and has no internal fallback.
  Actual heap/stack and Flash figures belong in the validation record below.
- Tox sources are checksum-pinned in `scripts/configure_chat.py`. It applies two
  small local patches: 1024 rather than 32768 local packet queue slots (power of
  two, unchanged wire format); GNU `strerror_r` ABI handling for ESP-IDF newlib.
  `scripts/build_chat.py` compiles using the configured ESP toolchain. ESP-IDF's
  bundled libsodium supplies crypto. The Tox-only compile environment renames
  `os_random` to `crossmux_tox_os_random`: ESP-IDF's Wi-Fi library exports an
  incompatible function with the same original C symbol. Do not apply this
  alias to the application or SDK environment. RadioLib is pinned to 7.8.1.
- RF power, expander reset/antenna and independent SPI live in HalRadio and the
  SDK PaperMono board support. No interrupt handlers call SPI/I2C or allocate.

## Validation

Host tests cover UTF-8 limits, malformed/truncated frames, unaligned decoding,
maximum frame size, bounded duplicate tracking and the app capability matrix:
`cmake --build build/test --target ChatProtocolTest ChatStoreTest` followed by
`ctest --test-dir build/test -R "ChatProtocol|ChatCapabilities|ChatPresence|SystemInbox|ChatStoreTest|WifiNearby|ToxLinkage" --output-on-failure`.
Storage tests cover torn records, status updates, pending recovery, retention,
rotation and write failure.
`test/chat/test_wifi_identity_qr.py` compiles the actual firmware QR helper against
the downloaded QRCode source, rejects invalid IDs, and checks its capacity. With
Pillow and zxing-cpp installed in the test Python environment, it independently
decodes several pixel scales and compares the exact complete `tox:` URI.

Physical acceptance must record boot, RF discovery and bidirectional messages,
retry/ACK behavior, Wi-Fi identity persistence, friend approval and bidirectional
messages, keyboard-open receiving, app exit and sleep. Host/build passes alone
are not physical acceptance. Run the same build on both RF peers. RF parameter
and device ownership matching is the operator's responsibility.

Reference design: [MonoCard gist](https://gist.github.com/zhuzhe1983/e5fac99789fdd492fea1932b406067cc).
Board wiring: [M5Stack PaperMono LoRa example](https://docs.m5stack.com/en/arduino/papermono/lora).

### 2026-10-18 bench record

Both user-owned PaperMono boards were identified as 16 MiB Flash / 8 MiB PSRAM,
backed up in full and explicitly verified before flashing. Both write digests
and independent verify-flash passes matched. One board booted CrossMux; the
other reported a readable MBR whose first partition was type 0x83 (Linux), which
cannot be mounted by the FAT/exFAT storage stack. With subsequent explicit user authorization, its complete disk-image backup
was verified, the card reformatted to MBR/FAT32, and a 1 MiB write/readback plus
filesystem check passed.

`default` (ESP32-C3), ordinary `papermono`, `papermono_chat` and `papers3_chat`
compiled successfully. The exit-fix PaperMono chat image written to both boards used 6,314,486 / 6,553,600
Flash bytes and 132,764 / 327,680 static RAM bytes; these are linker figures,
not runtime free-heap or protocol peak measurements. Nine new host cases passed;
full CTest: 1053 / 1057 passed, with the same four pre-existing macOS test-harness
compiler failures (WeReadConnection, ReadingUiRegressions, UiFontFallback,
FileBrowserImages). Targeted chat cppcheck and formatting passed.

Local artifacts and per-device flash logs: `build/chat-validation/`. Runtime
heap/stack peaks and Tox interoperability still require physical measurements.

After card recovery, both devices entered `rf_chat` and logged SX1262 ready
with the same 868 MHz profile. Full `pio check -e papermono_chat` passed with
low/medium/high fail gates. Fixed-array scans carry explicit style annotations;
the Tox allocator callback has an indirect-call annotation. The exit-fix image initially remained unchanged during those checks. The user
confirmed both devices showed connected, discovered each other and
received messages in both directions. Public-room screenshots also showed the
messages exchanged. The user reported poor handling after one peer exited and
requested a conversation UI and optional awake-only background reception.

The subsequent UI/background revision is under validation. Host coverage adds
exit-frame validation, presence expiry across `millis()` wrap, explicit exit and
return, and notification coalescing/read isolation/capacity/counter bounds.
Tox traffic and the new background/sleep/notification routes are not yet accepted
on hardware. Session startup logs now report internal/PSRAM free and largest
blocks; shutdown logs report the worker's remaining stack.

The UI/background image (`papermono-chat-background.factory.bin`, SHA256
`5913de68513cdca0f0ae7025ece57848b8834456465f9bf76bcf0cf768fbff1b`) was then
written to both boards and independently verified. Application Flash: 6,328,526
/ 6,553,600 bytes; static RAM: 135,164 / 327,680 bytes. Final PaperMono and
PaperS3 chat builds passed; the default C3 regression build also passed.
Fourteen chat/protocol/presence/inbox/storage cases and the targeted power
regressions passed. The user subsequently confirmed that both boards boot normally
and display the new conversation UI. Background notification delivery/navigation,
offline retry and awake/sleep transitions remain separate pending hardware checks.

Both updated boards subsequently cold-booted, mounted SDMMC and entered RF chat.
RF startup reports a 10,320-byte session; A had 120,736 bytes internal free
(largest 77,812), B had 117,580 (largest 73,716). PSRAM free/largest was
7,751,276/7,733,236 on A and 7,702,616/7,602,164 on B. These are RF foreground
snapshots, not Wi-Fi-plus-reader peak measurements. Evidence: `background-boot-a/b.log`.

The user then confirmed the BG1 acceptance flow: RF background reception while
reading, system-message indication and notification navigation passed. Offline
retry and sleep transitions remain pending. Their first `wifi_chat` trial crashed;
Wi-Fi/Tox hardware acceptance is failed pending diagnosis and a verified fix.

Serial reproduction located a `LoadProhibited` in `rng_bytes`, called by
`crypto_new_keypair` during `new_dht`/`tox_new_testing` initialization. The
matching ELF hash starts `351d0c787`; the invalid RNG pointer was `0x9a433259`.
Its link map resolves `os_random` to `libwpa_supplicant.a(os_xtensa.c.obj)`, whose
return value is an integer, while Tox expects `const Random *`. An exported-symbol
comparison found this was the sole Tox/ESP SDK name collision. The Tox-only symbol
alias corrects that ABI collision without replacing libsodium's secure RNG.
`test/chat/test_tox_linkage.py` links the actual pinned Tox RNG sources with a
Wi-Fi ABI fixture: the original symbols reproduce the collision and the isolated
build preserves both calls. It needs the pinned dependency cache from a chat
build and explicitly skips when those sources are absent.

Both fixed chat targets built, with 11 selected linkage/protocol/capability/store
checks passing. PaperMono uses 6,365,634 / 6,553,600 application Flash bytes and
136,316 / 327,680 static RAM bytes. The corrected link includes the previously
omitted libsodium RNG initialization path; compared with the crashing image,
Flash grows by 37,108 bytes and static RAM by 1,152 bytes. No task stack or
application buffer was enlarged. The factory image SHA256 is
`3a43bfb33cd997d1c2b7d256dd9081dc8eb0dc6d66e17e1f063981514558fd40`.
Both boards were written and independently verified. The user confirmed Wi-Fi
chat no longer crashes and My ID opens. The crash board logged successful Tox
startup with 918,324 bytes allocated by the protocol, internal free/largest
46,532/31,732 bytes and PSRAM free/largest 5,719,264/5,505,012 bytes. These are
startup snapshots, not combined background-plus-reader peak measurements.
Friend approval, bidirectional Tox traffic and exit/reopen identity persistence
still need hardware acceptance. USB auto-download and watchdog reset succeeded,
avoiding a manual power cycle. Local per-device evidence remains in
`build/chat-validation/`.

The subsequent nearby-pairing/QR build passes both chat targets: PaperMono
6,372,410 / 6,553,600 application Flash bytes and 136,308 / 327,680 static RAM;
PaperS3 6,257,447 Flash bytes and 85,604 static RAM. Nineteen selected host tests
passed, including malformed discovery frames, deduplication, bounded capacity
and expiry across clock wrap. Two QR tests passed, including independent exact
URI decoding at 2/4/6/10 pixels per module. The user confirmed the QR and complete
ID are correct and phone scanning succeeds. The first physical pairing trial
could add contacts but showed them offline and failed to send.

The next network revision adds four public nodes with separate UDP and TCP ports
(including TCP 443), rather than using port 33445 for every relay. Warning-level
Tox logs and network/friend/add/send callbacks distinguish local request submission
from remote acceptance and friend connectivity. Both Mono application images were
written and independently verified, SHA256
`3e8867ea11c33011025ab6c21be7eb22615943ac45161b12346e16d3dfa9959e`.
The trace first records a TCP network connection while the friend is offline
(`TOX_ERR_FRIEND_SEND_MESSAGE_FRIEND_NOT_CONNECTED`), then a UDP friend connection
and incoming text on both devices. The user confirmed both online and bidirectional
messages after retry. This validates this revised image on this network; it does
not isolate a particular old node/port as the sole cause.

C1Max scanning a Mono QR initially produced no visible incoming request. The
saved public key and nospam bytes were compared with an independent decode of
Mono's actual screen QR and matched. The C1Max Wi-Fi interface later reported
associated but could not reach even the local gateway; TCP handshakes also timed
out. Reassociating Wi-Fi restored gateway reachability. With the original C1Max
client reopened, **both physical Mono logs recorded the C1Max friend-request
callback** (public ID prefix `100CF669`). No protocol patch was required for this
request exchange. This identifies a network outage during the investigation,
not proof that every earlier delay had the same cause. Explicit Mono acceptance
and bidirectional C1Max/Mono text still await physical confirmation.

A separate isolated host test linked the modified 0.2.23 core (1024 queue slots)
against the C1Max project's 0.2.22 core, with public networking disabled. Friend
requests, explicit test acceptance, bidirectional UTF-8 and receipts passed.
This verifies core-version interoperability, not the complete device/UI flow.
Local evidence: `wifi-c1-followup-a/b.log`, `c1-restored-network.json` and
`tox-version-interop.log` in `build/chat-validation/`. Experimental C1Max routing
and TCP-only services were stopped; the original installed client/service and
identity remain in use. An unvalidated onion-route experiment was removed from
the firmware inputs.

Concurrent RF/Wi-Fi background reception plus OPDS recorded an internal-heap
minimum of 768 bytes on one Mono (no crash captured). RF shutdown repeatedly
reported 13,124 bytes unused on its former 16 KiB stack. The follow-up uses 8 KiB
for RF, releasing 8 KiB internal RAM while preserving the 16 KiB Tox stack
(Tox's recorded remaining stack was about 4.3 KiB). New-build RF stack and combined
OPDS peak acceptance remain pending; this is not a claim of verified memory
margin on the revised image.

Friend operation results are now separate from network status in the snapshot:
queued, request submitted, accepted, existing contact, invalid/self ID, contact
limit, storage failure or general failure. Connection ticks cannot erase an add
failure; the matching successful connection clears a waiting hint. Selecting an
existing nearby friend opens the thread (or reviews its pending incoming request)
instead of silently submitting a duplicate. These bounded scalar fields add no
new heap allocation. The UI follow-up is under build/hardware validation.

The friend-request notification/reconnect/RF-stack follow-up built successfully
for `papermono_chat` and `papers3_chat`, passed all 20 selected host tests and
PaperMono cppcheck with no defects. Its PaperMono application SHA256 is
`62cfa6e5d5819655b72256301dc4ff63ab1b8f967420d018ada59a0a7ad0e026`.
It has **not been flashed or physically accepted**; the devices retain the
previous network-diagnostic image while explicit C1Max friendship acceptance
is pending. C1Max public Tox connectivity still fluctuated during observation,
even after local gateway reachability recovered.

### 2026-10-20 ESP-NOW candidate

`papermono_chat`, `papers3_chat` and the C3 `default` target built successfully.
The 31 selected chat/protocol/storage/inbox host tests passed, including ten
ESP protocol/backend cases and two ESP persistence cases. PaperMono cppcheck
reported no defects; the repository formatting check passed.

The PaperMono application is 6,433,696 bytes, SHA256
`20c4fc3e9a3e66856dcbf001f47ff2aa184901ec07930e6bd35de8e7edcff4b5`.
The linker reports 136,428 static RAM bytes; the padded binary leaves 119,904
bytes in the 6,553,600-byte app slot. Both physical Monos (MAC endings 7014
and BF04) had matching partition tables, received an app-only write at
0x10000, passed an independent readback verification, and restarted normally.
The first screenshot from BF04 shows the `esp_chat` app in the grid.

Both real devices subsequently logged ESP-NOW initialization on channel 1 and
`Ready=1 kind=2`. With their existing listeners running, internal heap at ESP
startup was 37,640/40,340 bytes free, with a 22,516-byte largest block on each.
PSRAM free/largest blocks were 6,531,492/6,422,516 and 6,572,492/6,553,588 bytes.
BF04's normal ESP worker exit reported 4,656 bytes of unused stack. No panic was
captured in this observation window. These are observed points, not a proven
worst-case budget for concurrent readers/downloads.

On 2026-10-21, the user confirmed normal ESP chat communication between the two
physical Monos. This establishes device-to-device message interoperability for
this candidate. The report did not separately identify group routing, receipt
counts, or ESP background notification/sleep checks; those acceptance items
remain unconfirmed.
Evidence and immutable binaries: `build/chat-validation/esp-chat-20261020/`.
No upstream PR is submitted by this flashing step.

### 小语音消息接入检查（未实现）

2026-10-23 对当前 CrossMux、固定版本 FreeInk SDK 和相邻 CrossStore
工作区作了只读审计。**没有启动麦克风、录制声音或实测语音音质，也没有
新增录音、播放、编解码或附件传输代码。不能把当前状态描述成“打开 mic
开关即可使用语音消息”。** 下列行号是本次检查时的位置。

现有权限与能力并不等价：

- [`Permissions.h`](../../src/crossstore/Permissions.h#L13) 第 13–24 行只有
  `sound` 声音播放权限，没有 `microphone`/录音权限。
  [`LuaAppActivity.cpp`](../../src/activities/apps/appstore/LuaAppActivity.cpp#L436)
  第 436–440 行同时检查应用声明与已授予权限，再把 `app.sound(cue)` 交给
  `HalBuzzer`。它只接受 `click` 和 `woodfish`，不接受文件名或任意 PCM
  （[`HalBuzzer.cpp`](../../lib/hal/HalBuzzer.cpp#L36)，第 36–55 行）。
- `audio` 是硬件能力标志，不是单独的扬声器权限。
  [`Package.cpp`](../../src/crossstore/Package.cpp#L41) 第 41–46 行也把
  支持短 PCM 的蜂鸣器计入 `audio`，所以 `app.has("audio")` 不能证明该设备
  支持语音文件播放或有音频 codec。
  [`Package.h`](../../src/crossstore/Package.h#L21) 第 21–24 行尚无麦克风能力项。
- 相邻 CrossStore 中三个聊天包的权限只有通信、通知和后台：
  [wifi_chat manifest](../../../CrossStore/packages/wifi_chat/manifest.json)
  第 25–29 行为 `network/notifications/background`；
  [rf_chat manifest](../../../CrossStore/packages/rf_chat/manifest.json)
  第 24–28 行与
  [esp_chat manifest](../../../CrossStore/packages/esp_chat/manifest.json)
  第 24–28 行为 `radio/notifications/background`。它们均未声明 `sound`，
  也没有尚未定义的录音权限。

Mono 的麦克风已有底层描述，但录音链路尚未接通。
[`BoardConfig.h`](../../freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h#L1153)
第 1153–1156 行定义 PDM CLK=GPIO45、DATA=GPIO46，供电来自 IOE1 IO12，
因此 `MicConfig.enable` 为未分配。
[`M5Ioe1.h`](../../freeink-sdk/libs/hardware/BoardConfig/include/M5Ioe1.h#L53)
第 53、58–59、127–144 行把该供电位包含在启动时拉低的输出集合内。
SDK 的 [`Microphone.cpp`](../../freeink-sdk/libs/hardware/Microphone/src/Microphone.cpp#L19)
第 19–22 行只处理直接 ESP GPIO，遇到未分配使能脚就返回；第 26–77 行虽能配置
PDM RX，但没有启用 Mono 的 IOE1 电源。
`src/`、`lib/` 没有录音调用或 `HalMicrophone`，PlatformIO 也未登记 Microphone
库依赖。现有 [`HalSystem.cpp`](../../lib/hal/HalSystem.cpp#L31) 第 31 行只读取
板型声明；商店因此显示“有硬件，尚无应用录音接口”
（[`AppStoreActivity.cpp`](../../src/activities/apps/appstore/AppStoreActivity.cpp#L471)，
第 471–472 行）。官方 [Mono MIC 示例](https://docs.m5stack.com/en/arduino/papermono/mic)
同样明确使用 GPIO45/46 与 M5IOE1_PIN_12，能够作为后续接线和供电验证依据。

Mono 当前播放通路是 GPIO42 驱动的蜂鸣器，没有板载 I2S 音频 codec
（[`BoardConfig.h`](../../freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h#L1129)，
第 1129–1142 行）。官方 [Buzzer 文档](https://docs.m5stack.com/en/arduino/papermono/buzzer)
虽然通过 `M5Unified::Speaker_Class` 调用它，硬件仍被归类为 buzzer；不能据类名
承诺高保真扬声器效果。当前木鱼采用 S3 单引脚 I2S delta-sigma 输出，已经能播放
固定波形，并非只有 MIDI 或方波音调；但
[`Buzzer.h`](../../freeink-sdk/libs/hardware/Buzzer/include/Buzzer.h#L29) 第 29–33 行及
[`Buzzer.cpp`](../../freeink-sdk/libs/hardware/Buzzer/src/Buzzer.cpp#L74) 第 74–80、139 行
限定 **24 kHz、16-bit 单声道、最多 200 ms，阻塞调用，总截止时间 400 ms**。
它不能直接承接数秒语音，也没有 WAV/MP3 文件播放接口。

SDK 另有真正的 codec 输出层
[`AudioManager.h`](../../freeink-sdk/libs/hardware/AudioManager/include/AudioManager.h#L57)
第 57–66 行，支持回调式 WAV/PCM 流、16-bit、1–2 声道、8–48 kHz；这是另一条
设备能力路径，不能套用到 Mono。CrossMux 的
[`HalAudioOutput.cpp`](../../lib/hal/HalAudioOutput.cpp#L3) 第 3 行仍由
`CROSSPOINT_CAP_SOUND_FEEDBACK` 编译开关限制，其 PCM 接口在第 50–55 行，尚未
暴露给 Lua；该板型开关的现有用途见
[`platformio.ini`](../../platformio.ini#L769) 第 769–781 行。

三个聊天应用目前都没有语音附件模型：

| 层/传输 | 当前实现与限制 | 短语音需要补的部分 |
| --- | --- | --- |
| 共同消息和 Lua | [`ChatModel.h`](../../src/activities/apps/chat/ChatModel.h#L16) 第 16、26–33、50–56 行：正文最多 192 字节，只接受无控制字符的完整 UTF-8；[`CrossStoreRuntime.cpp`](../../lib/CrossStoreRuntime/CrossStoreRuntime.cpp#L233) 第 233–251 行也把聊天参数和回包限制为文本 | 独立 `Voice/Attachment` 消息种类，编码、时长、字节数、附件 ID、校验值和传输状态；Lua 只操作句柄与元数据，不能把二进制塞进文本 |
| wifi_chat / Tox | [`ToxBackend.cpp`](../../src/activities/apps/chat/ToxBackend.cpp#L146) 第 146–154、379–414 行只收发 `friend_message`；[`build_chat.py`](../../scripts/build_chat.py#L15) 第 15–19 行只编译 toxcore，不含 ToxAV | 优先采用录好后发送文件，接通标准 Tox file 收发/分块/取消回调；本地固定的 toxcore 0.2.23 已有这些 API（`.cache/chat-deps/c-toxcore-0.2.23/toxcore/tox.h` 第 1945、2024、2061、2098 行）。标准 WAV 可作为普通文件互通，其他客户端是否显示语音气泡仍需验证 |
| esp_chat | [`EspChatProtocol.h`](../../src/activities/apps/chat/EspChatProtocol.h#L9) 第 9–24、46–86 行：只有 Hello/Text/Ack/Bye，固定头 32 字节，完整帧最多 224 字节；[`EspNowBackend.cpp`](../../src/activities/apps/chat/EspNowBackend.cpp#L70) 第 70–84 行只保留一条待发文本 | 增加版本化二进制附件帧、分片编号、重组长度上限、校验、超时/重传、群聊接收回执与发送节奏；现有整条消息 ACK 不能替代附件分片重组 |
| rf_chat / LoRa | [`RadioChatProtocol.h`](../../src/activities/apps/chat/RadioChatProtocol.h#L8) 第 8–17、27–66 行：同样只有四类帧，头 28 字节，完整帧最多 220 字节；[`HalRadio.cpp`](../../lib/hal/HalRadio.cpp#L32) 第 32–44 行使用 BW125/SF9/CR4:5 的短消息配置 | 新增附件协议与低码率语音编码，严格限制时长和并发，量测空中占用时间；不能直接沿用文本发送循环连续广播 PCM |

记录文件目前也只有文本字段，见
[`ChatStore.cpp`](../../src/activities/apps/chat/ChatStore.cpp#L124) 第 124–131 行。
新增附件需同时版本化消息记录，保留旧文本读取，并明确残留 `.part`、失败重试、
存储限额及卸载时的处理。后台接收附件应延续“仅唤醒期间工作”，不能把后台接收
权限解释成后台录音或自动播放授权。

建议按以下阶段接入；这些是后续设计，不是已经实现的能力：

1. **先验证本机录放。** 增加共用 `HalMicrophone` 和板级 mic 供电钩子，经 HAL
   串行控制 IOE1、PDM/I2S 生命周期。录音启动失败、退出应用、撤回权限与休眠都要
   停止采样并关断供电。为 Mono 另做可取消的分块 PCM 输出，验证数秒人声是否可懂；
   现有 200 ms 接口不能在 Lua 中简单循环拼接成长录音。真实 codec 设备走
   AudioManager，蜂鸣器 PCM 与 codec 输出继续保留明确的能力区分。
2. **补齐权限和 Lua 服务。** 新增明确的 `microphone` 录音权限；声音播放可复用
   `sound`，避免重复设置。保留 `buzzer/audio` 兼容性，同时增加可查询的录音和
   连续 PCM 输出能力。聊天包把录音作为可选功能，按硬件与授权决定是否显示按钮；
   不应把麦克风或连续 PCM 输出加入整个聊天包的硬性 `requires`，否则缺少相应
   硬件的设备连文字聊天也无法安装。拒绝录音权限仍可文字聊天、接收语音附件；
   播放则单独检查播放能力与 `sound` 授权。旧版本授权不得自动补上新录音权限，
   必须由用户明确授予。本次不改 manifest，也不预授予任何尚未实现的权限。
   原生 C++ 服务提供开始、停止、取消、状态、播放与发送附件句柄，Lua
   只负责 UI。使用既有私有目录 `/.crosspoint/appdata/<id>/`，通过 Storage 写入
   有上限的临时文件；退出和失败清理未提交内容。
3. **先做 3–10 秒录好后发送。** 16 kHz/16-bit 单声道每秒约 32,000 字节，10 秒
   为 320,000 字节，超过 Lua 当前 128 KiB 上限
   （[`CrossStoreRuntime.h`](../../lib/CrossStoreRuntime/CrossStoreRuntime.h#L15)，
   第 15–16 行）。因此采用固定小块采集/编码/存卡，不能把整条 PCM 放进 Lua
   字符串或扩张现有消息数组。先接 wifi_chat 的标准文件传输，再接 ESP-NOW 的
   有界分片与重传；接收完成并校验后再允许播放。
4. **最后单独评估 LoRa。** 可比较 [Codec 2](https://github.com/drowe67/codec2)
   的低码率语音方案；其官方说明给出 700–3200 bit/s 范围。例如 1300 bit/s 的
   10 秒编码净数据约 1,625 字节，不含分片、校验和重传开销。此处只是大小计算，
   尚未移植 codec、测 CPU/RAM/Flash、音质或实际传输时延。先确认短语音可懂、
   空中占用与文本响应可接受，再考虑增加时长；不在这一阶段引入实时通话。
