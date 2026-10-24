# RadioDecoder

On-demand, MP3-only playback for the foreground AirTune service. `CROSSMUX_RADIO=0`
or an absent macro compiles no decoder/resampler implementation. The stock
minimp3 source and CC0 license are retained under `vendor`; the exact upstream
revision and source URL are in `vendor/UPSTREAM.md`.

`Mp3Decoder` uses 6,668 bytes of persistent state and a 4,608-byte interleaved
PCM buffer. The upstream decoder's frame scratch stays on the worker stack:
the macOS non-optimized compiler reports a 16,432-byte frame for
`mp3dec_decode_frame` (not an ESP32 stack measurement). The service initially
uses a 32 KiB decoder/network stack and a 4 KiB output stack, and logs both
device high-water marks on stop. On the current Mono configuration,
`CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM=1` allows
`xTaskCreateWithCaps(..., MALLOC_CAP_SPIRAM)`; its owner waits for suspension and
uses `vTaskDeleteWithCaps`, retaining only the task control block internally.
Other configurations need sufficient internal stack capacity or return NoMemory.

Decoder state, the 8 KiB input buffer, PCM, a 64 KiB PCM ring and separate
512-sample converter/consumer blocks reside in one bounded, fallible PSRAM
workspace. The ring holds 32,768 mono samples, or 1.365 seconds at 24 kHz.
Compared with the synchronous player, buffering adds 64 KiB for the ring,
1 KiB for the consumer block and 4 KiB for its task stack in PSRAM, plus one
internal task control block and small atomic queue/diagnostic fields. There is
no per-frame allocation, overwrite of unread samples or internal-heap fallback.
A 256 KiB PSRAM reserve and internal free/largest-block floors are checked before
start; TLS keeps the existing
`HttpDownloader::hasMemoryForTls` gate. These are preflight checks, not memory
reservations. Physical Wi-Fi/TLS/audio coexistence and the stack margin still
need device acceptance.

The mono converter tracks rational phase across frame boundaries. All nine MP3
sample rates (8–48 kHz) produce 24 kHz output with the original duration and
pitch; this is format conversion, not sample-rate relabeling or speed adjustment.
A 10.8 kHz, second-order Butterworth low-pass precedes downsampling from
32/44.1/48 kHz. Stereo channels are averaged, and interpolation/clamping use a
fixed 512-sample output block. The last partial input interval is held on clean
EOF, at most one output sample beyond the exact fractional duration.

Before the PCM queue, `SpeakerLoudness` conditions the 24 kHz mono signal.
The Player selects its experimental **Buzzer** profile because the current
`HalRadioAudio` supports only PaperMono's single-pin buzzer. A future speaker or
codec backend must select appropriate conditioning instead of inheriting this
choice. No Lua API or AirTune package update is required.

The Buzzer profile applies second-order Butterworth high-pass at 350 Hz and
low-pass at 4500 Hz before a bounded compressor. Its absolute-amplitude detector
uses 5 ms attack and 120 ms release. Below a 0.08 full-scale detector threshold,
makeup is 6x; above it, the gain is `6*sqrt(0.08/envelope)`, clamped to 1–6x
(2:1 steady-state amplitude compression). It does not use a hard noise gate or
instantaneous clipping. Quiet background noise can also be amplified, but
silence cannot cause unlimited gain growth. These frequencies are an experimental
speech-oriented tradeoff, **not** a measured inverse response of this buzzer;
bass and voice warmth are reduced.

A 120-sample (5 ms) lookahead limits peaks through a smoothed gain envelope
(0.5 ms attack, 60 ms release), targeting 0.86 full scale for Buzzer, with a
separately counted 0.94 full-scale emergency guard. This raises the digital
peak target only about 1.54 dB versus the prior profile; any additional speech
clarity comes from spectral/dynamic processing, not higher voltage or a new
amplifier. The prior **Standard** profile remains available to tests and offline
comparisons: 120 Hz one-pole high-pass, fixed 2.75x makeup, 0.72 peak target and
0.80 emergency guard. It is not a second selectable AirTune setting.

Both profiles preserve the sample rate and output count. There is no time
stretch, pitch shift, note extraction or conversion to MIDI. `finish()` emits
every pending conditioned sample exactly once, including streams shorter than
the lookahead; it does not append padding or a filter tail. A failed sink is
latched and never retried because it may already have accepted a prefix.

The conditioner is a trivially destructible member of the existing PSRAM
workspace, statically limited to 2 KiB (1640 bytes in the host build, 32 bytes
more than the prior single-profile processor). Its
480-byte delay and 1024-byte output buffers are reused for the whole session.
It adds no allocation or task and does not run on the 4 KiB output-task stack.
The preflight memory calculation includes it through `sizeof(Work)`.

The default volume is 50. On Mono, HAL volume 0–100 scales the conditioned PCM
from mute to unity; the earlier additional 25% gain cap is removed. This final
stage only attenuates, independently of the conditioner. Applications can
restore saved volume, including zero. The existing single-pin PDM encoder and
hardware clock configuration are unchanged. These numbers describe digital
amplitude, not a calibrated acoustic volume percentage or maximum sound pressure.

The service accepts direct HTTP(S) MP3 streams. It bounds initial/redirect URLs,
follows at most three redirects, rejects HTTPS downgrades, requests no ICY
metadata and strips bounded metadata blocks when servers send it anyway.
Chunked HTTP is handled by the existing SecureNet transport. MP3 frame/ID3
boundaries survive arbitrary network chunks. HTML, M3U/HLS, AAC, unknown MIME,
compressed HTTP bodies, invalid framing and prolonged absence of decoded audio
fail closed. A direct stream is required; playlist resolution, AAC and remote
catalog search are outside this service. Legacy `ICY 200 OK` status lines are
not supported by the current SecureHttpClient parser (HTTP status lines are).

The foreground owns the Player. It must join on exit/sleep before audio/Wi-Fi
shutdown. Network callbacks stay on the worker and never enter Lua. Network
reads and TLS handshake use a three-second socket timeout plus cancellation;
Arduino's blocking DNS lookup retains lwIP's own retry bound and cannot be
interrupted mid-lookup by this API. No live TLS/socket owner is force-deleted.
Audio writes use the HAL's bounded write contract. Network/decoder callbacks
only enqueue PCM; a separate priority-2 output task feeds the HAL while the
priority-1 network task reads or decodes. A full ring applies cancel-aware
backpressure. Output waits for 12,288 samples (512 ms) before claiming the HAL,
then runs at the fixed 24 kHz rate. On queue underflow it fades the last output
amplitude to PCM zero over 5 ms, continuously feeds correctly encoded silence,
and waits for that same watermark before fading in the next source samples.
This inserts a buffering pause without dropping, repeating or speeding up the
source audio. Simply leaving DMA empty would make the current single-pin
driver auto-clear to GPIO-low, not the 50% duty cycle of PCM silence.

Normal EOF drains even a tail shorter than the watermark. Network/timeout
failure also drains the already decoded finite tail: at most 1.365 s in the ring,
21.3 ms in the converter and the HAL's bounded 40 ms shutdown. No subsequent
network data is awaited. Cancellation skips the queued tail; hardware failure
stops output immediately. After the network producer finishes, it joins and
deletes the suspended output task before releasing their shared workspace.
The network worker then suspends until its foreground owner joins/deletes it.
No live socket, task or shared PCM buffer is freed under another task.

`Snapshot.bufferedMs` reports approximate queued PCM duration and `underruns`
counts empty-queue transitions into rebuffering. State is Buffering during
startup/rebuffering, Playing while sending source PCM, and Idle/Error after
cleanup. Stop logs include queue underruns, both stack high-water marks and
`max-refill-us`, the longest gap between a successful HAL write returning and
the next write starting. These diagnostics distinguish network underflow from
output scheduling stalls; they are not hardware DMA-underflow counters. With
the current 8 ms DMA tail, refill gaps approaching 8,000 us require device
investigation. The HAL/DMA size and sample rate are unchanged by buffering.

Run `python3 lib/RadioDecoder/test/test_radio.py` from the repository root. It
uses ASan/UBSan for the real decoder, parser and converter. With ffmpeg installed
it generates 997 Hz MP3 fixtures at 8/44.1/48 kHz, verifies byte-split invariance,
duration/pitch, and tests the production Player with substituted HAL/network/task
boundaries for both task-creation/allocation failures, redirects, timeout,
repeated cancellation and join. The production SPSC queue is tested concurrently
for bounded backpressure, exact ordering and wraparound. A paced fake HAL models
24 kHz consumption and an 8 ms DMA tail; a 300 ms network pause must preserve all
samples with no rebuffering, and a 1.8 s pause must produce a fade/silence/restart
without losing any source samples. Tests also cover EOF below the startup
watermark, network/timeout finite-tail draining and cancellation during startup,
full-ring backpressure, rebuffering and error draining. Host sleep coalescing is
excluded from the fake driver's DMA budget; these tests check handoff gaps, not
hard real-time scheduling. No test plays audio on a device. These tests do not
certify physical sound quality, real network cancellation latency or ESP32 stack
depth.

Device checkpoint (2026-10-24): the PaperMono test unit ran the real
Player, decoder, resampler and I2S output against a local HTTP fixture.
This checkpoint predates the removal of the 25% gain cap; it does not validate
listening quality at the new gain. Each
scenario received 32,256 MP3 bytes and decoded 168 frames; the source was a
4-second 997 Hz tone at 48 kHz (4.032 seconds including MP3 frame padding).

| HTTP delivery | Rebuffer events | Maximum refill gap | Output stack remaining |
| --- | ---: | ---: | ---: |
| Smooth | 0 | 1,113 us | 2,420 / 4,096 bytes |
| One 300 ms pause | 0 | 174 us | 2,452 / 4,096 bytes |
| One 1.8 s pause | 1 | 402 us | 2,452 / 4,096 bytes |

All three scenarios reached Idle without an audio error. The network worker
retained 10,284 / 32,768 stack bytes, and session PSRAM returned to its baseline
between scenarios. The temporary diagnostic image was replaced with the normal
firmware, independently verified and observed booting to Home. These measurements
cover transport recovery and task headroom for this fixture; they do not establish
acoustic quality, behavior during concurrent UI rendering, or every live station.
Listening acceptance remains pending. Local evidence is retained under
`build/crossstore-validation/papermono-20261024-audio-buffer/manifest.json`.

Subsequent listening and output checks (same device, 2026-10-24) used the
unity-gain firmware with AirTune 0.1.1. The user reported clean but quiet live
radio at volume 50, and still quiet with audible noise at 100. Loudness and
high-volume sound-quality acceptance therefore remain **unresolved**.

Three temporary, MAC-restricted diagnostics bypassed Wi-Fi, MP3 decoding and
the Player. They used fixed 24 kHz PCM, bounded writes, a reused 1 KiB static
producer block, silent gaps and one sequence per boot; no microphone was used.

| Comparison | Runtime evidence | User listening feedback |
| --- | --- | --- |
| 1/3 kHz sine, volume 50/100, 2 seconds each | Four complete 48,000-sample scenes; refill gaps at most 64 us | Only the two higher tones were clearly noticed, and remained quiet |
| LEDC square versus PCM sine at 1/3 kHz, 1 second each | Four scenes without error; PCM refill gaps at most 57 us | LEDC was clearly louder; the 1 kHz square attracted attention, but was not considered proof of maximum volume |
| 3 kHz LEDC square, I2S square, existing PDM sine, candidate clustered PWM sine | All scenes completed; three PCM scenes of 24,000 samples, refill gaps at most 72 us | The two square waves sounded similar; the two sine encoders showed little difference |

These are subjective comparisons, not sound-pressure or acoustic-distortion
measurements. The equal square-wave comparison does not show a gross I2S
output loss. A square wave has different amplitude and harmonic content from
the sine, so its greater loudness does not by itself identify a PCM driver bug,
the transducer's frequency response, or maximum achievable volume.

The clustered PWM candidate preserved each frame's high-bit count and the
original sample rate, but provided no material listening improvement and added
harmonics in the digital waveform model. It was **not adopted**. Temporary
startup, HAL and encoder changes were restored byte-for-byte; every diagnostic
image was replaced with the normal firmware and independently verified. The
normal PlatformIO build also reproduces the restored image SHA256
`c679e01daa3b81d86ecf79e9472a64bc414f5ba6143a521b9a98244be0d48134`.
Home was observed after restoration. Local logs, exact test sources and result
records are retained under `.cache/mono-volume-20261024/pcm-diagnostic/`,
`output-comparison/` and `pwm-comparison/`.

The [official PaperMono factory example](https://github.com/m5stack/M5PaperMono-UserDemo/blob/e55877dc04b89e13a8519cee5435dc326499e1ca/main/hal/hal_buzzer.cpp)
uses GPIO42 LEDC square waves. The
[official specification](https://docs.m5stack.com/en/core/PaperMono) lists a
built-in buzzer; that designation alone does not establish its sound-pressure
limit or rule out useful PCM playback. Do not infer a maximum-volume claim from
these tests or blindly increase digital gain beyond full scale.

The subsequent speaker-conditioning build adds an independent ASan/UBSan check
(`python3 lib/RadioDecoder/test/test_loudness.py`, also called by `test_radio.py`).
It covers 0/1/119/120/121-sample tails, every chunk size 1–513, failure after a
partially consumed sink batch, reuse, DC/silence, full-scale/random peaks, and
997/3000 Hz duration/frequency preservation. The real Player's existing
transport, underflow, short/error-tail, allocation and joined-exit regressions
also pass with conditioning before the queue.

A bounded public Beijing News stream capture was decoded with the production
minimp3/resampler and processed with the actual conditioner on the host. For the
same complete 15-second, 360,000-sample observation, RMS changed from -13.015 to
-11.448 dBFS (+1.57 dB), peak from 28,580 to 23,593, with zero emergency-guard or
full-scale samples. A fixed 300 Hz–4 kHz reference filter measured +3.19 dB;
this filter is **not a measured PaperMono response or acoustic loudness model**.
The 38 quietest original 100 ms windows had a median paired gain of +4.32 dB.
All input samples were retained, and zero-input checks stayed silent. Raw
broadcast audio was deleted after analysis; scripts/statistics are retained in
`.cache/mono-loudness-20261024/`.

Firmware artifact and device acceptance records are in
`build/crossstore-validation/papermono-20261024-loudness/manifest.json`.
Electrical PCM statistics and successful target compilation do not by
themselves establish perceived loudness or eliminate hardware distortion;
real AirTune listening remains a separate acceptance step.

### Experimental buzzer profile (2026-10-24)

After trying the official UserDemo, the user requested one buzzer-specific
AirTune candidate. The official demo has no volume slider; its audible cues do
not establish a maximum sound pressure for broadcast playback. The new profile
keeps the previously checked 768 kHz single-pin PDM output, hardware clocks,
volume control and queue ownership unchanged. No supply or pin configuration
was changed. The previous Standard profile remains an offline reference.

`test_buzzer_profile.py` (also run by `test_radio.py`) compiles the real DSP with
ASan/UBSan. It checks arbitrary chunk boundaries, exact short/long output counts,
latched partial-sink failures, reset across both profiles, DC/silence, extreme
inputs, bounded noise gain after long silence, pass/stop-band behavior and
997/3000 Hz frequency preservation. The production Player's transport/lifecycle
suite also passed with Buzzer selected. Host object size is 1640 bytes; this
adds 32 bytes to the existing PSRAM workspace and no task or allocation.

A new bounded Beijing News capture compared both production profiles on the
same 360,000 samples (15 seconds, 24 kHz). Standard/Buzzer output peaks were
23,593 / 28,180 and neither used its emergency guard. Full-band RMS increased
1.21 dB; a fixed 300 Hz–4 kHz reference filter measured +2.89 dB. This reference
filter is not a measured buzzer response, and these are numerical signal
measurements, **not acoustic loudness or listening acceptance**. Fresh silence
and settled post-signal silence remained zero. Temporary broadcast/PCM files
were deleted after analysis; only the scripts, hashes and numerical results
remain under `.cache/mono-buzzer-radio-20261024/`.

`papermono_store` compiled successfully. The candidate and deployment receipt
are in `build/crossstore-validation/papermono-20261024-buzzer-radio/`. Restoring
from the official demo uses the verified pre-demo full Flash backup with only
app0 replaced; NVS, other partitions and TF application data are preserved.
Physical playback still needs listening at the same station/volume, checking
speech clarity, noise/distortion, controls, stopping and returning to reading.
The full repository CI remains blocked at existing theme/main formatting
failures; targeted audio checks and compilation do not imply full CI success.

Listening result: the user reported this candidate was still too quiet and
requested leaving it installed without further Mono audio tuning. Playback
loudness therefore **did not pass acceptance**; do not describe the audio issue
as fixed. Further AirTune hardware evaluation is deferred until the user's
Ink4 Plus arrives, subject to its actual audio backend support.
