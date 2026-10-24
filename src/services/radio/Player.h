#pragma once
#include <cstdint>
#include <memory>

namespace radio {
constexpr uint8_t DEFAULT_VOLUME = 50;
enum class State : uint8_t { Idle, Connecting, Buffering, Playing, Error };
enum class Error : uint8_t { None, InvalidUrl, Unsupported, Network, Timeout, NoMemory, Audio };
struct Snapshot {
  State state = State::Idle;
  Error error = Error::None;
  uint32_t bytes = 0, frames = 0;
  uint32_t bufferedMs = 0, underruns = 0;
};

// Foreground/activity-owned. Call stopAndJoin before exit, sleep or destruction;
// it cancels bounded network/audio waits and joins before releasing any memory.
// Worker callbacks never enter Lua, render, or use an Activity pointer.
class Player {
 public:
  Player();
  ~Player();
  Player(const Player&) = delete;
  Player& operator=(const Player&) = delete;
  bool start(const char* url);
  void stopAndJoin();
  Snapshot snapshot() const;
  void setVolume(uint8_t value);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
  Error failure_ = Error::None;
  uint8_t volume_ = DEFAULT_VOLUME;
};
}  // namespace radio
