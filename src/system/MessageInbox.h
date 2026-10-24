#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace systemMessages {
enum class Source : uint8_t { Radio, Wifi, EspNow, Application };
enum class NoticeKind : uint8_t { Message, FriendRequest };
struct Notice {
  Source source = Source::Radio;
  NoticeKind kind = NoticeKind::Message;
  char peer[77]{};
  char preview[193]{};
  uint16_t unread = 0;
};
// Pure bounded model. The runtime owner serializes access; no message history
// or transport objects live here. History belongs to the app's SD store.
class Inbox {
 public:
  static constexpr size_t CAPACITY = 8;
  Notice items[CAPACITY]{};
  uint8_t count = 0;
  uint32_t revision = 0;
  void post(Source source, const char* peer, const char* preview, NoticeKind kind = NoticeKind::Message) {
    if (std::strlen(peer) >= sizeof(Notice::peer) || std::strlen(preview) >= sizeof(Notice::preview)) return;
    size_t index = 0;
    while (index < count && (items[index].source != source || std::strcmp(items[index].peer, peer))) ++index;
    if (index == count) {
      if (count == CAPACITY) {
        std::memmove(items, items + 1, (CAPACITY - 1) * sizeof(Notice));
        index = CAPACITY - 1;
      } else
        ++count;
      items[index] = {};
      items[index].source = source;
      std::strcpy(items[index].peer, peer);
    }
    auto& item = items[index];
    item.kind = kind;
    std::strcpy(item.preview, preview);
    if (item.unread < UINT16_MAX) ++item.unread;
    ++revision;
  }
  void read(Source source, const char* peer) {
    for (size_t i = 0; i < count; ++i) {
      if (items[i].source == source && !std::strcmp(items[i].peer, peer)) {
        std::memmove(items + i, items + i + 1, (count - i - 1) * sizeof(Notice));
        --count;
        ++revision;
        return;
      }
    }
  }
  uint32_t unread() const {
    uint32_t total = 0;
    for (size_t i = 0; i < count; ++i) total += items[i].unread;
    return total;
  }
};
}  // namespace systemMessages
