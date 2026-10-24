#pragma once
#include <cstddef>
#include <cstdint>

#include "vendor/minimp3.h"

namespace radio {
// Caller keeps this bounded state and the 2304-sample PCM buffer in PSRAM.
// Upstream frame scratch remains on the decoder worker's measured stack.
class Mp3Decoder {
 public:
  static constexpr size_t MAX_PCM_SAMPLES = MINIMP3_MAX_SAMPLES_PER_FRAME;
  void reset();
  int decode(const uint8_t* bytes, size_t length, int16_t* pcm, mp3dec_frame_info_t& info);

 private:
  mp3dec_t decoder_{};
};
}  // namespace radio
