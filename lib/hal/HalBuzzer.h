#pragma once

namespace HalBuzzer {
bool present();
bool supportsPcm();
// Fixed short cues; PCM is at most 200 ms, driver deadline 400 ms.
bool play(const char* cue);
// Does not interrupt a radio stream. Its worker must call HalRadioAudio::end().
void shutdown();
}  // namespace HalBuzzer
