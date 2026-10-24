#pragma once
#include <Arduino.h>

#include <cassert>
#include <vector>
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_FAIL = -1, I2S_NUM_AUTO = -1, I2S_ROLE_MASTER = 0;
constexpr int I2S_GPIO_UNUSED = -1, I2S_DATA_BIT_WIDTH_16BIT = 16, I2S_SLOT_MODE_STEREO = 2;
using gpio_num_t = int;
struct Channel {
  bool running = false;
};
using i2s_chan_handle_t = Channel*;
struct i2s_chan_config_t {
  unsigned dma_desc_num, dma_frame_num;
  bool auto_clear;
};
#define I2S_CHANNEL_DEFAULT_CONFIG(a, b) \
  i2s_chan_config_t { 0, 0, false }
struct Clock {
  unsigned rate;
};
struct Slot {
  int width, mode;
};
struct Pins {
  int mclk, bclk, ws, dout, din;
};
struct i2s_std_config_t {
  Clock clk_cfg;
  Slot slot_cfg;
  Pins gpio_cfg;
};
#define I2S_STD_CLK_DEFAULT_CONFIG(rate) \
  Clock { rate }
#define I2S_STD_MSB_SLOT_DEFAULT_CONFIG(width, mode) \
  Slot { width, mode }
inline Channel channel;
inline unsigned allocated = 0, allocations = 0, writes = 0;
inline unsigned writeDelay = 3;
inline bool failNew = false, failInit = false, failEnable = false, failWrite = false, shortWrite = false;
inline bool failDisable = false, failDelete = false;
inline void (*writeHook)() = nullptr;
inline std::vector<uint16_t> stream;
inline int i2s_new_channel(const i2s_chan_config_t* c, i2s_chan_handle_t* out, void*) {
  assert(!allocated && c->dma_desc_num == 3 && c->dma_frame_num == 64 && c->auto_clear);
  if (failNew) return ESP_FAIL;
  ++allocated;
  ++allocations;
  *out = &channel;
  return ESP_OK;
}
inline int i2s_channel_init_std_mode(i2s_chan_handle_t, const i2s_std_config_t* c) {
  assert(c->clk_cfg.rate == 24000 && c->slot_cfg.width == 16 && c->slot_cfg.mode == 2);
  assert(c->gpio_cfg.dout == 42 && c->gpio_cfg.bclk == -1 && c->gpio_cfg.ws == -1);
  return failInit ? ESP_FAIL : ESP_OK;
}
inline int i2s_channel_enable(i2s_chan_handle_t c) {
  assert(!c->running);
  if (failEnable) return ESP_FAIL;
  c->running = true;
  return ESP_OK;
}
inline int i2s_channel_disable(i2s_chan_handle_t c) {
  assert(c->running);
  if (failDisable) return ESP_FAIL;
  c->running = false;
  return ESP_OK;
}
inline int i2s_del_channel(i2s_chan_handle_t c) {
  assert(allocated && !c->running);
  if (failDelete) return ESP_FAIL;
  --allocated;
  return ESP_OK;
}
inline int i2s_channel_write(i2s_chan_handle_t c, const void* input, size_t bytes, size_t* written, uint32_t timeout) {
  assert(c->running && timeout > 0 && timeout <= 40 && bytes <= 256);
  ++writes;
  if (writeHook) writeHook();
  fakeMillis += writeDelay < timeout ? writeDelay : timeout;
  if (writeDelay > timeout) {
    *written = 0;
    return ESP_FAIL;
  }
  if (failWrite) {
    *written = 0;
    return ESP_FAIL;
  }
  const auto* samples = static_cast<const uint16_t*>(input);
  stream.insert(stream.end(), samples, samples + bytes / 2);
  *written = shortWrite ? bytes / 2 : bytes;
  return ESP_OK;
}
