#pragma once
#include "ChatBuildConfig.h"
#include "MessageInbox.h"
class GfxRenderer;
struct Rect;
namespace systemMessages {
void post(Source source, const char* peer, const char* preview, NoticeKind kind = NoticeKind::Message);
void read(Source source, const char* peer);
void copy(Inbox& out);
uint32_t revision();
uint32_t unread();
// RenderLock owns badge geometry and visibility, matching global input routing.
void beginFrame(bool allowed);
void drawBadge(const GfxRenderer& renderer, const Rect& band);
bool hit(int x, int y);
void clearHit();
}  // namespace systemMessages
