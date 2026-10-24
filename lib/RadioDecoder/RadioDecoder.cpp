#if defined(CROSSMUX_RADIO) && CROSSMUX_RADIO
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#define MINIMP3_IMPLEMENTATION
#include "RadioDecoder.h"

#include <cstring>

namespace radio {
void Mp3Decoder::reset() {
  std::memset(&decoder_, 0, sizeof(decoder_));
  mp3dec_init(&decoder_);
}
int Mp3Decoder::decode(const uint8_t* bytes, size_t length, int16_t* pcm, mp3dec_frame_info_t& info) {
  info = {};
  if (!bytes || !pcm || length < 4 || length > 2304) return 0;
  return mp3dec_decode_frame(&decoder_, bytes, static_cast<int>(length), pcm, &info);
}
}  // namespace radio
#endif
