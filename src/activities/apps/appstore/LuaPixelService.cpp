#include <HalSystem.h>
#include <Logging.h>
#include <Memory.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include <cstdio>
#include <cstring>
#include <functional>

#include "LuaAppActivity.h"
#include "activities/apps/pixel-switch/PixelSwitchState.h"

namespace {
// Preserve the original public protocol and rate limit across app re-entry.
pixel_switch::PixelSwitchRateLimiter limiter;
constexpr uint32_t kRetryMs = 5000, kSnapshotMs = 1500;
}  // namespace

class LuaPixelService final : public crossstore::NativeService {
 public:
  explicit LuaPixelService(LuaAppActivity& owner) : owner_(owner), mqtt_(socket_) {}
  ~LuaPixelService() override {
    mqtt_.disconnect();
    socket_.stop();
  }
  bool command(const char* name, const char* argument, int64_t index, crossstore::ServiceReply& reply) override {
    if (!std::strcmp(name, "pixel.start")) {
      state_ = State::Connecting;
      started_ = millis();
      retryAt_ = millis() - kRetryMs;
      if (WiFi.status() != WL_CONNECTED) {
        owner_.systemWifiOwned_ = owner_.systemWifiOwned_ || WiFi.getMode() == WIFI_OFF;
        owner_.wifiPending_ = true;
      }
      return true;
    }
    if (!std::strcmp(name, "pixel.status")) {
      constexpr const char* names[] = {"connecting", "waiting", "online", "paused", "error"};
      std::strcpy(reply.text, names[static_cast<unsigned>(state_)]);
      reply.a = revision_;
      reply.b = pending_.size();
      reply.c = limiter.retryAfterMs(millis(), pending_.size());
      return true;
    }
    if (!std::strcmp(name, "pixel.row")) {
      if (index < 0 || index >= pixel_switch::DISPLAY_HEIGHT) return false;
      constexpr char shades[] = "FA50";
      for (int x = 0; x < pixel_switch::DISPLAY_WIDTH; ++x) {
        const auto point = pixel_switch::displayToCanvas(x, static_cast<int>(index));
        reply.text[x] = shades[static_cast<unsigned>(canvas_.shadeAt(point.x, point.y))];
      }
      reply.text[pixel_switch::DISPLAY_WIDTH] = 0;
      return true;
    }
    if (!std::strcmp(name, "pixel.place")) {
      if (state_ != State::Online || !mqtt_.connected() || index < 0 || index >= 6000 || std::strlen(argument) != 1 ||
          argument[0] < '0' || argument[0] > '3')
        return false;
      const auto point = pixel_switch::displayToCanvas(index % 60, index / 60);
      switch (pending_.place(canvas_, limiter, point.x, point.y, static_cast<pixel_switch::Shade>(argument[0] - '0'),
                             millis())) {
        case pixel_switch::PlacementResult::RateLimited:
          std::strcpy(reply.text, "rate_limited");
          return false;
        case pixel_switch::PlacementResult::Unchanged:
          return true;
        case pixel_switch::PlacementResult::Changed:
          placedAt_ = millis();
          ++revision_;
          return true;
      }
    }
    return false;
  }
  void network(bool connected) override {
    if (!connected) state_ = State::Paused;
  }
  void pump() override {
    if (state_ == State::Paused || state_ == State::Error) return;
    const uint32_t now = millis();
    if (WiFi.status() != WL_CONNECTED || !mqtt_.connected()) {
      if (state_ != State::Connecting) {
        pending_.rollback(canvas_);
        state_ = State::Connecting;
        started_ = now;
        ++revision_;
      }
      if (pixel_switch::reconnectWindowExpired(started_, now)) {
        state_ = State::Paused;
        return;
      }
      if (WiFi.status() != WL_CONNECTED || now - retryAt_ < kRetryMs) return;
      retryAt_ = now;
      if (!prepared_) {
        // PubSubClient owns this one 1600-byte buffer until service destruction.
        if (!mqtt_.setBufferSize(1600)) {
          LOG_ERR("STORE", "OOM allocating pixel MQTT buffer (1600 bytes)");
          state_ = State::Error;
          return;
        }
        mqtt_.setServer("mqtt-cn.uipcat.com", 1883);
        mqtt_.setSocketTimeout(2);
        mqtt_.setCallback([this](char* topic, uint8_t* bytes, unsigned length) {
          if (!pixel_switch::importCanvasMessage(canvas_, topic, bytes, length)) return;
          pending_.rebase(canvas_);
          state_ = State::Online;
          ++revision_;
        });
        prepared_ = true;
      }
      HalSystem::DeviceId id{};
      if (!HalSystem::getDeviceId(id)) {
        state_ = State::Error;
        return;
      }
      char client[40];
      std::snprintf(client, sizeof(client), "crossstore-%02x%02x%02x%02x%02x%02x", id[0], id[1], id[2], id[3], id[4],
                    id[5]);
      if (!mqtt_.connect(client)) return;
      if (!mqtt_.subscribe(pixel_switch::MQTT_TOPIC)) {
        mqtt_.disconnect();
        return;
      }
      state_ = State::Waiting;
      subscribedAt_ = now;
    }
    mqtt_.loop();
    if (state_ == State::Waiting && now - subscribedAt_ >= kSnapshotMs) state_ = State::Online;
    if (state_ != State::Online || pending_.empty() || now - placedAt_ < pixel_switch::PUBLISH_DEBOUNCE_MS) return;
    placedAt_ = now;
    switch (pending_.flush(canvas_, limiter, now, [this](const pixel_switch::PixelSwitchState::Bytes& bytes) {
      return mqtt_.publish(pixel_switch::MQTT_TOPIC, bytes.data(), bytes.size(), true);
    })) {
      case pixel_switch::FlushResult::NothingPending:
        break;
      case pixel_switch::FlushResult::BlankRejected:
      case pixel_switch::FlushResult::Published:
        ++revision_;
        break;
      case pixel_switch::FlushResult::PublishFailed:
        mqtt_.disconnect();
        break;
    }
  }

 private:
  enum class State : uint8_t { Connecting, Waiting, Online, Paused, Error };
  LuaAppActivity& owner_;
  pixel_switch::PixelSwitchState canvas_;
  pixel_switch::PixelSwitchPendingBatch pending_;
  WiFiClient socket_;
  PubSubClient mqtt_;
  State state_ = State::Paused;
  uint32_t started_ = 0, retryAt_ = 0, subscribedAt_ = 0, placedAt_ = 0, revision_ = 1;
  bool prepared_ = false;
};

std::unique_ptr<crossstore::NativeService> crossstore::makePixelService(LuaAppActivity& owner) {
  // Activity-lifetime state (~2 KiB + MQTT buffer); too large for the main-loop
  // stack, allocated only for this app and released before its WiFi ownership.
  auto service = makeUniqueNoThrow<LuaPixelService>(std::ref(owner));
  if (!service)
    LOG_ERR("STORE", "OOM allocating pixel service (%u bytes)", static_cast<unsigned>(sizeof(LuaPixelService)));
  return service;
}
