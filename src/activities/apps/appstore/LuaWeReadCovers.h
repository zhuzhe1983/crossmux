#pragma once

#include <Bitmap.h>
#include <HalMemory.h>

#include <array>
#include <cstddef>
#include <cstdint>

class GfxRenderer;

// One visible WeRead page, prepared on the activity thread under RenderLock.
// Only authoritative bookshelf indices are stored; Lua cannot supply a path.
class LuaWeReadCovers {
 public:
  static constexpr size_t CAPACITY = 12;
  static constexpr int MAX_WIDTH = 112, MAX_HEIGHT = 164;
  static constexpr size_t MAX_BYTES = MAX_WIDTH * MAX_HEIGHT / 2;
  enum class State : uint8_t { Empty, Missing, Ready, Invalid, NoMemory, Unavailable };
  static bool parsePlacement(const char* argument, int (&values)[4]);
  ~LuaWeReadCovers() { file_.close(); }
  void clear();
  void clearPlacements() { count_ = 0; }
  bool place(uint32_t index, int x, int y, int width, int height);
  // Retire old slots only between Lua callbacks, never during rendering.
  void prepare();
  size_t count() const { return count_; }
  uint32_t index(size_t slot) const { return slot < count_ ? slots_[slot].requested : 0; }
  State state(size_t slot) const { return slot < count_ ? slots_[slot].state : State::Unavailable; }
  void unavailable(size_t slot);
  State load(size_t slot, const char* path);
  bool beginFrame(GfxRenderer& renderer);
  void displayFrame(GfxRenderer& renderer);

 private:
  struct Slot {
    HalMemory::PsramBuffer pixels;
    uint32_t requested = 0, loaded = 0;
    int16_t x = 0, y = 0, width = 0, height = 0;
    uint8_t sourceWidth = 0, sourceHeight = 0;
    State state = State::Empty;
  };
  enum class Output : uint8_t { Monochrome, Overlay, Native };
  std::array<Slot, CAPACITY> slots_{};
  size_t count_ = 0;
  Output output_ = Output::Monochrome;
  // Reusable parsing workspace, not a per-cover allocation or task-stack array.
  HalFile file_;
  Bitmap bitmap_{file_};
  uint8_t row_[MAX_WIDTH * 4]{}, gray_[MAX_WIDTH]{};
  void paint(const GfxRenderer& renderer, int levels) const;
};
