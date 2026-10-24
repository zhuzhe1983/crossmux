#include "StreamFormat.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace radio {
namespace {
bool equalsType(const char* text, const char* allowed) {
  while (*allowed) {
    if (std::tolower(static_cast<unsigned char>(*text++)) != *allowed++) return false;
  }
  return !*text || *text == ';' || *text == ' ';
}
}  // namespace

bool validUrl(const char* url) {
  if (!url || !*url) return false;
  const size_t length = strnlen(url, MAX_URL_BYTES + 1);
  if (length > MAX_URL_BYTES) return false;
  size_t host = 0;
  if (!std::strncmp(url, "http://", 7)) host = 7;
  if (!std::strncmp(url, "https://", 8)) host = 8;
  if (!host || host == length || url[host] == '/' || url[host] == ':') return false;
  for (size_t i = 0; i < length; ++i) {
    const unsigned char c = url[i];
    if (c <= 32 || c >= 127 || c == '\\' || c == '#' || c == '@') return false;
  }
  return true;
}

bool mp3ContentType(const char* type) {
  if (!type || !*type) return true;  // Some Icecast relays omit MIME; inspect frames.
  return equalsType(type, "audio/mpeg") || equalsType(type, "audio/mp3") || equalsType(type, "audio/x-mpeg") ||
         equalsType(type, "audio/mpeg3") || equalsType(type, "application/octet-stream");
}

bool IcyFilter::configure(const char* value) {
  interval_ = audioLeft_ = metadataLeft_ = 0;
  lengthByte_ = false;
  if (!value || !*value) return true;
  uint32_t interval = 0;
  for (; *value; ++value) {
    if (*value < '0' || *value > '9' || interval > 104857) return false;
    interval = interval * 10 + static_cast<unsigned>(*value - '0');
  }
  if (!interval || interval > 1024 * 1024) return false;
  interval_ = audioLeft_ = interval;
  return true;
}

bool IcyFilter::feed(const uint8_t* bytes, size_t size, ByteSink sink, void* context) {
  if ((!bytes && size) || !sink) return false;
  if (!interval_) return !size || sink(context, bytes, size);
  while (size) {
    if (metadataLeft_) {
      const auto count = std::min(size, static_cast<size_t>(metadataLeft_));
      metadataLeft_ -= count;
      bytes += count;
      size -= count;
      if (!metadataLeft_) audioLeft_ = interval_;
    } else if (lengthByte_) {
      metadataLeft_ = *bytes++ * 16u;
      --size;
      lengthByte_ = false;
      if (!metadataLeft_) audioLeft_ = interval_;
    } else {
      const auto count = std::min(size, static_cast<size_t>(audioLeft_));
      if (!sink(context, bytes, count)) return false;
      audioLeft_ -= count;
      bytes += count;
      size -= count;
      if (!audioLeft_) lengthByte_ = true;
    }
  }
  return true;
}

bool mp3Header(const uint8_t* p, FrameHeader& out) {
  out = {};
  if (!p || p[0] != 255 || (p[1] & 0xe0) != 0xe0 || (p[1] & 6) != 2) return false;
  const unsigned version = (p[1] >> 3) & 3, index = (p[2] >> 4) & 15, frequency = (p[2] >> 2) & 3;
  if (version == 1 || !index || index == 15 || frequency == 3) return false;
  constexpr uint16_t rates[] = {44100, 48000, 32000};
  constexpr uint16_t mpeg1[] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
  constexpr uint16_t mpeg2[] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160};
  out.rate = rates[frequency] / (version == 3 ? 1 : version == 2 ? 2 : 4);
  const unsigned bitrate = version == 3 ? mpeg1[index] : mpeg2[index];
  out.bytes = (version == 3 ? 144000u : 72000u) * bitrate / out.rate + ((p[2] >> 1) & 1);
  out.channels = (p[3] & 0xc0) == 0xc0 ? 1 : 2;
  return out.bytes >= 24 && out.bytes <= 1441;
}

void Mp3Framer::reset(uint8_t* buffer) {
  buffer_ = buffer;
  used_ = tagLeft_ = skipped_ = 0;
  initial_ = true;
}
void Mp3Framer::discard(size_t count) {
  used_ -= count;
  if (used_) std::memmove(buffer_, buffer_ + count, used_);
}
bool Mp3Framer::consume(ByteSink sink, void* context) {
  while (used_) {
    if (tagLeft_) {
      const auto count = std::min(used_, static_cast<size_t>(tagLeft_));
      tagLeft_ -= count;
      discard(count);
      continue;
    }
    if (initial_) {
      if (used_ < 10) return true;
      initial_ = false;
      if (!std::memcmp(buffer_, "ID3", 3)) {
        if (buffer_[3] < 2 || buffer_[3] > 4 || (buffer_[6] | buffer_[7] | buffer_[8] | buffer_[9]) & 0x80)
          return false;
        tagLeft_ =
            (uint32_t{buffer_[6]} << 21) | (uint32_t{buffer_[7]} << 14) | (uint32_t{buffer_[8]} << 7) | buffer_[9];
        if (buffer_[3] == 4 && (buffer_[5] & 0x10)) tagLeft_ += 10;
        if (tagLeft_ > 65536) return false;
        discard(10);
        continue;
      }
      if (buffer_[0] == '<' || !std::memcmp(buffer_, "#EXTM3U", 7) || !std::memcmp(buffer_, "[playlist]", 10))
        return false;
    }
    if (used_ < 4) return true;
    FrameHeader header;
    if (!mp3Header(buffer_, header)) {
      // A live connection can begin inside a frame. Do not diagnose AAC from
      // two sync-looking payload bytes; MIME rejects known AAC and this bounded
      // search rejects an unknown stream that never supplies a Layer III frame.
      size_t count = 1;
      while (count + 3 < used_ && buffer_[count] != 255) ++count;
      skipped_ += count;
      if (skipped_ > BUFFER_BYTES) return false;
      discard(count);
      continue;
    }
    if (used_ < header.bytes) return true;
    if (!sink(context, buffer_, header.bytes)) return false;
    skipped_ = 0;
    discard(header.bytes);
  }
  return true;
}
bool Mp3Framer::feed(const uint8_t* bytes, size_t size, ByteSink sink, void* context) {
  if (!buffer_ || (!bytes && size) || !sink) return false;
  while (size) {
    const auto count = std::min(size, BUFFER_BYTES - used_);
    if (!count) return false;
    std::memcpy(buffer_ + used_, bytes, count);
    used_ += count;
    bytes += count;
    size -= count;
    if (!consume(sink, context)) return false;
  }
  return true;
}
}  // namespace radio
