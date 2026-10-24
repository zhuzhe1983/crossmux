#if defined(CROSSMUX_RADIO) && CROSSMUX_RADIO
#include "MonoResampler.h"

#include <algorithm>

namespace radio {
bool MonoResampler::configure(uint32_t inputRate, Sink sink, void* context) {
  if (!sink) return false;
  b0_ = 1;
  b1_ = b2_ = a1_ = a2_ = 0;
  // 10.8 kHz Butterworth low-pass, Q=1/sqrt(2), normalized a0=1.
  // Fixed MP3 sample rates avoid libm/trigonometry and allocation at runtime.
  switch (inputRate) {
    case 8000:
    case 11025:
    case 12000:
    case 16000:
    case 22050:
    case 24000:
      break;
    case 32000:
      b0_ = b2_ = 0.47491774f;
      b1_ = 0.94983549f;
      a1_ = 0.65193845f;
      a2_ = 0.24773253f;
      break;
    case 44100:
      b0_ = b2_ = 0.28356588f;
      b1_ = 0.56713175f;
      a1_ = -0.03755875f;
      a2_ = 0.17182226f;
      break;
    case 48000:
      b0_ = b2_ = 0.24834108f;
      b1_ = 0.49668216f;
      a1_ = -0.1842138f;
      a2_ = 0.17757812f;
      break;
    default:
      return false;
  }
  rate_ = inputRate;
  phase_ = 0;
  sink_ = sink;
  context_ = context;
  havePrevious_ = false;
  previous_ = z1_ = z2_ = 0;
  used_ = 0;
  return true;
}

bool MonoResampler::emit(float value) {
  value = std::clamp(value, -32768.0f, 32767.0f);
  output_[used_++] = static_cast<int16_t>(value);
  if (used_ != BLOCK_SAMPLES) return true;
  if (!sink_(context_, output_, used_)) return false;
  used_ = 0;
  return true;
}

bool MonoResampler::sample(float value, bool endpoint) {
  if (!havePrevious_) {
    havePrevious_ = true;
    previous_ = value;
    phase_ = rate_;
    return emit(value);
  }
  while (phase_ < OUTPUT_RATE || (endpoint && phase_ == OUTPUT_RATE)) {
    if (!emit(previous_ + (value - previous_) * (static_cast<float>(phase_) / OUTPUT_RATE))) return false;
    phase_ += rate_;
  }
  phase_ -= OUTPUT_RATE;
  previous_ = value;
  return true;
}

bool MonoResampler::feed(const int16_t* interleaved, size_t frames, unsigned channels) {
  if (!rate_ || !interleaved || (channels != 1 && channels != 2)) return false;
  for (size_t i = 0; i < frames; ++i) {
    const int32_t mono = channels == 1 ? interleaved[i] : (int32_t{interleaved[2 * i]} + interleaved[2 * i + 1]) / 2;
    const float value = b0_ * mono + z1_;
    z1_ = b1_ * mono - a1_ * value + z2_;
    z2_ = b2_ * mono - a2_ * value;
    if (!sample(value)) return false;
  }
  return true;
}

bool MonoResampler::finish() {
  if (!rate_) return false;
  if (havePrevious_ && !sample(previous_, false)) return false;
  if (used_ && !sink_(context_, output_, used_)) return false;
  used_ = 0;
  havePrevious_ = false;
  return true;
}
}  // namespace radio
#endif
