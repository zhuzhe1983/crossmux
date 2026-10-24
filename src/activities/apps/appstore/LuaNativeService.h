#pragma once

#include <CrossStoreRuntime.h>

#include <memory>

class LuaAppActivity;
class GfxRenderer;
namespace crossstore {
// At most one foreground protocol adapter per activity. Lua owns the UI;
// blocking network steps run between callbacks, never inside their budget.
class NativeService {
 public:
  virtual ~NativeService() = default;
  virtual bool command(const char*, const char*, int64_t, ServiceReply&) = 0;
  virtual void pump() = 0;
  virtual void network(bool) {}
  virtual bool imageScreen() const { return false; }
  virtual void renderImage(GfxRenderer&) {}
  // Optional cached canvas images; no Lua, storage or allocation during paint.
  virtual void clearCanvas() {}
  virtual bool beginCanvasFrame(GfxRenderer&) { return false; }
  virtual void displayCanvasFrame(GfxRenderer&) {}
};
std::unique_ptr<NativeService> makePixelService(LuaAppActivity&);
std::unique_ptr<NativeService> makeAirPageService(LuaAppActivity&);
std::unique_ptr<NativeService> makeWeReadService(LuaAppActivity&);
std::unique_ptr<NativeService> makeRadioService(LuaAppActivity&);
}  // namespace crossstore
