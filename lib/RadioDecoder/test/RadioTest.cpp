#include <MonoResampler.h>
#include <PcmQueue.h>
#include <RadioDecoder.h>

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>

#include "services/radio/StreamFormat.h"

using namespace radio;
namespace {
bool appendBytes(void* context, const uint8_t* data, size_t size) {
  auto& bytes = *static_cast<std::vector<uint8_t>*>(context);
  bytes.insert(bytes.end(), data, data + size);
  return true;
}
bool appendPcm(void* context, const int16_t* data, size_t size) {
  assert(size > 0 && size <= MonoResampler::BLOCK_SAMPLES);
  auto& samples = *static_cast<std::vector<int16_t>*>(context);
  samples.insert(samples.end(), data, data + size);
  return true;
}
bool rejectPcm(void*, const int16_t*, size_t) { return false; }
bool countFrame(void* context, const uint8_t* bytes, size_t count) {
  FrameHeader header;
  assert(mp3Header(bytes, header) && header.bytes == count);
  ++*static_cast<unsigned*>(context);
  return true;
}
void parserTests() {
  assert(validUrl("https://radio.example:8443/live.mp3?listen=1"));
  assert(validUrl("http://192.0.2.1/stream"));
  assert(!validUrl(nullptr) && !validUrl("") && !validUrl("ftp://radio.example"));
  assert(!validUrl("https://") && !validUrl("http:///stream"));
  assert(!validUrl("https://user:pass@radio.example/live") && !validUrl("http://radio.example/\r\nX: y"));
  assert(!validUrl((std::string("http://radio.example/") + std::string(512, 'a')).c_str()));
  assert(mp3ContentType(nullptr) && mp3ContentType("Audio/MPEG; charset=binary"));
  assert(mp3ContentType("application/octet-stream"));
  for (const char* mime : {"text/html", "audio/aac", "audio/aacp", "application/vnd.apple.mpegurl", "audio/x-mpegurl",
                           "audio/ogg", "audio/mpeg-fake"})
    assert(!mp3ContentType(mime));
  std::vector<uint8_t> wire{'a', 'b', 'c', 'd', 'e', 1};
  wire.insert(wire.end(), 16, 255);
  for (auto byte : {'f', 'g', 'h', 'i', 'j', '\0', 'k', 'l'}) wire.push_back(byte);
  for (size_t split = 1; split <= wire.size(); ++split) {
    IcyFilter icy;
    assert(icy.configure("5"));
    std::vector<uint8_t> actual;
    actual.reserve(12);
    for (size_t offset = 0; offset < wire.size(); offset += split)
      assert(icy.feed(wire.data() + offset, std::min(split, wire.size() - offset), appendBytes, &actual));
    assert(std::string(actual.begin(), actual.end()) == "abcdefghijkl");
  }
  IcyFilter invalid;
  for (const char* value : {"0", "-1", "5junk", "99999999999999999", "1048577"}) assert(!invalid.configure(value));
  assert(invalid.configure("1048576"));
  FrameHeader header;
  const uint8_t mpeg1[] = {0xff, 0xfb, 0x90, 0};
  assert(mp3Header(mpeg1, header) && header.bytes == 417 && header.rate == 44100 && header.channels == 2);
  const uint8_t mpeg25[] = {0xff, 0xe3, 0x38, 0xc0};
  assert(mp3Header(mpeg25, header) && header.bytes == 216 && header.rate == 8000 && header.channels == 1);
  const uint8_t freeBitrate[] = {0xff, 0xfb, 0x00, 0};
  assert(!mp3Header(freeBitrate, header));
  std::vector<uint8_t> frames{'I', 'D', '3', 4, 0, 0, 0, 0, 0, 23};
  frames.insert(frames.end(), 23, 37);
  for (unsigned i = 0; i < 7; ++i) {
    frames.insert(frames.end(), std::begin(mpeg1), std::end(mpeg1));
    frames.insert(frames.end(), 413, 0);
  }
  for (size_t split : {size_t{1}, size_t{7}, size_t{511}, size_t{2048}, frames.size()}) {
    std::array<uint8_t, Mp3Framer::BUFFER_BYTES> storage{};
    Mp3Framer framer;
    framer.reset(storage.data());
    unsigned count = 0;
    for (size_t offset = 0; offset < frames.size(); offset += split)
      assert(framer.feed(frames.data() + offset, std::min(split, frames.size() - offset), countFrame, &count));
    assert(count == 7 && framer.complete());
  }
  // Connecting to an ongoing MP3 may start inside compressed payload; an
  // incidental FF F1 pair must not be classified as AAC before the next frame.
  {
    std::array<uint8_t, Mp3Framer::BUFFER_BYTES> storage{};
    Mp3Framer framer;
    framer.reset(storage.data());
    unsigned count = 0;
    const uint8_t prefix[] = {0x24, 0xff, 0xf1, 0x50, 0x80, 0, 0, 0, 0, 0};
    assert(framer.feed(prefix, sizeof(prefix), countFrame, &count));
    assert(framer.feed(frames.data() + 33, frames.size() - 33, countFrame, &count));
    assert(count == 7 && framer.complete());
  }
  for (const auto& bad : std::vector<std::vector<uint8_t>>{{'<', 'h', 't', 'm', 'l', '>', 'b', 'a', 'd', '!'},
                                                           {'#', 'E', 'X', 'T', 'M', '3', 'U', '\n', 'x', 'x'},
                                                           {'[', 'p', 'l', 'a', 'y', 'l', 'i', 's', 't', ']'},
                                                           {'I', 'D', '3', 4, 0, 0, 0x80, 0, 0, 0},
                                                           {'I', 'D', '3', 4, 0, 0, 1, 0, 0, 0},
                                                           std::vector<uint8_t>(Mp3Framer::BUFFER_BYTES + 10, 0)}) {
    std::array<uint8_t, Mp3Framer::BUFFER_BYTES> storage{};
    Mp3Framer framer;
    framer.reset(storage.data());
    unsigned count = 0;
    assert(!framer.feed(bad.data(), bad.size(), countFrame, &count));
    assert(count == 0);
  }
}

double rms(const std::vector<int16_t>& samples) {
  double power = 0;
  for (size_t i = 100; i < samples.size(); ++i) power += static_cast<double>(samples[i]) * samples[i];
  return std::sqrt(power / (samples.size() - 100));
}
std::vector<int16_t> converted(uint32_t rate, unsigned hz, size_t split, unsigned channels) {
  std::vector<int16_t> input, output;
  input.reserve(rate * channels);
  output.reserve(24002);
  for (uint32_t i = 0; i < rate; ++i) {
    const auto sample = static_cast<int16_t>(16000 * std::sin(6.283185307179586 * hz * i / rate));
    input.push_back(sample);
    if (channels == 2) input.push_back(sample);
  }
  MonoResampler resampler;
  assert(resampler.configure(rate, appendPcm, &output));
  for (size_t offset = 0; offset < rate; offset += split)
    assert(resampler.feed(input.data() + offset * channels, std::min<size_t>(split, rate - offset), channels));
  assert(resampler.finish());
  assert(output.size() == 24000);  // Exactly one second; no sample-rate relabeling.
  return output;
}
void resamplingTests() {
  for (uint32_t rate : {8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000}) {
    const auto baseline = converted(rate, 997, rate, 1);
    for (unsigned split : {1, 17, 1152}) assert(baseline == converted(rate, 997, split, 2));
    unsigned crossings = 0;
    for (size_t i = 1; i < baseline.size(); ++i)
      if (baseline[i - 1] <= 0 && baseline[i] > 0) ++crossings;
    assert(crossings >= 996 && crossings <= 998);  // Pitch stays 997 Hz at 24 kHz output.
  }
  assert(rms(converted(48000, 18000, 1152, 1)) < rms(converted(48000, 1000, 1152, 1)) * 0.3);
  MonoResampler unsupported;
  assert(!unsupported.configure(96000, appendPcm, nullptr));
  assert(unsupported.configure(24000, rejectPcm, nullptr));
  std::array<int16_t, 600> pcm{};
  assert(!unsupported.feed(pcm.data(), pcm.size(), 1));
}

void queueTests() {
  std::array<int16_t, PcmQueue::CAPACITY> storage{}, source{};
  for (size_t i = 0; i < source.size(); ++i) source[i] = static_cast<int16_t>(i);
  PcmQueue queue;
  queue.reset(storage.data());
  assert(queue.available() == 0);
  assert(queue.push(source.data(), source.size()) == source.size());
  assert(queue.push(source.data(), 1) == 0);  // Full ring never overwrites unread PCM.
  std::array<int16_t, 777> block{};
  assert(queue.pop(block.data(), block.size()) == block.size());
  assert(std::equal(block.begin(), block.end(), source.begin()));
  assert(queue.push(source.data(), block.size()) == block.size());
  size_t index = block.size();
  while (queue.available()) {
    const size_t count = queue.pop(block.data(), block.size());
    for (size_t i = 0; i < count; ++i) assert(block[i] == source[(index++) % source.size()]);
  }
  assert(index == source.size() + block.size());

  // Production SPSC acquire/release handoff under real concurrent writes and
  // repeated wraparound. Separate uneven chunk sizes exercise split copies.
  queue.reset(storage.data());
  constexpr uint32_t TOTAL = 400000;
  std::thread producer([&] {
    std::array<int16_t, 257> input{};
    uint32_t sent = 0;
    while (sent < TOTAL) {
      const size_t count = std::min(size_t{TOTAL - sent}, input.size());
      for (size_t i = 0; i < count; ++i) input[i] = static_cast<int16_t>((sent + i) % 32767);
      sent += queue.push(input.data(), count);
      std::this_thread::yield();
    }
  });
  uint32_t received = 0;
  while (received < TOTAL) {
    const size_t count = queue.pop(block.data(), block.size());
    for (size_t i = 0; i < count; ++i) assert(block[i] == static_cast<int16_t>((received + i) % 32767));
    received += count;
    std::this_thread::yield();
  }
  producer.join();
  assert(queue.available() == 0);
}

struct ActualDecode {
  Mp3Decoder decoder;
  MonoResampler resampler;
  std::array<int16_t, Mp3Decoder::MAX_PCM_SAMPLES> pcm{};
  std::vector<int16_t> output;
  uint32_t frames = 0, samples = 0, rate = 0;
  static bool frame(void* context, const uint8_t* bytes, size_t size) {
    auto& self = *static_cast<ActualDecode*>(context);
    mp3dec_frame_info_t info{};
    const int count = self.decoder.decode(bytes, size, self.pcm.data(), info);
    assert(info.layer == 3 && info.frame_bytes == int(size));
    if (!count) return true;
    if (!self.rate) {
      self.rate = info.hz;
      assert(self.resampler.configure(self.rate, appendPcm, &self.output));
    }
    assert(self.rate == unsigned(info.hz));
    assert(self.resampler.feed(self.pcm.data(), count, info.channels));
    ++self.frames;
    self.samples += count;
    return true;
  }
};
void actualMp3(const char* path) {
  std::ifstream file(path, std::ios::binary);
  const std::vector<uint8_t> mp3(std::istreambuf_iterator<char>(file), {});
  assert(!mp3.empty());
  std::vector<int16_t> baseline;
  baseline.reserve(30000);
  for (size_t split : {1, 37, 2048, 8192}) {
    ActualDecode decoded;
    decoded.decoder.reset();
    decoded.output.reserve(30000);
    std::array<uint8_t, Mp3Framer::BUFFER_BYTES> storage{};
    Mp3Framer framer;
    framer.reset(storage.data());
    for (size_t offset = 0; offset < mp3.size(); offset += split)
      assert(framer.feed(mp3.data() + offset, std::min(split, mp3.size() - offset), ActualDecode::frame, &decoded));
    assert(framer.complete());
    assert(decoded.frames > 10 && decoded.resampler.finish());
    assert(decoded.output.size() == (uint64_t{decoded.samples} * 24000 + decoded.rate - 1) / decoded.rate);
    if (baseline.empty()) baseline = decoded.output;
    assert(baseline == decoded.output);
    assert(rms(decoded.output) > 1000);
    std::printf("MP3 %u Hz: %u frames, %u input samples -> %zu mono/24k samples, chunk=%zu\n", decoded.rate,
                decoded.frames, decoded.samples, decoded.output.size(), split);
  }
}
}  // namespace
int main(int argc, char** argv) {
  parserTests();
  resamplingTests();
  queueTests();
  for (int i = 1; i < argc; ++i) actualMp3(argv[i]);
  std::printf("Radio parser/resampling passed; decoder-state=%zu bytes, resampler=%zu bytes\n", sizeof(Mp3Decoder),
              sizeof(MonoResampler));
}
