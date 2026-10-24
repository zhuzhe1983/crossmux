#include <BoardConfig.h>
#include <Buzzer.h>
#include <BuzzerPcm.h>
#include <HalBuzzer.h>
#include <HalRadioAudio.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <thread>
#include <vector>

#if FREEINK_CAP_BUZZER && defined(CONFIG_IDF_TARGET_ESP32S3)
#include <driver/gpio.h>
#include <driver/i2s_std.h>

namespace {
constexpr size_t RAMP = 96;

double recovered(size_t first, size_t count) {
  uint32_t ones = 0;
  for (size_t i = first * 2; i < (first + count) * 2; ++i) ones += __builtin_popcount(stream[i]);
  return ones * 65536.0 / (count * 32) - 32768;
}

void assertReleased() { assert(!allocated && !channel.running && gpioLevel == 0 && !ledcAttached); }

void checkStream() {
  Buzzer buzzer;
  int16_t samples[1024];
  for (size_t i = 0; i < 1024; ++i) samples[i] = static_cast<int16_t>((i * 7919) % 65536 - 32768);
  assert(!buzzer.writeStream(samples, 1));
  stream.clear();
  assert(buzzer.beginStream());
  const auto allocation = allocations;
  assert(!buzzer.beginStream() && !buzzer.begin() && !buzzer.playPcm(samples, 10));
  assert(!buzzer.writeStream(nullptr, 1) && !buzzer.writeStream(samples, 0));
  assert(!buzzer.writeStream(samples, 1025));
  assert(buzzer.writeStream(samples, 1024));
  const std::vector<uint16_t> combined = stream;
  assert(buzzer.endStream());
  assertReleased();
  // A producer may change chunk boundaries without resetting the sigma-delta
  // error. Compare real output bytes, not a reimplementation of the encoder.
  stream.clear();
  assert(buzzer.beginStream());
  for (size_t i = 0; i < 1024; i += 17) assert(buzzer.writeStream(samples + i, std::min<size_t>(17, 1024 - i)));
  assert(stream == combined && allocations == allocation + 1);
  const auto activeAllocation = allocations;
  for (unsigned i = 0; i < 12; ++i) assert(buzzer.writeStream(samples, 1024));
  assert(channel.running && allocations == activeAllocation);  // >0.5 s, one DMA allocation
  assert(buzzer.endStream());
  assertReleased();
  for (size_t i = stream.size() - 256 * 2; i < stream.size(); ++i) assert(stream[i] == 0);

  for (unsigned volume : {0u, 25u, 100u, 255u}) {
    stream.clear();
    std::fill(std::begin(samples), std::end(samples), 16000);
    assert(buzzer.beginStream() && buzzer.writeStream(samples, 1024, volume));
    const double expected = 16000.0 * std::min(volume, 100u) / 100;
    assert(std::abs(recovered(RAMP, 1024) - expected) <= 2);
    assert(buzzer.endStream());
  }
  fakeMillis = 0xfffffffa;
  assert(buzzer.beginStream() && buzzer.writeStream(samples, 1024) && buzzer.endStream());
  for (bool* failure : {&failNew, &failInit, &failEnable, &failWrite, &shortWrite}) {
    *failure = true;
    assert(!buzzer.beginStream());
    assertReleased();
    *failure = false;
    assert(buzzer.beginStream() && buzzer.endStream());
  }
  for (bool* failure : {&failWrite, &shortWrite}) {
    assert(buzzer.beginStream());
    *failure = true;
    assert(!buzzer.writeStream(samples, 1024));
    assertReleased();
    *failure = false;
    assert(buzzer.endStream());
  }
  assert(buzzer.beginStream());
  writeDelay = 20;
  auto started = fakeMillis;
  assert(!buzzer.writeStream(samples, 1024) && fakeMillis - started <= 100);
  assertReleased();
  writeDelay = 3;
  assert(buzzer.beginStream());
  writeDelay = 20;
  started = fakeMillis;
  assert(!buzzer.endStream() && fakeMillis - started <= 40);
  assertReleased();
  writeDelay = 3;
  for (bool* failure : {&failDisable, &failDelete}) {
    assert(buzzer.beginStream());
    *failure = true;
    assert(!buzzer.endStream() && allocated && gpioLevel == 0);
    assert(!buzzer.beginStream());
    *failure = false;
    buzzer.end();
    assertReleased();
  }
  // Failed initialization plus failed cleanup must never reuse a channel
  // whose pin routing was removed. Retry cleanup before a fresh allocation.
  failInit = failDelete = true;
  assert(!buzzer.beginStream() && allocated && gpioLevel == 0);
  failInit = false;
  assert(!buzzer.beginStream() && !channel.running);
  failDelete = false;
  assert(buzzer.beginStream() && buzzer.endStream());
  assertReleased();
}

void checkHal() {
  int16_t samples[1024];
  std::fill(std::begin(samples), std::end(samples), 16000);
#if FREEINK_DEVICE_PAPERMONO && !CROSSPOINT_EMULATED
  assert(HalRadioAudio::supports());
  assert(!HalRadioAudio::write(samples, 1));
  assert(HalRadioAudio::begin(25));
  assert(!HalRadioAudio::begin(25));
  assert(!HalBuzzer::play("woodfish") && !HalBuzzer::play("click"));
  HalBuzzer::shutdown();
  assert(channel.running && allocated);
  // Competing foreground actions run from inside the I/O stub while the
  // actual HAL producer owns the channel, including a cross-thread volume set.
  writeHook = [] {
    assert(!HalBuzzer::play("click"));
    HalBuzzer::shutdown();
    assert(channel.running);
  };
  stream.clear();
  assert(HalRadioAudio::write(samples, 1024));
  assert(std::abs(recovered(0, 1024) - 4000) <= 2);  // UI 25 -> 25% source amplitude
  writeHook = nullptr;
  std::thread foreground([] { HalRadioAudio::setVolume(100); });
  foreground.join();
  stream.clear();
  assert(HalRadioAudio::write(samples, 1024));
  assert(std::abs(recovered(0, 1024) - 16000) <= 2);  // UI 100 -> full source amplitude
  HalRadioAudio::setVolume(0);
  stream.clear();
  assert(HalRadioAudio::write(samples, 1024));
  assert(std::abs(recovered(0, 1024)) <= 2);
  HalRadioAudio::end();
  HalRadioAudio::end();
  assertReleased();
  toneHook = [] { assert(!HalRadioAudio::begin(25)); };
  fakeMillis += 100;
  assert(HalBuzzer::play("click"));
  toneHook = nullptr;
  HalBuzzer::shutdown();
  assert(HalRadioAudio::begin(25));
  failWrite = true;
  assert(!HalRadioAudio::write(samples, 1));
  failWrite = false;
  assertReleased();
  assert(!HalBuzzer::play("click"));  // Failed producer retains ownership until end.
  HalRadioAudio::end();
  for (bool* failure : {&failDisable, &failDelete}) {
    assert(HalRadioAudio::begin(25));
    *failure = true;
    HalRadioAudio::end();
    assert(allocated && gpioLevel == 0);
    assert(!HalRadioAudio::begin(25) && !HalBuzzer::play("click"));
    *failure = false;
    HalBuzzer::shutdown();  // Fault quarantine allows explicit cleanup retry.
    assertReleased();
  }
  failInit = failDelete = true;
  assert(!HalRadioAudio::begin(25) && allocated && gpioLevel == 0);
  failInit = false;
  assert(!HalRadioAudio::begin(25) && !HalBuzzer::play("click"));
  failDelete = false;
  HalBuzzer::shutdown();
  assertReleased();
  assert(!HalBuzzer::play(nullptr));
  assert(HalRadioAudio::begin(25));
  HalRadioAudio::end();
#else
  assert(!HalRadioAudio::supports() && !HalRadioAudio::begin(25));
  assert(!HalRadioAudio::write(samples, 1024));
  HalRadioAudio::setVolume(25);
  HalRadioAudio::end();
#endif
}
}  // namespace
#endif

int main() {
  assert(freeink::BuzzerPcm::scale(-32768, 100) == -32768);
  assert(freeink::BuzzerPcm::scale(32767, 255) == 32767);
  assert(freeink::BuzzerPcm::scale(32767, 0) == 0);
  assert(freeink::BuzzerPcm::scale(-16000, 25, 25) == -1000);
#if FREEINK_CAP_BUZZER && defined(CONFIG_IDF_TARGET_ESP32S3)
  checkStream();
  checkHal();
#else
  Buzzer buzzer;
  int16_t sample = 0;
  assert(!buzzer.beginStream() && !buzzer.writeStream(&sample, 1) && !buzzer.endStream());
  assert(!HalRadioAudio::supports() && !HalRadioAudio::begin(25) && !HalRadioAudio::write(&sample, 1));
  HalRadioAudio::setVolume(25);
  HalRadioAudio::end();
  HalBuzzer::shutdown();
#endif
}
