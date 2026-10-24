#pragma once
#include <cstddef>
#include <cstdint>

namespace radio {
class MonoResampler {
 public:
  using Sink = bool (*)(void*, const int16_t*, size_t);
  static constexpr uint32_t OUTPUT_RATE = 24000;
  static constexpr size_t BLOCK_SAMPLES = 512;
  bool configure(uint32_t inputRate, Sink sink, void* context);
  bool feed(const int16_t* interleaved, size_t frames, unsigned channels);
  bool finish();
  uint32_t inputRate() const { return rate_; }

 private:
  // Rational phase carries across MP3 frames; playback duration/pitch stay at
  // the source rate. A low-pass biquad precedes downsampling, then linear
  // interpolation reconstructs the positions on the 24 kHz timeline.
  uint32_t rate_ = 0, phase_ = 0;
  Sink sink_ = nullptr;
  void* context_ = nullptr;
  bool havePrevious_ = false;
  float previous_ = 0, z1_ = 0, z2_ = 0;
  float b0_ = 1, b1_ = 0, b2_ = 0, a1_ = 0, a2_ = 0;
  int16_t output_[BLOCK_SAMPLES]{};
  size_t used_ = 0;
  bool emit(float value);
  bool sample(float value, bool endpoint = true);
};
}  // namespace radio
