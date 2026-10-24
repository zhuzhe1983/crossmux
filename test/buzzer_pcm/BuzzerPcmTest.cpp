#include <BoardConfig.h>
#include <Buzzer.h>
#include <BuzzerPcm.h>

#include <cassert>
#include <cmath>
#include <initializer_list>
#if FREEINK_CAP_BUZZER && defined(CONFIG_IDF_TARGET_ESP32S3)
#include <driver/i2s_std.h>
#endif

int main() {
  for (int level : {-32768, -21000, -1, 0, 1024, 21000, 32767}) {
    freeink::BuzzerPcm encoder;
    uint32_t ones = 0;
    for (unsigned i = 0; i < 1024; ++i) {
      uint16_t slots[2];
      encoder.encode(level, slots);
      ones += __builtin_popcount(slots[0]) + __builtin_popcount(slots[1]);
    }
    const double decoded = ones * 65536.0 / (1024 * 32) - 32768;
    assert(std::abs(decoded - level) <= 2.0);
  }
  Buzzer buzzer;
  int16_t clip[3360]{};
#if FREEINK_CAP_BUZZER && defined(CONFIG_IDF_TARGET_ESP32S3)
  assert(Buzzer::supportsPcm());
  assert(!buzzer.playPcm(nullptr, 4) && !buzzer.playPcm(clip, 4801) && !buzzer.playPcm(clip, 0));
  assert(allocations == 0);
  clip[0] = 12000;
  clip[3359] = -12000;
  assert(buzzer.begin() && ledcAttached == 1);
  assert(buzzer.playPcm(clip, 3360) && ledcAttached == 0 && allocated == 1);
  assert(!channel.running && allocations == 1);
  // Recover the waveform's per-sample duty from the emitted 16-bit slots.
  for (unsigned i = 0; i < 3360; ++i) {
    const unsigned slot = (96 + i) * 2;
    int value = (__builtin_popcount(stream[slot]) + __builtin_popcount(stream[slot + 1])) * 2048 - 32768;
    assert(std::abs(value - clip[i]) <= 2048);
  }
  assert(stream[0] == 0 && stream[1] == 0);
  for (size_t i = (96 + 3360 + 96) * 2; i < stream.size(); ++i) assert(stream[i] == 0);
  stream.clear();
  fakeMillis = 0xfffffffa;  // rollover must not abort a valid clip
  assert(buzzer.playPcm(clip, 3360) && allocations == 1);
  assert(buzzer.begin() && allocated == 0 && ledcAttached == 1);
  buzzer.end();
  assert(ledcAttached == 0);
  for (bool* failure : {&failNew, &failInit, &failEnable, &failWrite, &shortWrite}) {
    *failure = true;
    assert(!buzzer.playPcm(clip, 3360) && !allocated && !channel.running);
    *failure = false;
    assert(buzzer.playPcm(clip, 3360));
    buzzer.end();
  }
  writeDelay = 40;
  const auto start = fakeMillis;
  assert(!buzzer.playPcm(clip, 3360) && !allocated && fakeMillis - start <= 400);
  BoardConfig::ACTIVE.audio.buzzer = -1;
  assert(!Buzzer::supportsPcm() && !buzzer.begin() && !buzzer.playPcm(clip, 3360));
#else
  assert(!Buzzer::supportsPcm() && !buzzer.playPcm(clip, 3360));
#if FREEINK_CAP_BUZZER
  assert(buzzer.begin());
  buzzer.tone(2000, 10);
  assert(fakeMillis == 10);
#else
  assert(!buzzer.begin());
#endif
  buzzer.end();
  assert(ledcAttached == 0);
#endif
}
