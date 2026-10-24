#include <AudioManager.h>
#include <BoardConfig.h>
#include <HalAudioOutput.h>
#include <HalRadioAudio.h>
#include <Waveshare397Power.h>
#include <Wire.h>

#include <algorithm>
#include <barrier>
#include <cstdio>

// Only hardware-facing SDK methods are faked. The production header defines
// their signatures/state; both complete firmware HAL translation units link in.
namespace freeink {
bool AudioManager::begin() {
  fixture::control("codec_begin");
  assert((Wire.registers[0x90] & 0x03) == 0x03);
  begun_ = !fixture::failBegin;
  return begun_;
}
void AudioManager::setVolume(uint8_t) { fixture::control("cue_volume"); }
bool AudioManager::playPcm(const PcmSource& source, uint32_t rate, uint8_t channels) {
  fixture::control("cue_play");
  assert(begun_ && source.read && rate == 16000 && channels == 1);
  if (fixture::failCue) return false;
  playbackState_.store(PlaybackState::Playing);
  return true;
}
bool AudioManager::requestRestart() {
  fixture::control("cue_restart");
  return playbackState_.load() == PlaybackState::Playing;
}
void AudioManager::powerDown() {
  fixture::control("codec_down");
  if (fixture::stuckAsync && isPlaying()) return;
  playbackState_.store(PlaybackState::Idle);
  begun_ = false;
}
bool AudioManager::beginPcmStream(uint32_t rate) {
  fixture::control("stream_begin");
  assert(begun_ && !isPlaying() && rate == 24000);
  if (fixture::failStreamBegin) return false;
  playbackState_.store(PlaybackState::PcmStream);
  return true;
}
bool AudioManager::writePcmStream(const int16_t* samples, size_t count, uint8_t volume) {
  assert(fixture::busDepth == 0);  // DMA calls never pin the shared control bus.
  if (playbackState_.load() != PlaybackState::PcmStream) return false;
  if (!samples || !count || count > 1024 || fixture::failWrite) {
    playbackState_.store(PlaybackState::PcmStreamFailed);
    return false;
  }
  assert(volume <= 100);
  if (fixture::holdWrite.load()) {
    fixture::writeEntered.store(true);
    fixture::waitFor([] { return fixture::releaseWrite.load(); });
  }
  fixture::pcm.insert(fixture::pcm.end(), samples, samples + count);
  fixture::volumes.push_back(volume);
  return true;
}
void AudioManager::endPcmStream() {
  fixture::control("stream_end");
  assert(playbackState_.load() == PlaybackState::PcmStream || playbackState_.load() == PlaybackState::PcmStreamFailed);
  playbackState_.store(PlaybackState::Idle);
  begun_ = false;
}
}  // namespace freeink

namespace {
int cueRead(uint8_t*, size_t) { return 0; }
bool cueSeek(size_t) { return true; }
bool cue() { return HalAudioOutput::playPcm(cueRead, cueSeek, 16000, 1, 50); }
const std::array<int16_t, 7> PCM{-32768, -12000, -1, 0, 1, 12000, 32767};

void railsOff() { assert((Wire.registers[0x90] & 0x07) == 0x04); }
void noControlChange(unsigned control, unsigned wire) {
  assert(fixture::controlCalls.load() == control && fixture::wireCalls.load() == wire);
}
void assertCueBlocked() {
  const auto control = fixture::controlCalls.load(), wire = fixture::wireCalls.load();
  assert(!cue() && !HalAudioOutput::restart(99));
  HalAudioOutput::setVolume(1);
  HalAudioOutput::shutdown();
  assert(!HalRadioAudio::begin(50));
  noControlChange(control, wire);
}
void recoverCue() {
  assert(cue());
  assert(HalAudioOutput::restart(75));
  HalAudioOutput::shutdown();
  railsOff();
}

void powerLockTests() {
  Wire.registers[0x03] = 0x4A;
  // Exercise the actual nested setAudioPower -> begin initialization path.
  assert(Waveshare397Power::setAudioPower(true));
  assert(fixture::mutexCreates.load() == 1 && fixture::busDepth == 0);
  assert((Wire.registers[0x90] & 0x07) == 0x03);
  assert(Waveshare397Power::setAudioPower(false));

  // Suspend one real RMW after its read. The display writer must block until
  // audio's write completes, preserving both masks rather than losing one.
  fixture::pauseLdoRead.store(true);
  const auto previousContentions = fixture::busContentions.load();
  std::thread audio([] { assert(Waveshare397Power::setAudioPower(true)); });
  fixture::waitFor([] { return fixture::ldoReadPaused.load(); });
  std::atomic<bool> displayFinished{false};
  std::thread display([&] {
    assert(Waveshare397Power::setDisplayPower(true));
    displayFinished.store(true);
  });
  fixture::waitFor([&] { return fixture::busContentions.load() > previousContentions; });
  assert(!displayFinished.load());
  fixture::releaseLdoRead.store(true);
  audio.join();
  display.join();
  assert((Wire.registers[0x90] & 0x07) == 0x07);

  // Simultaneous bit clearing is just as important as simultaneous enabling.
  // Each phase rendezvous starts from a known state and checks the real bank.
  std::barrier phases(2);
  std::thread toggler([&] {
    for (unsigned i = 0; i < 200; ++i) {
      phases.arrive_and_wait();
      assert(Waveshare397Power::setAudioPower(i % 2 == 0));
      phases.arrive_and_wait();
    }
  });
  for (unsigned i = 0; i < 200; ++i) {
    phases.arrive_and_wait();
    assert(Waveshare397Power::setDisplayPower(i % 2 == 0));
    phases.arrive_and_wait();
    assert((Wire.registers[0x90] & 0x07) == (i % 2 == 0 ? 0x07 : 0));
  }
  toggler.join();
  assert(Waveshare397Power::setDisplayPower(true));
  {
    const Waveshare397Power::ScopedBusLock outer;
    assert(fixture::busDepth == 1);
    assert(Waveshare397Power::setAudioPower(false));
    assert(fixture::busDepth == 1);
  }
  assert(fixture::busDepth == 0 && fixture::mutexCreates.load() == 1);
  railsOff();
}

void streamTests() {
  using HalRadioAudio::OutputKind;
  assert(HalRadioAudio::supports() && HalRadioAudio::outputKind() == OutputKind::Speaker);
  fixture::audioPresent = false;
  const auto control = fixture::controlCalls.load(), wire = fixture::wireCalls.load();
  assert(!HalRadioAudio::supports() && HalRadioAudio::outputKind() == OutputKind::None);
  assert(!HalRadioAudio::begin(50) && !HalRadioAudio::write(PCM.data(), PCM.size()));
  HalRadioAudio::end();
  noControlChange(control, wire);
  fixture::audioPresent = true;

  assert(cue());
  fixture::events.clear();
  assert(HalRadioAudio::begin(50));
  const std::vector<std::string> transition{"codec_down", "rail_off", "rail_on", "codec_begin", "stream_begin"};
  assert(fixture::events == transition);  // No 16/24 kHz clock overlap with an async cue.
  assertCueBlocked();
  const auto streamControl = fixture::controlCalls.load(), streamWire = fixture::wireCalls.load();
  fixture::pcm.clear();
  fixture::volumes.clear();
  for (const uint8_t volume : {0, 50, 100, 255}) {
    HalRadioAudio::setVolume(volume);
    assert(HalRadioAudio::write(PCM.data(), PCM.size()));
    assert(fixture::volumes.back() == std::min<uint8_t>(volume, 100));
    assert(std::equal(PCM.begin(), PCM.end(), fixture::pcm.end() - PCM.size()));
  }
  noControlChange(streamControl, streamWire);
  HalRadioAudio::end();
  railsOff();
  const auto endControl = fixture::controlCalls.load(), endWire = fixture::wireCalls.load();
  HalRadioAudio::end();
  assert(!HalRadioAudio::write(PCM.data(), PCM.size()));
  noControlChange(endControl, endWire);
  recoverCue();

  for (const uint8_t volume : {0, 50, 100, 255}) {
    assert(HalRadioAudio::begin(volume));
    const auto beginControl = fixture::controlCalls.load(), beginWire = fixture::wireCalls.load();
    assert(HalRadioAudio::write(PCM.data(), PCM.size()));
    assert(fixture::volumes.back() == std::min<uint8_t>(volume, 100));
    noControlChange(beginControl, beginWire);
    HalRadioAudio::end();
    railsOff();
  }

  // A real foreground thread competes while its worker is in a blocking DMA
  // call. Stream ownership prevents all cue/control mutations; only the atomic
  // volume command is accepted and reaches the next PCM block.
  fixture::holdWrite.store(true);
  std::thread worker([] {
    assert(HalRadioAudio::begin(50));
    assert(HalRadioAudio::write(PCM.data(), PCM.size()));
    assert(HalRadioAudio::write(PCM.data(), PCM.size()));
    HalRadioAudio::end();
  });
  fixture::waitFor([] { return fixture::writeEntered.load(); });
  assertCueBlocked();
  HalRadioAudio::setVolume(37);
  // Display/RTC-like bus users are free to run during DMA; preserving the rail
  // here also checks the integrated power implementation, not a fake power API.
  assert(Waveshare397Power::setDisplayPower(true));
  fixture::releaseWrite.store(true);
  worker.join();
  fixture::holdWrite.store(false);
  assert(fixture::volumes[fixture::volumes.size() - 2] == 50 && fixture::volumes.back() == 37);
  railsOff();
  recoverCue();
}

void failureTests() {
  // Failed begin never hands a stream to Player; cleanup must finish immediately
  // or enter Fault. Player is not required to call end after begin returns false.
  for (bool* flag : {&fixture::failBegin, &fixture::failStreamBegin, &fixture::failRailOn}) {
    *flag = true;
    assert(!HalRadioAudio::begin(50));
    *flag = false;
    railsOff();
    recoverCue();
  }
  fixture::failCue = true;
  assert(!cue());
  fixture::failCue = false;
  railsOff();
  recoverCue();

  assert(HalRadioAudio::begin(50));
  fixture::failWrite = true;
  assert(!HalRadioAudio::write(PCM.data(), PCM.size()));
  fixture::failWrite = false;
  assertCueBlocked();  // A failed producer retains ownership until its end().
  assert(!HalRadioAudio::write(PCM.data(), PCM.size()));
  HalRadioAudio::end();
  railsOff();
  recoverCue();

  for (unsigned invalid = 0; invalid < 3; ++invalid) {
    assert(HalRadioAudio::begin(50));
    assert(!HalRadioAudio::write(invalid == 0 ? nullptr : PCM.data(), invalid == 1 ? 0 : invalid == 2 ? 1025 : 1));
    assertCueBlocked();
    HalRadioAudio::end();
    railsOff();
  }

  assert(cue());
  fixture::stuckAsync = true;
  const auto firstEvent = fixture::events.size();
  assert(!HalRadioAudio::begin(50));
  assert((Wire.registers[0x90] & 0x03) == 0x03);
  assert(std::count(fixture::events.begin() + firstEvent, fixture::events.end(), "rail_off") == 0);
  // Fault rejects new producers. Explicit shutdown retries the stuck owner,
  // but cannot remove power until the SDK confirms the async task has stopped.
  assert(!cue() && !HalRadioAudio::begin(50));
  HalAudioOutput::shutdown();
  assert((Wire.registers[0x90] & 0x03) == 0x03);
  fixture::stuckAsync = false;
  HalAudioOutput::shutdown();
  railsOff();
  recoverCue();

  // A rail-off failure must retain the obligation across a later shutdown.
  // Cover normal stream end and both codec/rail initialization failure paths.
  for (unsigned failure = 0; failure < 4; ++failure) {
    if (failure == 0) assert(HalRadioAudio::begin(50));
    fixture::failRailOff = true;
    fixture::failBegin = failure == 1;
    fixture::failRailOn = failure == 2;
    fixture::failStreamBegin = failure == 3;
    if (failure == 0)
      HalRadioAudio::end();
    else
      assert(!HalRadioAudio::begin(50));
    fixture::failBegin = fixture::failRailOn = fixture::failStreamBegin = false;
    assert((Wire.registers[0x90] & 0x03) == 0x03);
    assert(!cue() && !HalRadioAudio::begin(50));
    HalAudioOutput::shutdown();  // Still fails: Fault must remain recoverable.
    assert((Wire.registers[0x90] & 0x03) == 0x03);
    fixture::failRailOff = false;
    HalAudioOutput::shutdown();
    railsOff();
    recoverCue();
  }
}
}  // namespace

int main() {
  powerLockTests();
  streamTests();
  failureTests();
  assert(fixture::busDepth == 0 && fixture::mutexCreates.load() == 1);
  std::puts("Waveshare audio HAL PASS: real cue/stream ownership, volume, failure recovery and concurrent PMIC RMW");
}
