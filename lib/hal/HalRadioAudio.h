#pragma once

#include <cstddef>
#include <cstdint>

namespace HalRadioAudio {
// Currently the Mono single-pin output only; no fallback to another device.
bool supports();

// One worker owns begin/write/end. Fixed 24 kHz, signed 16-bit mono PCM.
// No task or PCM source buffer is allocated here. Volume is clamped to 0..100.
// Volume maps directly to source PCM amplitude: 100 is unity, 0 is mute.
// Acoustic loudness depends on the source and hardware, not just this value.
bool begin(uint8_t volume);
// 1..1024 samples per call; <=100 ms DMA-write budget, each wait <=20 ms.
// On false the producer must stop and call end(), including on invalid input.
bool write(const int16_t* samples, size_t count);
// May be called by the foreground while the worker is playing.
void setVolume(uint8_t volume);
// Same worker as begin/write. <=40 ms fade/drain budget, then driver cleanup.
// Short cues/shutdown never preempt a stream; its producer must release it.
void end();
}  // namespace HalRadioAudio
