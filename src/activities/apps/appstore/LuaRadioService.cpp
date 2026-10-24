#include "LuaNativeService.h"

#if CROSSMUX_RADIO
#include <HalMemory.h>
#include <HalRadioAudio.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <cstdio>
#include <cstring>

#include "LuaAppActivity.h"
#include "NetworkStartup.h"
#include "services/radio/Player.h"

namespace {
const char* stateName(const radio::State state) {
  switch (state) {
    case radio::State::Idle:
      return "idle";
    case radio::State::Connecting:
      return "connecting";
    case radio::State::Buffering:
      return "buffering";
    case radio::State::Playing:
      return "playing";
    case radio::State::Error:
      return "error";
  }
  return "error";
}
const char* errorName(const radio::Error error) {
  switch (error) {
    case radio::Error::None:
      return "none";
    case radio::Error::InvalidUrl:
      return "invalid_url";
    case radio::Error::Unsupported:
      return "unsupported";
    case radio::Error::Network:
      return "network";
    case radio::Error::Timeout:
      return "timeout";
    case radio::Error::NoMemory:
      return "no_memory";
    case radio::Error::Audio:
      return "audio";
  }
  return "network";
}
bool streamUrl(const char* url) {
  const size_t length = std::strlen(url);
  if (length < 10 || length > 511 || (std::strncmp(url, "http://", 7) && std::strncmp(url, "https://", 8)))
    return false;
  for (size_t i = 0; i < length; ++i)
    if (static_cast<unsigned char>(url[i]) <= 32 || url[i] == '\\' || url[i] == '@') return false;
  return true;
}
}  // namespace

// Only commands/copies run under the Lua callback budget. Worker creation,
// Wi-Fi UI and join run in pump(), outside the render lock. The destructor
// joins without acquiring RenderLock, including ActivityManager's sleep path.
class LuaRadioService final : public crossstore::NativeService {
 public:
  explicit LuaRadioService(LuaAppActivity& owner) : owner_(owner) {}
  ~LuaRadioService() override {
    player_.stopAndJoin();
    owner_.keepAwake_ = false;
  }
  bool command(const char* command, const char* argument, int64_t index, crossstore::ServiceReply& reply) override {
    if (!std::strcmp(command, "radio.start")) return true;
    if (!std::strcmp(command, "radio.play")) {
      if (!streamUrl(argument)) return false;
      std::strcpy(url_, argument);
      request_ = Request::Play;
      offline_ = false;
      ++revision_;
      return true;
    }
    if (!std::strcmp(command, "radio.stop")) {
      request_ = Request::Stop;
      return true;
    }
    if (!std::strcmp(command, "radio.volume")) {
      if (index < 0 || index > 100) return false;
      volume_ = static_cast<uint8_t>(index);
      player_.setVolume(volume_);
      ++revision_;
      return true;
    }
    if (!std::strcmp(command, "radio.status")) {
      const auto state = player_.snapshot();
      const char* label = offline_ ? "error" : request_ == Request::Play ? "connecting" : stateName(state.state);
      std::snprintf(reply.text, sizeof(reply.text), "%s", label);
      std::snprintf(reply.extra, sizeof(reply.extra), "%s", offline_ ? "offline" : errorName(state.error));
      reply.a = volume_;
      reply.b = state.bytes;
      reply.c = revision_;
      return true;
    }
    return false;
  }
  void network(bool connected) override {
    waitingWifi_ = false;
    if (!connected) {
      request_ = Request::Stop;
      offline_ = true;
      ++revision_;
    }
  }
  void pump() override {
    switch (request_) {
      case Request::None:
        break;
      case Request::Stop:
        player_.stopAndJoin();
        request_ = Request::None;
        break;
      case Request::Play:
        if (waitingWifi_) return;
        // Stop the old station before Wi-Fi selection or starting a new one.
        player_.stopAndJoin();
        if (WiFi.status() != WL_CONNECTED) {
          owner_.systemWifiOwned_ = owner_.systemWifiOwned_ || WiFi.getMode() == WIFI_OFF;
          owner_.wifiPending_ = true;
          waitingWifi_ = true;
          return;
        }
        NetworkStartup::prepare(owner_.renderer);
        player_.setVolume(volume_);
        player_.start(url_);
        request_ = Request::None;
        break;
    }
    const auto state = player_.snapshot();
    if (lastState_ != state.state || lastError_ != state.error) {
      lastState_ = state.state;
      lastError_ = state.error;
      ++revision_;
    }
    const bool playing = state.state == radio::State::Connecting || state.state == radio::State::Buffering ||
                         state.state == radio::State::Playing;
    owner_.keepAwake_ = playing && owner_.allowed("keep_awake");
  }

 private:
  enum class Request : uint8_t { None, Play, Stop };
  LuaAppActivity& owner_;
  radio::Player player_;
  char url_[512]{};
  Request request_ = Request::None;
  radio::State lastState_ = radio::State::Idle;
  radio::Error lastError_ = radio::Error::None;
  uint32_t revision_ = 1;
  uint8_t volume_ = radio::DEFAULT_VOLUME;
  bool waitingWifi_ = false, offline_ = false;
};

std::unique_ptr<crossstore::NativeService> crossstore::makeRadioService(LuaAppActivity& owner) {
  if (!HalRadioAudio::supports() || !HalMemory::getPsramHeap().totalBytes) return {};
  // One bounded service (~1 KiB + Player state) outlives callbacks; decoder
  // buffers are separately fallible PSRAM allocations owned by the worker.
  auto service = makeUniqueNoThrow<LuaRadioService>(owner);
  if (!service)
    LOG_ERR("RADIO", "OOM allocating Lua radio service (%u)", static_cast<unsigned>(sizeof(LuaRadioService)));
  return service;
}
#else
std::unique_ptr<crossstore::NativeService> crossstore::makeRadioService(LuaAppActivity&) { return {}; }
#endif
