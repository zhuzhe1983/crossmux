#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

#include "SpeakerLoudness.h"

using radio::SpeakerLoudness;
using Profile = SpeakerLoudness::Profile;

namespace {
constexpr double PI = 3.14159265358979323846;
constexpr size_t RATE = SpeakerLoudness::SAMPLE_RATE;

struct Collector {
  std::vector<int16_t> samples;
  size_t calls = 0, failCall = 0;

  static bool accept(void* context, const int16_t* values, size_t count) {
    auto& self = *static_cast<Collector*>(context);
    assert(values && count && count <= SpeakerLoudness::BLOCK_SAMPLES);
    ++self.calls;
    if (self.calls == self.failCall) {
      // A cancelled queue may consume a prefix before returning failure.
      self.samples.insert(self.samples.end(), values, values + count / 2);
      return false;
    }
    self.samples.insert(self.samples.end(), values, values + count);
    return true;
  }
};

int peak(const std::vector<int16_t>& values, size_t start = 0) {
  int result = 0;
  for (size_t i = start; i < values.size(); ++i) result = std::max(result, std::abs(int{values[i]}));
  return result;
}

double rms(const std::vector<int16_t>& values, size_t start, size_t stop) {
  assert(start < stop && stop <= values.size());
  double sum = 0;
  for (size_t i = start; i < stop; ++i) sum += static_cast<double>(values[i]) * values[i];
  return std::sqrt(sum / (stop - start));
}

double component(const std::vector<int16_t>& values, double frequency) {
  // Analyse a whole second, excluding filter/level-control startup transients.
  assert(values.size() >= 2 * RATE);
  double real = 0, imaginary = 0;
  for (size_t i = RATE; i < 2 * RATE; ++i) {
    const double phase = 2 * PI * frequency * i / RATE;
    real += values[i] * std::cos(phase);
    imaginary += values[i] * std::sin(phase);
  }
  return 2 * std::hypot(real, imaginary) / RATE;
}

std::vector<int16_t> tone(double frequency, double amplitude, size_t count = 2 * RATE) {
  std::vector<int16_t> result(count);
  for (size_t i = 0; i < count; ++i)
    result[i] = static_cast<int16_t>(std::lround(amplitude * std::sin(2 * PI * frequency * i / RATE)));
  return result;
}

std::vector<int16_t> noise(size_t count, int amplitude = 32767) {
  std::vector<int16_t> result(count);
  uint32_t state = 0x313faced;
  for (auto& value : result) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    value = static_cast<int16_t>(static_cast<int>(state % (2 * amplitude + 1)) - amplitude);
  }
  return result;
}

void completed(SpeakerLoudness& dsp, Collector& output, size_t count) {
  assert(dsp.finish() && dsp.finished() && !dsp.failed());
  assert(dsp.inputSamples() == count && dsp.outputSamples() == count && output.samples.size() == count);
  assert(dsp.maxOutputPeak() == peak(output.samples));
  assert(dsp.peakGuardSamples() == 0);
  assert(peak(output.samples) <= static_cast<int>(32768 * SpeakerLoudness::BUZZER_SAFETY_CEILING + 0.5f));
  const size_t calls = output.calls;
  assert(dsp.finish() && output.calls == calls);  // No replay or padded/filter tail.
  assert(!dsp.feed(nullptr, 0) && dsp.finished());
}

std::vector<int16_t> run(const std::vector<int16_t>& input, size_t chunk = 173, Profile profile = Profile::Buzzer) {
  assert(chunk);
  Collector output;
  output.samples.reserve(input.size());
  SpeakerLoudness dsp;
  dsp.configure(Collector::accept, &output, profile);
  assert(dsp.profile() == profile && !dsp.failed() && dsp.feed(nullptr, 0));
  for (size_t offset = 0; offset < input.size(); offset += std::min(chunk, input.size() - offset))
    assert(dsp.feed(input.data() + offset, std::min(chunk, input.size() - offset)));
  completed(dsp, output, input.size());
  return output.samples;
}

void streamContract() {
  for (size_t count : {0u, 1u, 2u, 119u, 120u, 121u, 511u, 512u, 513u, 1025u, 4096u}) {
    auto input = noise(count);
    if (count) input[0] = -32768;
    const auto reference = run(input, std::max(size_t{1}, count));
    assert(run(input, 1) == reference);
    assert(run(input, 127) == reference);
    assert(run(input, 512) == reference);
  }
  const auto input = noise(1537);
  const auto reference = run(input);
  for (size_t chunk = 1; chunk <= 513; ++chunk) assert(run(input, chunk) == reference);

  SpeakerLoudness dsp;
  Collector failed;
  failed.samples.reserve(input.size());
  failed.failCall = 2;
  dsp.configure(Collector::accept, &failed, Profile::Buzzer);
  assert(!dsp.feed(input.data(), input.size()) && dsp.failed());
  assert(failed.calls == 2 && failed.samples.size() == 768 && dsp.outputSamples() == 512);
  assert(!dsp.finish() && !dsp.feed(input.data(), input.size()));
  assert(failed.calls == 2 && failed.samples.size() == 768);

  // Reset must discard both profiles' filter/envelope/history, even after failure.
  for (Profile profile : {Profile::Standard, Profile::Buzzer, Profile::Standard, Profile::Buzzer}) {
    Collector reused;
    reused.samples.reserve(input.size());
    dsp.configure(Collector::accept, &reused, profile);
    assert(dsp.inputSamples() == 0 && dsp.outputSamples() == 0 && dsp.peakGuardSamples() == 0);
    assert(dsp.feed(input.data(), input.size()));
    completed(dsp, reused, input.size());
    assert(reused.samples == run(input, 71, profile));
  }
  Collector tailFailure;
  tailFailure.failCall = 1;
  dsp.configure(Collector::accept, &tailFailure, Profile::Buzzer);
  assert(dsp.feed(input.data(), 1));
  assert(!dsp.finish() && dsp.failed() && !dsp.finish() && tailFailure.calls == 1);
  dsp.configure(Collector::accept, &tailFailure, Profile::Buzzer);
  assert(!dsp.feed(nullptr, 1) && dsp.failed());
  dsp.configure(nullptr, nullptr, Profile::Buzzer);
  assert(dsp.failed() && !dsp.finish());
}

void silenceAndLimits() {
  assert(peak(run(std::vector<int16_t>(3 * RATE, 0))) == 0);
  for (int16_t dc : {int16_t{32767}, int16_t{-32768}, int16_t{10000}})
    assert(peak(run(std::vector<int16_t>(2 * RATE, dc)), RATE) <= 1);

  auto stress = noise(8 * RATE);
  std::fill(stress.begin(), stress.begin() + RATE, int16_t{-32768});
  std::fill(stress.begin() + RATE, stress.begin() + 2 * RATE, int16_t{32767});
  for (size_t i = 2 * RATE; i < 3 * RATE; ++i) stress[i] = i & 1 ? 32767 : -32768;
  std::fill(stress.begin() + 6 * RATE, stress.end(), int16_t{0});
  const auto result = run(stress);
  assert(peak(result) < 31000 && peak(result, 7 * RATE) <= 1);

  // A long quiet interval must not cause subsequent low-level noise to acquire
  // runaway gain. The repeated noise waveform has identical steady-state output.
  const auto quiet = noise(2 * RATE, 8);
  std::vector<int16_t> spaced(7 * RATE, 0);
  std::copy(quiet.begin(), quiet.end(), spaced.begin());
  std::copy(quiet.begin(), quiet.end(), spaced.begin() + 5 * RATE);
  const auto processed = run(spaced, 19);
  const double before = rms(processed, RATE, 2 * RATE);
  const double after = rms(processed, 6 * RATE, 7 * RATE);
  assert(before > 0 && std::fabs(after / before - 1) < 0.01);
  assert(peak(processed) < 8 * 14);  // Includes bounded filter-transient overshoot.
  assert(peak(std::vector<int16_t>(processed.begin() + 3 * RATE, processed.begin() + 5 * RATE)) == 0);
}

void bandwidthAndTimeline() {
  // Small-signal tests stay below compressor/limiter thresholds. Frequencies
  // validate a speech-focused profile, not a claimed physical buzzer response.
  const double centre = component(run(tone(997, 300)), 997);
  assert(centre > 300 * 5.8 && centre <= 300 * SpeakerLoudness::BUZZER_MAX_GAIN + 1);
  for (const auto& point : {std::pair<double, double>{50, 0.03}, {100, 0.10}, {8000, 0.18}, {10000, 0.05}})
    assert(component(run(tone(point.first, 300)), point.first) / centre < point.second);
  assert(component(run(tone(3000, 300)), 3000) / centre > 0.85);

  for (double frequency : {997.0, 3000.0}) {
    const auto input = tone(frequency, 1000);
    const auto output = run(input, 367);
    assert(output.size() == 2 * RATE);  // Exactly two seconds, no sample-rate relabeling.
    size_t rising = 0;
    for (size_t i = RATE + 1; i < output.size(); ++i)
      if (output[i - 1] <= 0 && output[i] > 0) ++rising;
    assert(std::fabs(double(rising) - frequency) <= 1.1);
  }
}

void contentTradeoff() {
  // A reproducible two-tone proxy: the low component competes for peak budget
  // while the mid-band component represents speech. It is not recorded speech
  // or an acoustic model. Total RMS alone would hide the intended improvement.
  auto mixed = tone(100, 0.7 * 32768);
  const auto middle = tone(997, 0.08 * 32768);
  for (size_t i = 0; i < mixed.size(); ++i) mixed[i] = static_cast<int16_t>(mixed[i] + middle[i]);
  const auto standard = run(mixed, 512, Profile::Standard);
  const auto buzzer = run(mixed);
  assert(component(buzzer, 997) > 2 * component(standard, 997));
  assert(component(buzzer, 100) < component(standard, 100));
  std::printf("Buzzer proxy: 997 Hz component gain %.2f dB; electrical signal only, not measured loudness\n",
              20 * std::log10(component(buzzer, 997) / component(standard, 997)));

  // A growing input remains compressed; it does not retain the quiet 6x gain
  // through overload. This catches accidental bypass of the limiter/compressor.
  const double quietGain = component(run(tone(997, 500)), 997) / 500;
  const auto loud = run(tone(997, 30000));
  const double loudGain = component(loud, 997) / 30000;
  assert(quietGain > 5 && loudGain < quietGain / 3);
  assert(peak(loud) <= static_cast<int>(32768 * SpeakerLoudness::BUZZER_TARGET_PEAK + 32));
}
}  // namespace

int main() {
  static_assert(sizeof(SpeakerLoudness) <= 2048);
  static_assert(std::is_trivially_destructible<SpeakerLoudness>::value);
  streamContract();
  silenceAndLimits();
  bandwidthAndTimeline();
  contentTradeoff();
  std::printf("BuzzerProfile PASS: streaming/reuse/failure, silence/DC/bounds, bandwidth/pitch/duration; sizeof=%zu\n",
              sizeof(SpeakerLoudness));
}
