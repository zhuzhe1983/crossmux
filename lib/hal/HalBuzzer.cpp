#include "HalBuzzer.h"

#include <BoardConfig.h>

#include <atomic>
#include <cstring>

#include "HalRadioAudio.h"
#if FREEINK_CAP_BUZZER && !CROSSPOINT_EMULATED
#include <Buzzer.h>
#include <Logging.h>
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#include "WoodfishAudio.generated.h"
#endif

namespace {
// SDK output is initialized lazily and released on exit/sleep. PCM samples
// stay in Flash; the small DMA channel is reused, without a playback task.
Buzzer buzzer;
uint32_t lastCue = 0;
bool played = false;
// One instance owns both short cues and continuous PCM. CAS rejects competing
// foreground calls without a wait or semaphore allocation. Only volume may
// change from another task while the decoder worker owns a stream.
enum class Owner : uint8_t { Idle, Cue, Stream, Shutdown, Fault };
std::atomic<Owner> owner{Owner::Idle};
std::atomic<uint8_t> streamVolume{0};
// The UI controls the source PCM amplitude directly: 0 is muted and 100
// preserves full scale. No additional attenuation or above-unity boost.
constexpr uint8_t MAX_STREAM_GAIN_PERCENT = 100;

bool claim(Owner next) {
  Owner expected = Owner::Idle;
  return owner.compare_exchange_strong(expected, next, std::memory_order_acq_rel);
}

void releaseAfterCleanup() { owner.store(buzzer.present() ? Owner::Fault : Owner::Idle, std::memory_order_release); }
}  // namespace
#endif
namespace HalBuzzer {
bool present() {
#if FREEINK_CAP_BUZZER && !CROSSPOINT_EMULATED
  return BoardConfig::hasBuzzer();
#else
  return false;
#endif
}
bool supportsPcm() {
#if FREEINK_CAP_BUZZER && !CROSSPOINT_EMULATED
  return Buzzer::supportsPcm();
#else
  return false;
#endif
}
bool play(const char* cue) {
#if FREEINK_CAP_BUZZER && !CROSSPOINT_EMULATED
  if (!cue) return false;
  const bool woodfish = !std::strcmp(cue, "woodfish");
  if (!woodfish && std::strcmp(cue, "click")) return false;
  if (!claim(Owner::Cue)) return false;
  if (played && millis() - lastCue < 80) {
    owner.store(Owner::Idle, std::memory_order_release);
    return false;
  }
  lastCue = millis();
  played = true;
#if defined(CONFIG_IDF_TARGET_ESP32S3)
  if (woodfish && supportsPcm()) {
    const bool ok = buzzer.playPcm(woodfishAudio::SAMPLES, sizeof(woodfishAudio::SAMPLES) / sizeof(int16_t));
    if (!ok) {
      LOG_ERR("AUDIO", "Woodfish PCM playback failed");
      buzzer.end();
      releaseAfterCleanup();
    } else {
      owner.store(Owner::Idle, std::memory_order_release);
    }
    return ok;
  }
#endif
  if (!buzzer.begin()) {
    buzzer.end();
    releaseAfterCleanup();
    return false;
  }
  if (woodfish) {
    buzzer.tone(1200, 12);
    buzzer.tone(800, 20);
  } else
    buzzer.tone(1800, 12);
  owner.store(Owner::Idle, std::memory_order_release);
  return true;
#else
  (void)cue;
  return false;
#endif
}
void shutdown() {
#if FREEINK_CAP_BUZZER && !CROSSPOINT_EMULATED
  if (!claim(Owner::Shutdown)) {
    Owner expected = Owner::Fault;
    if (!owner.compare_exchange_strong(expected, Owner::Shutdown, std::memory_order_acq_rel)) return;
  }
  buzzer.end();
  played = false;
  releaseAfterCleanup();
#endif
}
}  // namespace HalBuzzer

#if !(FREEINK_DEVICE_WAVESHARE_EPAPER_397 && CROSSPOINT_CAP_SOUND_FEEDBACK)
namespace HalRadioAudio {
OutputKind outputKind() { return supports() ? OutputKind::Buzzer : OutputKind::None; }

bool supports() {
#if FREEINK_CAP_BUZZER && !CROSSPOINT_EMULATED && FREEINK_DEVICE_PAPERMONO && defined(CONFIG_IDF_TARGET_ESP32S3)
  return Buzzer::supportsPcm();
#else
  return false;
#endif
}

bool begin(uint8_t volume) {
#if FREEINK_CAP_BUZZER && !CROSSPOINT_EMULATED
  if (!supports() || !claim(Owner::Stream)) return false;
  setVolume(volume);
  if (buzzer.beginStream()) return true;
  LOG_ERR("AUDIO", "Radio PCM output initialization failed");
  buzzer.end();
  releaseAfterCleanup();
  return false;
#else
  (void)volume;
  return false;
#endif
}

bool write(const int16_t* samples, size_t count) {
#if FREEINK_CAP_BUZZER && !CROSSPOINT_EMULATED
  if (owner.load(std::memory_order_acquire) != Owner::Stream) return false;
  const bool ok =
      buzzer.writeStream(samples, count, streamVolume.load(std::memory_order_relaxed), MAX_STREAM_GAIN_PERCENT);
  if (!ok) LOG_ERR("AUDIO", "Radio PCM output write failed");
  // Retain ownership even on failure until this producer calls end().
  return ok;
#else
  (void)samples;
  (void)count;
  return false;
#endif
}

void setVolume(uint8_t volume) {
#if FREEINK_CAP_BUZZER && !CROSSPOINT_EMULATED
  streamVolume.store(volume > 100 ? 100 : volume, std::memory_order_relaxed);
#else
  (void)volume;
#endif
}

void end() {
#if FREEINK_CAP_BUZZER && !CROSSPOINT_EMULATED
  if (owner.load(std::memory_order_acquire) != Owner::Stream) return;
  if (!buzzer.endStream()) LOG_ERR("AUDIO", "Radio PCM output shutdown failed");
  releaseAfterCleanup();
#endif
}
}  // namespace HalRadioAudio
#endif
