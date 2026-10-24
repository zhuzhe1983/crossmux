#include "HalAudioOutput.h"

#if CROSSPOINT_CAP_SOUND_FEEDBACK

#include <AudioManager.h>
#include <BoardConfig.h>
#include <Logging.h>

#include <atomic>

#include "HalRadioAudio.h"

#if FREEINK_DEVICE_WAVESHARE_EPAPER_397
#include "Waveshare397Power.h"
#endif

namespace {

#if FREEINK_DEVICE_WAVESHARE_EPAPER_397
constexpr unsigned long AUDIO_RAIL_SETTLE_MS = 10;
#endif

AudioManager audio;
enum class PowerState : uint8_t { Off, Powered, Ready };
PowerState powerState = PowerState::Off;
enum class Owner : uint8_t { Idle, Control, Stream, Fault };
std::atomic<Owner> owner{Owner::Idle};
#if FREEINK_DEVICE_WAVESHARE_EPAPER_397
std::atomic<uint8_t> streamVolume{0};
using BusLock = Waveshare397Power::ScopedBusLock;
#else
struct BusLock {
  BusLock() {}
  ~BusLock() {}
};
#endif

// Foreground cues fail immediately while a radio worker owns the output.
// The stream's DMA writes never take the I2C bus lock; control operations do.
class ControlClaim {
 public:
  explicit ControlClaim(Owner expected = Owner::Idle)
      : held_(owner.compare_exchange_strong(expected, Owner::Control, std::memory_order_acq_rel)) {}
  ControlClaim(const ControlClaim&) = delete;
  ControlClaim& operator=(const ControlClaim&) = delete;
  ~ControlClaim() {
    if (held_) owner.store(Owner::Idle, std::memory_order_release);
  }
  bool held() const { return held_; }
  void transfer(const Owner next) {
    owner.store(next, std::memory_order_release);
    held_ = false;
  }

 private:
  bool held_;
};

bool setBoardPower(const bool enabled) {
#if FREEINK_DEVICE_WAVESHARE_EPAPER_397
  return Waveshare397Power::setAudioPower(enabled);
#else
  (void)enabled;
  return true;
#endif
}

bool beginOwned() {
  if (powerState == PowerState::Ready) return true;
  // A failed I2C acknowledgement does not prove the rail stayed off. Retain
  // the cleanup obligation even when enabling or codec initialization fails.
  powerState = PowerState::Powered;
  if (!setBoardPower(true)) {
    LOG_ERR("SND", "Failed to enable board audio power");
    return false;
  }
#if FREEINK_DEVICE_WAVESHARE_EPAPER_397
  delay(AUDIO_RAIL_SETTLE_MS);
#endif
  if (!audio.begin()) {
    LOG_ERR("SND", "Failed to initialize audio output");
    return false;
  }
  powerState = PowerState::Ready;
  return true;
}

bool shutdownOwned() {
  switch (powerState) {
    case PowerState::Off:
      return true;
    case PowerState::Ready:
      audio.powerDown();
      // A callback that failed to stop must keep its channel and powered rails.
      // Refuse a new stream until a later explicit shutdown can safely recover it.
      if (audio.isPlaying()) {
        LOG_ERR("SND", "Audio task did not stop; retaining output ownership");
        return false;
      }
      powerState = PowerState::Powered;
      break;
    case PowerState::Powered:
      break;
  }
  if (!setBoardPower(false)) {
    LOG_ERR("SND", "Failed to disable board audio power");
    return false;
  }
  powerState = PowerState::Off;
  return true;
}

}  // namespace

namespace HalAudioOutput {

bool playPcm(const ReadCallback read, const SeekCallback seek, const uint32_t sampleRate, const uint8_t channels,
             const uint8_t volume) {
  if (!read) return false;
  ControlClaim claim;
  if (!claim.held()) return false;
  const BusLock lock;
  if (!beginOwned()) {
    if (!shutdownOwned()) claim.transfer(Owner::Fault);
    return false;
  }
  audio.setVolume(volume);
  AudioManager::PcmSource source = {read, seek};
  if (audio.playPcm(source, sampleRate, channels)) return true;
  LOG_ERR("SND", "Failed to play feedback PCM");
  if (!shutdownOwned()) claim.transfer(Owner::Fault);
  return false;
}

bool restart(const uint8_t volume) {
  ControlClaim claim;
  if (!claim.held()) return false;
  const BusLock lock;
  if (powerState != PowerState::Ready) return false;
  audio.setVolume(volume);
  return audio.requestRestart();
}

void setVolume(const uint8_t volume) {
  ControlClaim claim;
  if (!claim.held()) return;
  const BusLock lock;
  if (powerState == PowerState::Ready) audio.setVolume(volume);
}

void shutdown() {
  const Owner current = owner.load(std::memory_order_acquire);
  if (current != Owner::Idle && current != Owner::Fault) return;
  ControlClaim claim(current);
  if (!claim.held()) return;
  const BusLock lock;
  if (!shutdownOwned()) claim.transfer(Owner::Fault);
}

}  // namespace HalAudioOutput

#if FREEINK_DEVICE_WAVESHARE_EPAPER_397
namespace HalRadioAudio {

OutputKind outputKind() { return supports() ? OutputKind::Speaker : OutputKind::None; }

bool supports() { return BoardConfig::hasAudio(); }

bool begin(const uint8_t volume) {
  if (!supports()) return false;
  ControlClaim claim;
  if (!claim.held()) return false;
  const BusLock lock;
  // A short cue may still be draining. Stop it on the owning control path
  // before changing the codec clock from its 16 kHz mode to 24 kHz streaming.
  if (!shutdownOwned()) {
    claim.transfer(Owner::Fault);
    return false;
  }
  if (!beginOwned()) {
    if (!shutdownOwned()) claim.transfer(Owner::Fault);
    return false;
  }
  if (!audio.beginPcmStream(24000)) {
    LOG_ERR("AUDIO", "Speaker PCM stream initialization failed");
    if (!shutdownOwned()) claim.transfer(Owner::Fault);
    return false;
  }
  setVolume(volume);
  claim.transfer(Owner::Stream);
  return true;
}

bool write(const int16_t* samples, const size_t count) {
  if (owner.load(std::memory_order_acquire) != Owner::Stream) return false;
  const bool ok = audio.writePcmStream(samples, count, streamVolume.load(std::memory_order_relaxed));
  if (!ok) LOG_ERR("AUDIO", "Speaker PCM stream write failed");
  // Even a failed stream retains ownership until its producer calls end().
  return ok;
}

void setVolume(const uint8_t volume) { streamVolume.store(volume > 100 ? 100 : volume, std::memory_order_relaxed); }

void end() {
  ControlClaim claim(Owner::Stream);
  if (!claim.held()) return;
  const BusLock lock;
  audio.endPcmStream();
  if (!shutdownOwned()) claim.transfer(Owner::Fault);
}

}  // namespace HalRadioAudio
#endif

#endif
