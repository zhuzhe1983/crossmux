#pragma once
#include <cstddef>
#include <cstdint>

namespace radio {
constexpr size_t MAX_URL_BYTES = 512;
bool validUrl(const char* url);
bool mp3ContentType(const char* contentType);

using ByteSink = bool (*)(void*, const uint8_t*, size_t);

// Removes interleaved ICY metadata even when a server ignores Icy-MetaData: 0.
class IcyFilter {
 public:
  bool configure(const char* interval);
  bool feed(const uint8_t* data, size_t size, ByteSink sink, void* context);

 private:
  uint32_t interval_ = 0, audioLeft_ = 0, metadataLeft_ = 0;
  bool lengthByte_ = false;
};

struct FrameHeader {
  uint16_t bytes = 0, rate = 0;
  uint8_t channels = 0;
};
bool mp3Header(const uint8_t* bytes, FrameHeader& out);

// Keeps split frame/tag boundaries without letting minimp3 discard incomplete
// network chunks. Storage is caller-owned PSRAM, never a growing container.
class Mp3Framer {
 public:
  static constexpr size_t BUFFER_BYTES = 8192;
  void reset(uint8_t* buffer);
  bool feed(const uint8_t* data, size_t size, ByteSink sink, void* context);
  bool complete() const { return used_ == 0 && tagLeft_ == 0; }

 private:
  uint8_t* buffer_ = nullptr;
  size_t used_ = 0;
  uint32_t tagLeft_ = 0, skipped_ = 0;
  bool initial_ = true;
  bool consume(ByteSink sink, void* context);
  void discard(size_t bytes);
};
}  // namespace radio
