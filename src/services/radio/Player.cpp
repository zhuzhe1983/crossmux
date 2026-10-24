#include "Player.h"

#include <algorithm>

#include "StreamFormat.h"

#if defined(CROSSMUX_RADIO) && CROSSMUX_RADIO
#include <Arduino.h>
#include <HalMemory.h>
#include <HalRadioAudio.h>
#include <Logging.h>
#include <Memory.h>
#include <MonoResampler.h>
#include <PcmQueue.h>
#include <RadioDecoder.h>
#include <SecureHttpClient.h>
#include <SpeakerLoudness.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#if CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM
#include <esp_heap_caps.h>
#include <freertos/idf_additions.h>
#endif

#include <atomic>
#include <cstring>
#include <type_traits>

#include "network/HttpDownloader.h"

namespace radio {
namespace {
constexpr uint32_t STACK_BYTES = 32768;
constexpr uint32_t OUTPUT_STACK_BYTES = 4096;
constexpr size_t FADE_SAMPLES = 120;  // 5 ms, amplitude only; never changes the source sample timeline.
constexpr uint32_t IO_TIMEOUT_MS = 3000, NO_AUDIO_TIMEOUT_MS = 10000;
bool redirect(int status) { return status == 301 || status == 302 || status == 303 || status == 307 || status == 308; }
void logMemory(const char* phase) {
  const auto internal = HalMemory::getInternalHeap(), psram = HalMemory::getPsramHeap();
  LOG_INF("RADIO", "%s internal=%zu largest=%zu min=%zu PSRAM=%zu largest=%zu", phase, internal.freeBytes,
          internal.largestBlockBytes, internal.minFreeBytes, psram.freeBytes, psram.largestBlockBytes);
}
BaseType_t createWorker(void (*entry)(void*), const char* name, uint32_t stack, void* context, unsigned priority,
                        TaskHandle_t* task) {
#if CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM
  return xTaskCreateWithCaps(entry, name, stack, context, priority, task, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  return xTaskCreate(entry, name, stack, context, priority, task);
#endif
}
void joinWorker(TaskHandle_t& task, const std::atomic<bool>& done) {
  if (!task) return;
  // No member access/destructors remain after done and suspension, even on SMP.
  while (!done.load() || eTaskGetState(task) != eSuspended) vTaskDelay(pdMS_TO_TICKS(1));
#if CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM
  vTaskDeleteWithCaps(task);
#else
  vTaskDelete(task);
#endif
  task = nullptr;
}
}  // namespace

class Player::Impl {
 public:
  // Keep the small queue/state controls in internal RAM. Default new may fall
  // back to PSRAM, where the IDF S32C1I workaround serializes atomic RMW through
  // a global portMUX. The normal C++ constructor/destructor still run; only
  // allocation is capability-constrained, with the matching free deleter.
  static void* operator new(size_t bytes, const std::nothrow_t&) noexcept {
    static_assert(alignof(Impl) <= alignof(std::max_align_t));
    return memory::makeInternalByteBufferNoThrow(bytes).release();
  }
  static void operator delete(void* pointer) noexcept { memory::FreeDeleter{}(static_cast<uint8_t*>(pointer)); }
  static void operator delete(void* pointer, const std::nothrow_t&) noexcept { operator delete(pointer); }
  struct Work {
    Mp3Decoder decoder;
    MonoResampler resampler;
    // At most 2 KiB of lookahead/output state, in the existing PSRAM workspace.
    // Reuse for the session; keep DSP off the 4 KiB real-time output stack.
    SpeakerLoudness loudness;
    Mp3Framer framer;
    IcyFilter icy;
    uint8_t input[Mp3Framer::BUFFER_BYTES];
    int16_t pcm[Mp3Decoder::MAX_PCM_SAMPLES];
    int16_t queued[PcmQueue::CAPACITY];
    int16_t output[MonoResampler::BLOCK_SAMPLES];
  };
  static_assert(std::is_trivially_destructible_v<Work>);
  TaskHandle_t task = nullptr, audioTask = nullptr;
  std::atomic<bool> cancel{false}, done{false}, producerDone{false}, audioDone{false};
  std::atomic<State> state{State::Connecting};
  std::atomic<Error> error{Error::None};
  std::atomic<uint32_t> bytes{0}, frames{0}, underruns{0};
  std::atomic<uint8_t> volume{DEFAULT_VOLUME};
  char url[MAX_URL_BYTES + 1]{};
  // One session per player, matching HttpDownloadSession's SecureNet boundary;
  // neither HttpDownloader's global response state nor UI callbacks are used.
  freeink::SecureHttpClient http;
  Work* work = nullptr;
  PcmQueue queue;
  uint32_t lastDecoded = 0;
  bool headersChecked = false, drainingTail = false;
  uint32_t lastWriteUs = 0, maxRefillGapUs = 0;  // Output-task-only timing diagnostics.
  bool wroteOutput = false;

  bool fail(Error reason) {
    Error expected = Error::None;
    error.compare_exchange_strong(expected, reason);
    return false;
  }
  static bool output(void* context, const int16_t* samples, size_t count) {
    auto& self = *static_cast<Impl*>(context);
    size_t sent = 0;
    while (sent < count) {
      if (self.cancel.load() || self.audioDone.load() || (!self.drainingTail && self.error.load() != Error::None))
        return false;
      const size_t accepted = self.queue.push(samples + sent, count - sent);
      sent += accepted;
      if (!accepted) vTaskDelay(pdMS_TO_TICKS(1));
    }
    return true;
  }
  static bool processOutput(void* context, const int16_t* samples, size_t count) {
    auto& self = *static_cast<Impl*>(context);
    return self.work->loudness.feed(samples, count);
  }
  bool writeOutput(size_t count) {
    if (cancel.load()) return false;
    if (wroteOutput) maxRefillGapUs = std::max(maxRefillGapUs, static_cast<uint32_t>(micros() - lastWriteUs));
    HalRadioAudio::setVolume(volume.load());
    if (!HalRadioAudio::write(work->output, count)) return fail(Error::Audio);
    lastWriteUs = micros();
    wroteOutput = true;
    return true;
  }
  void play() {
    bool started = false, buffering = true;
    size_t fadeIn = FADE_SAMPLES;
    int16_t last = 0;
    while (!cancel.load()) {
      // Acquire producerDone before inspecting the final queue: EOF/error must
      // drain even a short tail, without waiting for another prebuffer window.
      const bool finished = producerDone.load();
      const size_t available = queue.available();
      if (finished && !available) break;
      if (buffering) {
        if (available >= PcmQueue::PREBUFFER || (finished && available)) {
          if (!started) {
            if (!HalRadioAudio::begin(volume.load())) {
              fail(Error::Audio);
              break;
            }
            started = true;
          }
          buffering = false;
          fadeIn = FADE_SAMPLES;
        } else if (started) {
          // PCM zero is encoded by the HAL to the PDM midpoint. Leaving DMA
          // empty would auto-clear to GPIO-low/negative full scale instead.
          std::fill_n(work->output, MonoResampler::BLOCK_SAMPLES, 0);
          if (!writeOutput(MonoResampler::BLOCK_SAMPLES)) break;
          continue;
        } else {
          vTaskDelay(pdMS_TO_TICKS(1));
          continue;
        }
      }
      const size_t count = queue.pop(work->output, MonoResampler::BLOCK_SAMPLES);
      if (count) {
        // Only adjust the first 5 ms of amplitude after a buffered restart.
        // Every source sample is consumed exactly once, at the original rate.
        for (size_t i = 0; i < count && fadeIn; ++i, --fadeIn)
          work->output[i] =
              static_cast<int16_t>(int32_t{work->output[i]} * static_cast<int32_t>(FADE_SAMPLES - fadeIn) /
                                   static_cast<int32_t>(FADE_SAMPLES - 1));
        last = work->output[count - 1];
        if (!writeOutput(count)) break;
        state.store(State::Playing);
        continue;
      }
      if (producerDone.load()) break;
      buffering = true;
      state.store(State::Buffering);
      underruns.fetch_add(1);
      // Fade from the last actual output amplitude, then feed silence while
      // retaining every newly decoded sample until the restart watermark.
      for (size_t i = 0; i < MonoResampler::BLOCK_SAMPLES; ++i)
        work->output[i] = i < FADE_SAMPLES
                              ? static_cast<int16_t>(int32_t{last} * static_cast<int32_t>(FADE_SAMPLES - 1 - i) /
                                                     static_cast<int32_t>(FADE_SAMPLES - 1))
                              : 0;
      last = 0;
      if (!writeOutput(MonoResampler::BLOCK_SAMPLES)) break;
    }
    if (started) HalRadioAudio::end();
    LOG_INF("RADIO", "output stopped underruns=%lu max-refill-us=%lu stack-free=%u/%lu",
            static_cast<unsigned long>(underruns.load()), static_cast<unsigned long>(maxRefillGapUs),
            static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
            static_cast<unsigned long>(OUTPUT_STACK_BYTES));
  }
  static void audioEntry(void* context) {
    auto& self = *static_cast<Impl*>(context);
    self.play();
    self.audioDone.store(true);
    for (;;) vTaskSuspend(nullptr);
  }
  static bool frame(void* context, const uint8_t* data, size_t count) {
    auto& self = *static_cast<Impl*>(context);
    if (self.cancel.load() || self.error.load() != Error::None) return false;
    mp3dec_frame_info_t info{};
    const int samples = self.work->decoder.decode(data, count, self.work->pcm, info);
    if (info.layer != 3 || info.frame_bytes != static_cast<int>(count) || info.channels < 1 || info.channels > 2)
      return self.fail(Error::Unsupported);
    // A stream can join in the middle of a bit reservoir. Allow those initial
    // frames to prime the decoder, bounded by the no-audio timeout.
    if (!samples) return true;
    auto& resampler = self.work->resampler;
    if (resampler.inputRate() != static_cast<uint32_t>(info.hz)) {
      if (resampler.inputRate() && !resampler.finish()) return false;
      if (!resampler.configure(info.hz, processOutput, &self)) return self.fail(Error::Unsupported);
    }
    if (!resampler.feed(self.work->pcm, samples, info.channels)) return false;
    self.lastDecoded = millis();
    self.frames.fetch_add(1);
    return true;
  }
  static bool audioBytes(void* context, const uint8_t* data, size_t count) {
    auto& self = *static_cast<Impl*>(context);
    return self.work->framer.feed(data, count, frame, context);
  }
  bool body(const uint8_t* data, size_t count) {
    if (cancel.load() || error.load() != Error::None) return false;
    if (http.getStatus() != 200) return false;  // Stop/drain no redirect or error body.
    if (!headersChecked) {
      const auto* mime = http.getUniqueHeader("content-type");
      const auto* encoding = http.getUniqueHeader("content-encoding");
      const auto* interval = http.getUniqueHeader("icy-metaint");
      if (!mp3ContentType(mime ? mime->c_str() : nullptr) ||
          (encoding && !encoding->empty() && *encoding != "identity") ||
          !work->icy.configure(interval ? interval->c_str() : nullptr))
        return fail(Error::Unsupported);
      headersChecked = true;
      state.store(State::Buffering);
    }
    bytes.fetch_add(static_cast<uint32_t>(count));
    if (!work->icy.feed(data, count, audioBytes, this)) {
      if (!cancel.load() && error.load() == Error::None) fail(Error::Unsupported);
      return false;
    }
    return true;
  }
  bool abortRequest() {
    if (cancel.load() || error.load() != Error::None) return true;
    if (static_cast<uint32_t>(millis() - lastDecoded) >= NO_AUDIO_TIMEOUT_MS) {
      fail(Error::Timeout);
      return true;
    }
    return false;
  }
  void stream() {
    for (unsigned hop = 0; hop <= 3 && !cancel.load(); ++hop) {
      if (!std::strncmp(url, "https://", 8) && !HttpDownloader::hasMemoryForTls()) {
        fail(Error::NoMemory);
        return;
      }
      http.end();
      http.setTimeout(IO_TIMEOUT_MS);
      http.setFollowRedirects(0);  // Bound/validate every Location URL ourselves.
      http.setReuse(false);
      // Same current trust policy as HttpDownloadSession/HttpDownloader. Do not
      // add credentials or silently allow HTTPS -> HTTP redirect downgrades.
      http.setInsecure();
      http.setUserAgent("CrossMux-AirTune");
      if (!http.begin(url)) {
        fail(Error::InvalidUrl);
        return;
      }
      http.addHeader("Accept", "audio/mpeg,application/octet-stream");
      http.addHeader("Accept-Encoding", "identity");
      http.addHeader("Icy-MetaData", "0");
      headersChecked = false;
      const int status = http.GET([this](const uint8_t* data, size_t count) { return body(data, count); },
                                  [this] { return abortRequest(); });
      if (cancel.load() || error.load() != Error::None) return;
      if (redirect(status) && hop < 3) {
        const auto* location = http.getUniqueHeader("location");
        if (!location || location->size() > MAX_URL_BYTES) {
          fail(Error::InvalidUrl);
          return;
        }
        std::string next;
        next.reserve(MAX_URL_BYTES + 1);
        if (!freeink::SecureHttpClient::resolveUrl(url, *location, next) || !validUrl(next.c_str()) ||
            (!std::strncmp(url, "https://", 8) && next.compare(0, 8, "https://"))) {
          fail(Error::InvalidUrl);
          return;
        }
        std::memcpy(url, next.c_str(), next.size() + 1);
        continue;
      }
      if (status != 200 || !http.responseComplete()) {
        fail(Error::Network);
        return;
      }
      if (!frames.load() || !work->framer.complete()) {
        fail(Error::Unsupported);
        return;
      }
      if (!work->resampler.finish() && !cancel.load() && error.load() == Error::None) fail(Error::Audio);
      return;
    }
  }
  void run() {
    logMemory("worker start");
    // Decoder/filter state, compressed input, the 64 KiB PCM queue and a 1 KiB
    // consumer block exceed task stacks. One bounded PSRAM allocation is reused
    // until both tasks stop; it never falls back to scarce internal memory.
    auto storage = HalMemory::allocatePsram(sizeof(Work));
    if (!storage) {
      LOG_ERR("RADIO", "Cannot allocate PSRAM workspace (%zu bytes)", sizeof(Work));
      fail(Error::NoMemory);
    } else {
      work = std::construct_at(reinterpret_cast<Work*>(storage.get()));
      work->decoder.reset();
      work->framer.reset(work->input);
      queue.reset(work->queued);
      lastDecoded = millis();
      // HalRadioAudio currently supports only PaperMono's single-pin buzzer.
      // Keep the prior Standard profile available for reproducible comparisons;
      // a future speaker/codec backend must select its own conditioning here.
      work->loudness.configure(output, this, SpeakerLoudness::Profile::Buzzer);
      LOG_INF("RADIO", "buzzer profile: 350-4500 Hz, bounded 6x compression, 24 kHz output");
      if (work->loudness.failed()) {
        fail(Error::Audio);
      } else if (createWorker(audioEntry, "AirTunePCM", OUTPUT_STACK_BYTES, this, 2, &audioTask) != pdPASS) {
        LOG_ERR("RADIO", "Cannot allocate output stack (%lu bytes)", static_cast<unsigned long>(OUTPUT_STACK_BYTES));
        fail(Error::NoMemory);
      } else {
        stream();
        // Include the converter's last partial block in a finite network-error
        // tail. Hardware failure/cancel must never enqueue or wait for more PCM.
        if (!cancel.load() && (error.load() == Error::Network || error.load() == Error::Timeout) &&
            work->resampler.inputRate()) {
          drainingTail = true;
          work->resampler.finish();
          drainingTail = false;
        }
        // The lookahead delays delivery, never the source timeline: emit every
        // held sample exactly once at EOF or during the finite network tail.
        // Do not flush on cancel or a failed audio sink.
        if (!cancel.load() &&
            (error.load() == Error::None || error.load() == Error::Network || error.load() == Error::Timeout)) {
          drainingTail = true;
          if (!work->loudness.finish() && !cancel.load() && error.load() == Error::None) fail(Error::Audio);
          drainingTail = false;
        }
      }
      // EOF and network errors drain already decoded PCM (at most 1.365 s).
      // Cancellation skips the tail; no task can touch Work after this join.
      producerDone.store(true);
      http.end();
      joinWorker(audioTask, audioDone);
      LOG_INF("RADIO", "loudness input=%llu output=%llu guarded=%llu limited=%llu peak-in=%u peak-out=%u",
              static_cast<unsigned long long>(work->loudness.inputSamples()),
              static_cast<unsigned long long>(work->loudness.outputSamples()),
              static_cast<unsigned long long>(work->loudness.peakGuardSamples()),
              static_cast<unsigned long long>(work->loudness.limitedSamples()),
              static_cast<unsigned>(work->loudness.maxInputPeak()),
              static_cast<unsigned>(work->loudness.maxOutputPeak()));
      queue.reset(nullptr);
      work = nullptr;
    }
    storage.reset();
    state.store(cancel.load() || error.load() == Error::None ? State::Idle : State::Error);
    LOG_INF("RADIO", "stopped error=%u bytes=%lu frames=%lu underruns=%lu stack-free=%u/%lu work=%zu",
            static_cast<unsigned>(error.load()), static_cast<unsigned long>(bytes.load()),
            static_cast<unsigned long>(frames.load()), static_cast<unsigned long>(underruns.load()),
            static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)), static_cast<unsigned long>(STACK_BYTES),
            sizeof(Work));
    logMemory("worker stopped");
  }
  static void entry(void* context) {
    auto& self = *static_cast<Impl*>(context);
    self.run();
    self.done.store(true);
    // Owner waits for eSuspended before deleting a caps-allocated stack. No
    // member access/destructor remains after publishing done, even on SMP.
    for (;;) vTaskSuspend(nullptr);
  }
};

Player::Player() = default;
Player::~Player() { stopAndJoin(); }
bool Player::start(const char* url) {
  stopAndJoin();
  if (!validUrl(url)) {
    failure_ = Error::InvalidUrl;
    return false;
  }
  if (!HalRadioAudio::supports()) {
    failure_ = Error::Unsupported;
    return false;
  }
  const auto internal = HalMemory::getInternalHeap(), psram = HalMemory::getPsramHeap();
#if CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM
  const size_t internalReserve = 24 * 1024, largestInternal = 8 * 1024;
  const size_t externalBytes = STACK_BYTES + OUTPUT_STACK_BYTES + sizeof(Impl::Work);
#else
  const size_t internalReserve = STACK_BYTES + OUTPUT_STACK_BYTES + 24 * 1024, largestInternal = STACK_BYTES;
  const size_t externalBytes = sizeof(Impl::Work);
#endif
  if (internal.freeBytes < internalReserve || internal.largestBlockBytes < largestInternal ||
      psram.freeBytes < externalBytes + 256 * 1024 ||
      psram.largestBlockBytes < std::max(size_t{STACK_BYTES}, sizeof(Impl::Work))) {
    failure_ = Error::NoMemory;
    logMemory("start preflight rejected");
    return false;
  }
  // Bounded session/atomic controls/URL (~1 KiB) span Activity callbacks. Large
  // decoder/queue buffers and supported 32+4 KiB task stacks are in PSRAM.
  impl_ = makeUniqueNoThrow<Impl>();
  if (!impl_) {
    LOG_ERR("RADIO", "Cannot allocate player control (%zu bytes)", sizeof(Impl));
    failure_ = Error::NoMemory;
    return false;
  }
  std::memcpy(impl_->url, url, std::strlen(url) + 1);
  impl_->volume.store(volume_);
  const auto result = createWorker(Impl::entry, "AirTune", STACK_BYTES, impl_.get(), 1, &impl_->task);
  if (result == pdPASS) return true;
  LOG_ERR("RADIO", "Cannot allocate worker stack (%lu bytes)", static_cast<unsigned long>(STACK_BYTES));
  impl_.reset();
  failure_ = Error::NoMemory;
  return false;
}
void Player::stopAndJoin() {
  if (impl_ && impl_->task) {
    impl_->cancel.store(true);
    // SecureNet checks cancel between socket operations; DNS itself inherits
    // the Arduino/lwIP retry bound. Never force-delete live TLS/socket owners.
    joinWorker(impl_->task, impl_->done);
  }
  impl_.reset();
  failure_ = Error::None;
}
Snapshot Player::snapshot() const {
  if (!impl_) return {failure_ == Error::None ? State::Idle : State::Error, failure_, 0, 0};
  return {impl_->state.load(),
          impl_->error.load(),
          impl_->bytes.load(),
          impl_->frames.load(),
          static_cast<uint32_t>(impl_->queue.available() * 1000 / MonoResampler::OUTPUT_RATE),
          impl_->underruns.load()};
}
void Player::setVolume(uint8_t value) {
  volume_ = std::min<uint8_t>(value, 100);
  if (impl_) impl_->volume.store(volume_);
}
}  // namespace radio
#else
namespace radio {
class Player::Impl {};
Player::Player() = default;
Player::~Player() = default;
bool Player::start(const char*) {
  failure_ = Error::Unsupported;
  return false;
}
void Player::stopAndJoin() { failure_ = Error::None; }
Snapshot Player::snapshot() const { return {failure_ == Error::None ? State::Idle : State::Error, failure_, 0, 0}; }
void Player::setVolume(uint8_t value) { volume_ = std::min<uint8_t>(value, 100); }
}  // namespace radio
#endif
