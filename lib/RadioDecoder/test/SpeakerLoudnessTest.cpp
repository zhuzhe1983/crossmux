#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "SpeakerLoudness.h"

using radio::SpeakerLoudness;

namespace {
constexpr double PI = 3.14159265358979323846;

struct Collector {
  std::vector<int16_t> samples;
  size_t calls = 0;
  size_t failCall = 0;
  static bool accept(void* context, const int16_t* values, size_t count) {
    auto& self = *static_cast<Collector*>(context);
    assert(values && count && count <= 512);
    ++self.calls;
    if (self.calls == self.failCall) {
      // A real queue can accept a prefix before cancellation. It cannot be
      // retried just because the callback reports failure.
      self.samples.insert(self.samples.end(), values, values + count / 2);
      return false;
    }
    self.samples.insert(self.samples.end(), values, values + count);
    return true;
  }
};

uint16_t peak(const std::vector<int16_t>& data) {
  int32_t maximum = 0;
  for (int32_t sample : data) maximum = std::max(maximum, sample < 0 ? -sample : sample);
  return static_cast<uint16_t>(maximum);
}

double rms(const std::vector<int16_t>& data, size_t start, size_t stop) {
  assert(stop > start && stop <= data.size());
  double energy = 0;
  for (size_t i = start; i < stop; ++i) energy += static_cast<double>(data[i]) * data[i];
  return std::sqrt(energy / (stop - start));
}

std::vector<int16_t> randomInput(size_t count) {
  std::vector<int16_t> result(count);
  uint32_t state = 0x19750422;
  for (auto& sample : result) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    sample = static_cast<int16_t>(static_cast<int32_t>(state & 65535) - 32768);
  }
  return result;
}

std::vector<int16_t> tone(double frequency, int amplitude, size_t count) {
  std::vector<int16_t> result(count);
  for (size_t i = 0; i < count; ++i)
    result[i] = static_cast<int16_t>(std::lround(amplitude * std::sin(2 * PI * frequency * i / 24000)));
  return result;
}

void completed(SpeakerLoudness& dsp, Collector& destination, const std::vector<int16_t>& input) {
  assert(dsp.finish());
  assert(dsp.finished() && !dsp.failed());
  assert(dsp.inputSamples() == input.size());
  assert(dsp.outputSamples() == input.size());
  assert(destination.samples.size() == input.size());
  assert(dsp.maxInputPeak() == peak(input));
  assert(dsp.maxOutputPeak() == peak(destination.samples));
  assert(dsp.peakGuardSamples() == 0);         // Normal limiting must not use the hard guard.
  assert(peak(destination.samples) <= 26214);  // 0.80 FS, safely below int16 wrap.
  assert(dsp.minimumGain() > 0 && dsp.minimumGain() <= 1);
  const size_t calls = destination.calls;
  assert(dsp.finish());
  assert(destination.calls == calls && destination.samples.size() == input.size());
  assert(!dsp.feed(input.data(), input.size()));
  assert(dsp.finished() && !dsp.failed());
}

std::vector<int16_t> run(const std::vector<int16_t>& input, size_t chunk) {
  assert(chunk);
  Collector destination;
  SpeakerLoudness dsp;
  dsp.configure(Collector::accept, &destination);
  assert(dsp.feed(nullptr, 0));
  for (size_t position = 0; position < input.size();) {
    const size_t count = std::min(chunk, input.size() - position);
    assert(dsp.feed(input.data() + position, count));
    position += count;
  }
  completed(dsp, destination, input);
  return destination.samples;
}

void tailsAndChunks() {
  for (size_t count : {0u, 1u, 2u, 119u, 120u, 121u, 511u, 512u, 513u, 631u, 632u, 633u, 1024u, 1025u, 4096u}) {
    auto input = randomInput(count);
    if (count) input[0] = -32768;  // A one-sample stream still needs full attack preparation.
    const auto whole = run(input, std::max(size_t{1}, count));
    assert(run(input, 1) == whole);
    assert(run(input, 127) == whole);
    assert(run(input, 512) == whole);
  }

  const auto input = randomInput(2049);
  const auto reference = run(input, input.size());
  for (size_t chunk = 1; chunk <= 513; ++chunk) assert(run(input, chunk) == reference);

  const auto splitInput = randomInput(513);
  const auto splitReference = run(splitInput, splitInput.size());
  for (size_t split = 0; split <= splitInput.size(); ++split) {
    Collector destination;
    SpeakerLoudness dsp;
    dsp.configure(Collector::accept, &destination);
    assert(dsp.feed(splitInput.data(), split));
    assert(dsp.feed(splitInput.data() + split, splitInput.size() - split));
    completed(dsp, destination, splitInput);
    assert(destination.samples == splitReference);
  }
}

void failureAndReset() {
  SpeakerLoudness dsp;
  assert(!dsp.feed(nullptr, 0));
  assert(!dsp.finish() && dsp.failed());
  dsp.configure(nullptr, nullptr);
  assert(dsp.failed() && !dsp.finish());

  const auto input = randomInput(2400);
  Collector failed;
  failed.failCall = 2;
  dsp.configure(Collector::accept, &failed);
  assert(!dsp.feed(input.data(), input.size()));
  assert(dsp.failed() && !dsp.finished());
  assert(failed.calls == 2 && failed.samples.size() == 768);
  assert(dsp.outputSamples() == 512);
  assert(!dsp.feed(input.data(), input.size()));
  assert(!dsp.finish() && !dsp.finish());
  assert(failed.calls == 2 && failed.samples.size() == 768);

  Collector shortFailure;
  shortFailure.failCall = 1;
  dsp.configure(Collector::accept, &shortFailure);
  assert(dsp.feed(input.data(), 100));
  assert(!dsp.finish() && dsp.failed());
  assert(shortFailure.calls == 1 && shortFailure.samples.size() == 50);
  assert(dsp.outputSamples() == 0);
  assert(!dsp.finish() && !dsp.feed(input.data(), 1));
  assert(shortFailure.calls == 1 && shortFailure.samples.size() == 50);

  Collector invalid;
  dsp.configure(Collector::accept, &invalid);
  assert(!dsp.feed(nullptr, 1) && dsp.failed());
  assert(!dsp.finish() && invalid.calls == 0);

  const auto reference = run(input, input.size());
  for (unsigned reuse = 0; reuse < 2; ++reuse) {
    Collector recovered;
    dsp.configure(Collector::accept, &recovered);
    assert(!dsp.failed() && !dsp.finished());
    assert(dsp.inputSamples() == 0 && dsp.outputSamples() == 0 && dsp.peakGuardSamples() == 0);
    assert(dsp.feed(input.data(), input.size()));
    completed(dsp, recovered, input);
    assert(recovered.samples == reference);
  }
}

void rejectionAndPeaks() {
  for (int16_t dc : {int16_t{10000}, int16_t{-32768}, int16_t{32767}}) {
    const auto result = run(std::vector<int16_t>(24000, dc), 179);
    assert(std::all_of(result.begin() + 12000, result.end(), [](int16_t sample) { return sample == 0; }));
  }
  auto silence = run(std::vector<int16_t>(24000, 0), 179);
  assert(peak(silence) == 0);

  std::vector<int16_t> burst(24000, 0);
  for (size_t i = 0; i < 1200; ++i) burst[i] = (i & 1) ? 32767 : -32768;
  const auto burstResult = run(burst, 173);
  assert(std::all_of(burstResult.begin() + 3600, burstResult.end(), [](int16_t sample) { return sample == 0; }));

  auto stress = randomInput(96000);
  std::fill(stress.begin(), stress.begin() + 12000, int16_t{-32768});
  std::fill(stress.begin() + 12000, stress.begin() + 24000, int16_t{32767});
  for (size_t i = 24000; i < 48000; ++i) stress[i] = (i & 1) ? 32767 : -32768;
  Collector destination;
  SpeakerLoudness dsp;
  dsp.configure(Collector::accept, &destination);
  assert(dsp.feed(stress.data(), stress.size()));
  completed(dsp, destination, stress);
  assert(dsp.limitedSamples() > 48000);
  assert(dsp.minimumGain() < 0.25f);
  // Peak-limited samples remain well below the separate emergency ceiling.
  assert(peak(destination.samples) <= 24000);

  // Lookahead delays delivery, not the stream's sample positions or duration.
  std::vector<int16_t> impulse(1000, 0);
  impulse[12] = 1000;
  const auto impulseResult = run(impulse, 37);
  assert(std::all_of(impulseResult.begin(), impulseResult.begin() + 12, [](int16_t sample) { return sample == 0; }));
  assert(impulseResult[12] != 0);
}

void frequencyAndQuietGain() {
  for (double frequency : {997.0, 3000.0}) {
    const auto input = tone(frequency, 2000, 48000);
    const auto output = run(input, 367);
    const double boost = rms(output, 2400, 45600) / rms(input, 2400, 45600);
    assert(boost > 2.5 && boost <= 2.8);
    size_t rising = 0;
    for (size_t i = 2401; i < 45600; ++i)
      if (output[i - 1] <= 0 && output[i] > 0) ++rising;
    const double expected = frequency * (45600 - 2400) / 24000;
    assert(std::fabs(static_cast<double>(rising) - expected) <= 1.1);
  }
}
}  // namespace

int main() {
  static_assert(sizeof(SpeakerLoudness) <= 2048);
  static_assert(std::is_trivially_destructible<SpeakerLoudness>::value);
  tailsAndChunks();
  failureAndReset();
  rejectionAndPeaks();
  frequencyAndQuietGain();
  std::printf("SpeakerLoudness PASS: exact tails/chunks, latched failures, DC/silence, peaks, frequency; sizeof=%zu\n",
              sizeof(SpeakerLoudness));
}
