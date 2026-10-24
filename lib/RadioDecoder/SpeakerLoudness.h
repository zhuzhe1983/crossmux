#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace radio {
// Fixed-rate output conditioning, owned by the decoder worker. No allocation,
// SDK calls or resampling. Store as a Work member, not on the task stack.
class SpeakerLoudness {
 public:
  enum class Profile : uint8_t { Standard, Buzzer };
  using Sink = bool (*)(void*, const int16_t*, size_t);
  static constexpr uint32_t SAMPLE_RATE = 24000;
  static constexpr size_t LOOKAHEAD_SAMPLES = 120;
  static constexpr size_t BLOCK_SAMPLES = 512;
  static constexpr float MAKEUP_GAIN = 2.75f;
  static constexpr float TARGET_PEAK = 0.72f;
  static constexpr float SAFETY_CEILING = 0.80f;
  static constexpr float BUZZER_MAX_GAIN = 6.0f;
  static constexpr float BUZZER_TARGET_PEAK = 0.86f;
  static constexpr float BUZZER_SAFETY_CEILING = 0.94f;

  // Resets the entire stream state. Null sink leaves failed()==true. Single
  // owner/non-reentrant: configure/feed/finish and getters are not synchronized.
  void configure(Sink sink, void* context, Profile profile = Profile::Standard);
  // Arbitrary input block sizes; zero count is a no-op while active. On false,
  // stop feeding: sink failure is latched and its batch is never replayed.
  bool feed(const int16_t* samples, size_t count);
  // Emits exactly the pending input samples, no padding or extra filter tail.
  // Successful finish is idempotent. feed() after finish returns false.
  bool finish();

  bool failed() const { return state_ == State::Failed; }
  bool finished() const { return state_ == State::Finished; }
  uint64_t inputSamples() const { return inputSamples_; }
  // Batches whose sink returned true; a failed sink may have consumed a prefix,
  // which this all-or-stop callback interface cannot count or safely replay.
  uint64_t outputSamples() const { return outputSamples_; }
  uint64_t peakGuardSamples() const { return peakGuardSamples_; }
  uint64_t limitedSamples() const { return limitedSamples_; }
  // PCM magnitudes (input includes |-32768|). min gain excludes fixed makeup.
  uint16_t maxInputPeak() const { return maxInputPeak_; }
  uint16_t maxOutputPeak() const { return maxOutputPeak_; }
  float minimumGain() const { return minimumGain_; }
  Profile profile() const { return profile_; }

 private:
  enum class State : uint8_t { Unconfigured, Active, Finished, Failed };
  State state_ = State::Unconfigured;
  Profile profile_ = Profile::Standard;
  Sink sink_ = nullptr;
  void* context_ = nullptr;
  // 480 B lookahead + 1024 B output. configure only resets indices; every slot
  // is written before use, so reuse needs no large temporary/stack buffer.
  float delay_[LOOKAHEAD_SAMPLES]{};
  int16_t output_[BLOCK_SAMPLES]{};
  size_t delayHead_ = 0, delayed_ = 0, used_ = 0;
  float dc_ = 0, envelopeGain_ = 1, gain_ = 1, minimumGain_ = 1;
  // 20 bytes in the existing PSRAM Work: two biquad delay pairs and one
  // compressor envelope. No per-sample storage, task or heap allocation.
  float highZ1_ = 0, highZ2_ = 0, lowZ1_ = 0, lowZ2_ = 0, compressorEnvelope_ = 0;
  float targetPeak_ = TARGET_PEAK, safetyCeiling_ = SAFETY_CEILING;
  uint64_t inputSamples_ = 0, outputSamples_ = 0, peakGuardSamples_ = 0, limitedSamples_ = 0;
  uint16_t maxInputPeak_ = 0, maxOutputPeak_ = 0;

  void advanceGain(float incoming);
  float condition(float input);
  bool emit(float delayed);
  bool flush();
};
static_assert(sizeof(SpeakerLoudness) <= 2048, "Speaker conditioning must stay within the 2 KiB Work budget");
static_assert(std::is_trivially_destructible<SpeakerLoudness>::value);
}  // namespace radio
