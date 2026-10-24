#include <MonoResampler.h>
#include <RadioDecoder.h>
#include <RadioPlatform.h>
#include <SpeakerLoudness.h>

#include <array>
#include <cassert>
#include <fstream>
#include <iterator>

#include "network/HttpDownloader.h"
#include "services/radio/Player.h"
#include "services/radio/StreamFormat.h"

bool HttpDownloader::hasMemoryForTls() { return true; }
using radio::Error;
using radio::Player;
using radio::State;
namespace {
void waitFor(Player& player, State state) {
  const auto started = std::chrono::steady_clock::now();
  while (player.snapshot().state != state) {
    assert(std::chrono::steady_clock::now() - started < std::chrono::seconds(8));
    vTaskDelay(1);
  }
}
void clean() {
  assert(workspaces.load() == 0 && tasks.load() == 0 && audioOwners.load() == 0 && controls.load() == 0 &&
         sessions.load() == 0);
  requests.store(0);
  writes.store(0);
  audioSamples.store(0);
  extraTime.store(0);
  networkPaused.store(false);
  networkBlocked.store(false);
  capturedPcm.clear();
  pcmWrites.clear();
  dmaMisses.store(0);
}
void errorCase(Error error) {
  Player player;
  assert(player.start("https://radio.example/live"));
  waitFor(player, State::Error);
  assert(player.snapshot().error == error);
  player.stopAndJoin();
  clean();
}

struct Reference {
  radio::Mp3Decoder decoder;
  radio::MonoResampler resampler;
  std::array<int16_t, radio::Mp3Decoder::MAX_PCM_SAMPLES> pcm{};
  std::vector<int16_t> result;
  static bool samples(void* context, const int16_t* data, size_t count) {
    auto& self = *static_cast<Reference*>(context);
    self.result.insert(self.result.end(), data, data + count);
    return true;
  }
  static bool frame(void* context, const uint8_t* data, size_t count) {
    auto& self = *static_cast<Reference*>(context);
    mp3dec_frame_info_t info{};
    const int frames = self.decoder.decode(data, count, self.pcm.data(), info);
    if (!frames) return true;
    if (!self.resampler.inputRate()) assert(self.resampler.configure(info.hz, samples, &self));
    assert(self.resampler.feed(self.pcm.data(), frames, info.channels));
    return true;
  }
};
std::vector<int16_t> reference(size_t byteCount) {
  Reference reference;
  reference.decoder.reset();
  std::array<uint8_t, radio::Mp3Framer::BUFFER_BYTES> input{};
  radio::Mp3Framer framer;
  framer.reset(input.data());
  assert(framer.feed(mp3Bytes.data(), byteCount, Reference::frame, &reference));
  assert(reference.resampler.finish());
  // Compare the asynchronous Player with an offline chain, using different
  // block boundaries. DSP numerical/peak tests are independent in
  // SpeakerLoudnessTest; these checks retain the transport's exact timeline.
  std::vector<int16_t> processed;
  processed.reserve(reference.result.size());
  radio::SpeakerLoudness loudness;
  loudness.configure(
      [](void* context, const int16_t* samples, size_t count) {
        auto& result = *static_cast<std::vector<int16_t>*>(context);
        result.insert(result.end(), samples, samples + count);
        return true;
      },
      &processed, radio::SpeakerLoudness::Profile::Buzzer);
  assert(!loudness.failed());
  assert(loudness.feed(reference.result.data(), reference.result.size()));
  assert(loudness.finish());
  assert(processed.size() == reference.result.size());
  assert(loudness.peakGuardSamples() == 0);
  return processed;
}
void fadeIn(std::vector<int16_t>& samples, size_t offset) {
  for (size_t i = 0; i < 120 && i + offset < samples.size(); ++i)
    samples[offset + i] = static_cast<int16_t>(int32_t{samples[offset + i]} * static_cast<int32_t>(i) / 119);
}
void checkUnbroken(const std::vector<int16_t>& original) {
  auto expected = original;
  fadeIn(expected, 0);
  assert(capturedPcm == expected);  // Every decoded sample, in order, same duration/rate.
}
void checkGap(const std::vector<int16_t>& original) {
  auto silent = [](const PcmWrite& write) {
    return std::all_of(capturedPcm.begin() + write.offset, capturedPcm.begin() + write.offset + write.count,
                       [](int16_t value) { return value == 0; });
  };
  size_t silence = 0;
  // Ignore the MP3 encoder's initial reservoir/delay silence.
  while (silence < pcmWrites.size() && (pcmWrites[silence].offset < 2000 || !silent(pcmWrites[silence]))) ++silence;
  assert(silence > 0 && silence < pcmWrites.size());
  const auto ramp = pcmWrites[silence - 1];
  assert(ramp.count == 512 && ramp.offset > 0);
  const int16_t last = capturedPcm[ramp.offset - 1];
  for (size_t i = 0; i < ramp.count; ++i) {
    const auto expected = i < 120 ? static_cast<int16_t>(int32_t{last} * static_cast<int32_t>(119 - i) / 119) : 0;
    assert(capturedPcm[ramp.offset + i] == expected);
  }
  size_t restart = silence;
  while (restart < pcmWrites.size() && silent(pcmWrites[restart])) ++restart;
  assert(restart < pcmWrites.size());
  const size_t inserted = pcmWrites[restart].offset - ramp.offset;
  auto expected = original;
  fadeIn(expected, 0);
  fadeIn(expected, ramp.offset);
  assert(capturedPcm.size() == expected.size() + inserted);
  assert(std::equal(expected.begin(), expected.begin() + ramp.offset, capturedPcm.begin()));
  assert(std::equal(expected.begin() + ramp.offset, expected.end(), capturedPcm.begin() + pcmWrites[restart].offset));
  std::printf("Long gap: %zu source samples retained, %zu ramp/silence samples inserted at buffering boundary\n",
              expected.size(), inserted);
}

void bufferedTests(const char* longPath) {
  std::ifstream file(longPath, std::ios::binary);
  mp3Bytes.assign(std::istreambuf_iterator<char>(file), {});
  const auto original = reference(mp3Bytes.size());
  assert(original.size() > 96000);
  paceAudio = true;
  for (const auto mode : {NetworkMode::Complete, NetworkMode::Jitter, NetworkMode::LongGap}) {
    networkMode = mode;
    Player player;
    assert(player.start("https://radio.example/live"));
    waitFor(player, State::Idle);
    assert(player.snapshot().error == Error::None);
    assert(player.snapshot().underruns == (mode == NetworkMode::LongGap ? 1u : 0u));
    player.stopAndJoin();
    if (mode == NetworkMode::LongGap)
      checkGap(original);
    else
      checkUnbroken(original);
    std::printf("Paced output mode=%u: samples=%zu writes=%zu DMA deadline misses=%u\n", static_cast<unsigned>(mode),
                capturedPcm.size(), pcmWrites.size(), dmaMisses.load());
    // On the host this checks only our bounded synchronous fake driver cadence;
    // real FreeRTOS deadline/high-water measurements still need device logs.
    assert(dmaMisses.load() == 0);
    clean();
  }
  for (const auto mode : {NetworkMode::FailAfter, NetworkMode::TimeoutAfter}) {
    networkMode = mode;
    const auto partial = reference(16384);
    Player player;
    assert(player.start("https://radio.example/live"));
    waitFor(player, State::Error);
    assert(player.snapshot().error == (mode == NetworkMode::FailAfter ? Error::Network : Error::Timeout));
    player.stopAndJoin();
    checkUnbroken(partial);  // Network failure drains the finite converter/queue tail.
    clean();
  }
  networkMode = NetworkMode::HoldFull;
  {
    Player player;
    assert(player.start("https://radio.example/live"));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (player.snapshot().bufferedMs < 1300 || !networkBlocked.load()) {
      assert(std::chrono::steady_clock::now() < deadline);
      vTaskDelay(1);
    }
    const auto start = std::chrono::steady_clock::now();
    player.stopAndJoin();  // Producer blocked on full ring must observe cancel.
    assert(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(200));
  }
  clean();
  for (const auto mode : {NetworkMode::HoldEarly, NetworkMode::LongGap, NetworkMode::FailAfter}) {
    networkMode = mode;
    Player player;
    assert(player.start("https://radio.example/live"));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    for (;;) {
      const auto snapshot = player.snapshot();
      const bool reached = mode == NetworkMode::HoldEarly ? snapshot.bufferedMs > 0
                           : mode == NetworkMode::LongGap ? snapshot.underruns == 1
                                                          : snapshot.error == Error::Network;
      if (reached) break;
      assert(std::chrono::steady_clock::now() < deadline);
      vTaskDelay(1);
    }
    if (mode == NetworkMode::HoldEarly) assert(audioOwners.load() == 0);
    const auto start = std::chrono::steady_clock::now();
    player.stopAndJoin();  // Startup, rebuffering silence and network-error tail.
    assert(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(200));
    clean();
  }
  // Less than the 512 ms startup watermark still plays on normal EOF.
  mp3Bytes.resize(384 * 5);  // Five complete 48 kHz/128 kbps MPEG-1 frames.
  const auto shortTail = reference(mp3Bytes.size());
  assert(shortTail.size() < 12288 && !shortTail.empty());
  networkMode = NetworkMode::Complete;
  {
    Player player;
    assert(player.start("https://radio.example/live"));
    waitFor(player, State::Idle);
    assert(player.snapshot().error == Error::None && player.snapshot().underruns == 0);
    player.stopAndJoin();
    checkUnbroken(shortTail);
  }
  clean();
  paceAudio = false;
}
}  // namespace
int main(int argc, char** argv) {
  assert(argc == 3);
  std::ifstream file(argv[1], std::ios::binary);
  mp3Bytes.assign(std::istreambuf_iterator<char>(file), {});
  assert(!mp3Bytes.empty());
  capturedPcm.reserve(400000);
  pcmWrites.reserve(2000);
  {
    Player player;
    assert(player.snapshot().state == State::Idle);
    assert(!player.start("file:///book/test.mp3") && player.snapshot().error == Error::InvalidUrl);
    audioSupported = false;
    assert(!player.start("https://radio.example/live") && player.snapshot().error == Error::Unsupported);
    audioSupported = true;
    internalFree = 1000;
    assert(!player.start("https://radio.example/live") && player.snapshot().error == Error::NoMemory);
    internalFree = 100000;
    failControl = true;
    assert(!player.start("https://radio.example/live") && player.snapshot().error == Error::NoMemory);
    failControl = false;
    failTask = true;
    assert(!player.start("https://radio.example/live") && player.snapshot().error == Error::NoMemory);
    failTask = false;
  }
  clean();
  failWorkspace = true;
  errorCase(Error::NoMemory);
  failWorkspace = false;
  failOutputTask = true;
  errorCase(Error::NoMemory);
  failOutputTask = false;
  networkMode = NetworkMode::Hls;
  errorCase(Error::Unsupported);
  networkMode = NetworkMode::RedirectTooLong;
  errorCase(Error::InvalidUrl);
  networkMode = NetworkMode::Complete;
  failBegin = true;
  errorCase(Error::Audio);
  failBegin = false;
  failAudio = true;
  errorCase(Error::Audio);
  failAudio = false;
  for (const auto mode : {NetworkMode::Complete, NetworkMode::Redirect}) {
    networkMode = mode;
    Player player;
    assert(player.start("https://radio.example/live"));
    waitFor(player, State::Idle);
    assert(player.snapshot().frames > 10 && player.snapshot().error == Error::None);
    assert(requests.load() == (mode == NetworkMode::Redirect ? 2 : 1));
    player.stopAndJoin();
    clean();
  }
  for (const auto mode : {NetworkMode::HoldBefore, NetworkMode::HoldAfter}) {
    networkMode = mode;
    for (int i = 0; i < 4; ++i) {
      {
        Player player;
        assert(player.start("https://radio.example/live"));
        while (!requests.load()) vTaskDelay(1);
        if (mode == NetworkMode::HoldAfter) waitFor(player, State::Playing);
        // Destructor must cancel and join before freeing its captured this.
      }
      clean();
    }
  }
  {
    Player player;
    networkMode = NetworkMode::HoldBefore;
    assert(player.start("https://radio.example/live"));
    while (!requests.load()) vTaskDelay(1);
    extraTime.store(11000);
    waitFor(player, State::Error);
    assert(player.snapshot().error == Error::Timeout);
    player.stopAndJoin();
  }
  clean();
  assert(capsDeleted.load() == 32 && outputTasksDeleted.load() == 15);
  bufferedTests(argv[2]);
  std::puts(
      "Player lifecycle: allocation/audio failures, redirects, timeout, jitter/underrun recovery, finite tails and "
      "cancel/join passed");
}
