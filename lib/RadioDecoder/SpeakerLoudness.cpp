#if defined(CROSSMUX_RADIO) && CROSSMUX_RADIO
#include "SpeakerLoudness.h"

#include <algorithm>
#include <cmath>

namespace radio {
namespace {
// One-pole 120 Hz DC/low-frequency rejection: alpha=1-exp(-2*pi*120/24000).
// Reduces low-frequency excursion before makeup; not a speaker-response model.
constexpr float DC_ALPHA = 0.0309275737f;
// 0.5 ms attack smoothing and 60 ms multiplicative gain release. Gain never
// exceeds unity; each profile separately bounds its makeup gain.
constexpr float ATTACK_DECAY = 0.9200444146f;    // exp(-1/(24000*0.0005))
constexpr float RELEASE_FACTOR = 1.0006946856f;  // exp(1/(24000*0.060))
// Future release over the 120-sample delay is already allowed for when a peak
// enters. At departure, envelopeGain <= targetPeak_/abs(that incoming sample).
constexpr float LOOKAHEAD_RELEASE_COMPENSATION = 0.9200444146f;  // exp(-120/(24000*0.060))
constexpr float PCM_SCALE = 32768.0f;
// Experimental speech-focused band, not a measured inverse response of the
// PaperMono buzzer: second-order Butterworth HP 350 Hz then LP 4500 Hz, 24 kHz.
// Transposed direct form II. Coefficients are computed once offline; no trig or
// allocation on the worker. The compressor varies gain; it never clips samples.
struct Biquad {
  float b0, b1, b2, a1, a2;
};
constexpr Biquad HIGH_PASS{0.9372603903f, -1.8745207805f, 0.9372603903f, -1.8705806407f, 0.8784609203f};
constexpr Biquad LOW_PASS{0.1866943331f, 0.3733886662f, 0.1866943331f, -0.4629380253f, 0.2097153578f};
constexpr float COMPRESSOR_THRESHOLD = 0.08f;
constexpr float COMPRESSOR_ATTACK = 0.0082987074f;   // 1-exp(-1/(24000*0.005))
constexpr float COMPRESSOR_RELEASE = 0.0003471619f;  // 1-exp(-1/(24000*0.120))

float filter(float input, const Biquad& c, float& z1, float& z2) {
  const float output = c.b0 * input + z1;
  z1 = c.b1 * input - c.a1 * output + z2;
  z2 = c.b2 * input - c.a2 * output;
  // Finite-precision residue after silence must not keep denormal arithmetic
  // alive. This is far below a single int16 quantization step at maximum gain.
  if (std::fabs(z1) < 1e-12f) z1 = 0;
  if (std::fabs(z2) < 1e-12f) z2 = 0;
  return output;
}
}  // namespace

void SpeakerLoudness::configure(Sink sink, void* context, Profile profile) {
  sink_ = sink;
  context_ = context;
  state_ = sink ? State::Active : State::Failed;
  profile_ = profile;
  switch (profile_) {
    case Profile::Standard:
      targetPeak_ = TARGET_PEAK;
      safetyCeiling_ = SAFETY_CEILING;
      break;
    case Profile::Buzzer:
      targetPeak_ = BUZZER_TARGET_PEAK;
      safetyCeiling_ = BUZZER_SAFETY_CEILING;
      break;
  }
  delayHead_ = delayed_ = used_ = 0;
  dc_ = 0;
  highZ1_ = highZ2_ = lowZ1_ = lowZ2_ = compressorEnvelope_ = 0;
  envelopeGain_ = gain_ = minimumGain_ = 1;
  inputSamples_ = outputSamples_ = peakGuardSamples_ = limitedSamples_ = 0;
  maxInputPeak_ = maxOutputPeak_ = 0;
}

float SpeakerLoudness::condition(float input) {
  switch (profile_) {
    case Profile::Standard:
      dc_ += DC_ALPHA * (input - dc_);
      if (std::fabs(dc_) < 1e-9f) dc_ = 0;
      return (input - dc_) * MAKEUP_GAIN;
    case Profile::Buzzer: {
      const float band = filter(filter(input, HIGH_PASS, highZ1_, highZ2_), LOW_PASS, lowZ1_, lowZ2_);
      const float magnitude = std::fabs(band);
      const float rate = magnitude > compressorEnvelope_ ? COMPRESSOR_ATTACK : COMPRESSOR_RELEASE;
      compressorEnvelope_ += rate * (magnitude - compressorEnvelope_);
      if (compressorEnvelope_ < 1e-9f) compressorEnvelope_ = 0;
      // 2:1 above the detector threshold, capped at 6x for quiet material.
      // Silence cannot increase that cap and no gate discards quiet words.
      const float makeup =
          BUZZER_MAX_GAIN * std::sqrt(COMPRESSOR_THRESHOLD / std::max(COMPRESSOR_THRESHOLD, compressorEnvelope_));
      return band * std::clamp(makeup, 1.0f, BUZZER_MAX_GAIN);
    }
  }
  return 0;
}

void SpeakerLoudness::advanceGain(float incoming) {
  const float magnitude = std::fabs(incoming);
  const float compensatedTarget = targetPeak_ * LOOKAHEAD_RELEASE_COMPENSATION;
  const float required = magnitude > compensatedTarget ? compensatedTarget / magnitude : 1.0f;
  envelopeGain_ = std::min(required, std::min(1.0f, envelopeGain_ * RELEASE_FACTOR));
  if (gain_ > envelopeGain_)
    gain_ = envelopeGain_ + ATTACK_DECAY * (gain_ - envelopeGain_);
  else
    gain_ = std::min(envelopeGain_, gain_ * RELEASE_FACTOR);
  // Five ms of lookahead gives 120 attack steps (exp(-10) old-gain residue).
  // Profile-specific safety guards remain separate from ordinary limiting.
  // No window scan or amortized queue is used per sample.
}

bool SpeakerLoudness::flush() {
  if (!used_) return true;
  const size_t count = used_;
  used_ = 0;  // A sink can consume a prefix before failing; never retry it.
  if (!sink_(context_, output_, count)) {
    state_ = State::Failed;
    return false;
  }
  outputSamples_ += count;
  return true;
}

bool SpeakerLoudness::emit(float delayed) {
  float value = delayed * gain_;
  if (delayed != 0 && gain_ < 0.99999f) ++limitedSamples_;
  minimumGain_ = std::min(minimumGain_, gain_);
  if (value > safetyCeiling_ || value < -safetyCeiling_) {
    ++peakGuardSamples_;  // Count actual clamps, not ordinary gain reduction.
    value = std::clamp(value, -safetyCeiling_, safetyCeiling_);
  }
  const int32_t pcm = static_cast<int32_t>(std::lround(value * PCM_SCALE));
  const auto peak = static_cast<uint16_t>(pcm < 0 ? -pcm : pcm);
  maxOutputPeak_ = std::max(maxOutputPeak_, peak);
  output_[used_++] = static_cast<int16_t>(pcm);
  return used_ < BLOCK_SAMPLES || flush();
}

bool SpeakerLoudness::feed(const int16_t* samples, size_t count) {
  if (state_ != State::Active) return false;
  if (!samples && count) {
    state_ = State::Failed;
    return false;
  }
  for (size_t i = 0; i < count; ++i) {
    const int32_t pcm = samples[i];
    const auto peak = static_cast<uint16_t>(pcm < 0 ? -pcm : pcm);
    maxInputPeak_ = std::max(maxInputPeak_, peak);
    const float input = static_cast<float>(pcm) / PCM_SCALE;
    const float value = condition(input);
    advanceGain(value);
    ++inputSamples_;
    if (delayed_ < LOOKAHEAD_SAMPLES) {
      delay_[delayed_++] = value;
    } else {
      const float previous = delay_[delayHead_];
      delay_[delayHead_] = value;
      delayHead_ = (delayHead_ + 1) % LOOKAHEAD_SAMPLES;
      if (!emit(previous)) return false;
    }
  }
  return true;
}

bool SpeakerLoudness::finish() {
  switch (state_) {
    case State::Unconfigured:
      state_ = State::Failed;
      return false;
    case State::Failed:
      return false;
    case State::Finished:
      return true;
    case State::Active:
      break;
  }
  // Very short streams still need the full internal lookahead before their
  // first peak departs. These are envelope-only steps, never emitted silence.
  if (delayed_ && inputSamples_ < LOOKAHEAD_SAMPLES)
    for (size_t i = delayed_; i < LOOKAHEAD_SAMPLES; ++i) advanceGain(0);
  while (delayed_) {
    advanceGain(0);  // No future sample; do not create a high-pass filter tail.
    const float previous = delay_[delayHead_];
    delayHead_ = (delayHead_ + 1) % LOOKAHEAD_SAMPLES;
    --delayed_;
    if (!emit(previous)) return false;
  }
  if (!flush()) return false;
  state_ = State::Finished;
  return true;
}
}  // namespace radio
#endif
