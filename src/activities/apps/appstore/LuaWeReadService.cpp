#include <FontCacheManager.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>
#include <WeReadClient.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <optional>

#include "LuaAppActivity.h"
#include "LuaWeReadCache.h"
#include "LuaWeReadCovers.h"

class LuaWeReadService final : public crossstore::NativeService {
 public:
  explicit LuaWeReadService(LuaAppActivity& owner) : owner_(owner) {}
  ~LuaWeReadService() override {
    operation_.reset();
    shelf_.close();
    cache_.close();
  }
  bool command(const char* name, const char* argument, int64_t index, crossstore::ServiceReply& reply) override {
    if (!std::strcmp(name, "weread.start")) {
      initialize_ = true;
      return true;
    }
    if (!ready_) return false;
    if (!std::strcmp(name, "weread.status")) {
      constexpr const char* names[] = {"idle", "disclaimer", "connecting", "busy", "qr", "range", "done", "error"};
      std::strcpy(reply.text, names[static_cast<unsigned>(state_)]);
      reply.a = count_;
      reply.b = revision_;
      reply.c = static_cast<int>(error_);
      return true;
    }
    if (!std::strcmp(name, "weread.accept")) {
      if (!WeReadStore::acceptDisclaimer()) return false;
      accepted_ = true;
      setState(State::Idle);
      return true;
    }
    if (!accepted_) return false;
    if (job_ == Job::Cover && (!std::strcmp(name, "weread.detail") || !std::strcmp(name, "weread.cache") ||
                               !std::strcmp(name, "weread.open"))) {
      operation_.reset();
      job_ = Job::None;
    }
    if (!std::strcmp(name, "weread.cancel")) {
      operation_.cancel();
      coverStopped_ = true;
      pending_ = Job::None;
      setState(State::Idle);
      return true;
    }
    if (!std::strcmp(name, "weread.book")) {
      if (!readBook(index)) return false;
      std::snprintf(reply.text, sizeof(reply.text), "%s", scratch_.title);
      std::snprintf(reply.extra, sizeof(reply.extra), "%s", scratch_.author);
      reply.extra[utf8SafeTruncateBuffer(reply.extra, std::strlen(reply.extra))] = 0;
      reply.a = Storage.exists(WeReadStore::finalBookPath(scratch_).c_str());
      return true;
    }
    if (!std::strcmp(name, "weread.cover")) {
      int geometry[4]{};
      const auto bounds = owner_.canvasBounds();
      if (!owner_.canvasMode_ || index < 1 || static_cast<uint64_t>(index) > count_ ||
          !LuaWeReadCovers::parsePlacement(argument, geometry))
        return false;
      const int x = geometry[0], y = geometry[1], width = geometry[2], height = geometry[3];
      if (x > bounds.width || y > bounds.height || width > bounds.width - x || height > bounds.height - y) return false;
      return covers_.place(index, bounds.x + x, bounds.y + y, width, height);
    }
    if (!std::strcmp(name, "weread.cache")) {
      if (operation_.active() || pending_ != Job::None || !readBook(index) ||
          !std::memchr(scratch_.bookId, 0, sizeof(scratch_.bookId)))
        return false;
      const auto state = cache_.open(scratch_.bookId);
      switch (state) {
        case LuaWeReadCache::State::Missing:
          std::strcpy(reply.text, "missing");
          break;
        case LuaWeReadCache::State::Ready:
          std::strcpy(reply.text, "ready");
          break;
        case LuaWeReadCache::State::Invalid:
          std::strcpy(reply.text, "invalid");
          break;
      }
      WeReadStore::BookOptions saved;
      reply.a = Storage.exists(WeReadStore::finalBookPath(scratch_).c_str());
      reply.b = cache_.count();
      reply.c = WeReadStore::loadBookOptions(WeReadStore::bookDirectory(scratch_.bookId), saved)
                    ? static_cast<int>(saved.imagePolicy)
                    : -1;
      return true;
    }
    if (!std::strcmp(name, "weread.cached_chapter")) {
      bool cached = false;
      if (operation_.active() || pending_ != Job::None || !cache_.read(index, chapter_, cached)) return false;
      std::snprintf(reply.text, sizeof(reply.text), "%s", chapter_.title);
      reply.a = cached;
      reply.b = chapter_.paid;
      reply.c = chapter_.wordCount;
      return true;
    }
    if (!std::strcmp(name, "weread.open")) {
      if (!readBook(index)) return false;
      const auto path = WeReadStore::finalBookPath(scratch_);
      if (path.size() >= sizeof(owner_.pendingBook_) || !Storage.exists(path.c_str())) return false;
      std::strcpy(owner_.pendingBook_, path.c_str());
      std::strcpy(owner_.pendingRoute_, "reading_book");
      return true;
    }
    if (!std::strcmp(name, "weread.progress")) {
      std::snprintf(reply.text, sizeof(reply.text), "%s", operation_.progressTitle());
      reply.a = operation_.progressCompleted();
      reply.b = operation_.progressTotal();
      reply.c = static_cast<unsigned>(operation_.progressStage());
      return true;
    }
    if (!std::strcmp(name, "weread.qr")) {
      const char* url = operation_.qrUrl();
      if (index < 0 || index > 2 || static_cast<size_t>(index) * 192 >= std::strlen(url)) return false;
      std::snprintf(reply.text, sizeof(reply.text), "%s", url + index * 192);
      return true;
    }
    if (!std::strcmp(name, "weread.chapter_count")) {
      reply.a = operation_.chapterCount();
      return true;
    }
    if (!std::strcmp(name, "weread.chapter")) {
      if (index < 0 || index >= operation_.chapterCount() || !operation_.readChapter(index, chapter_)) return false;
      std::snprintf(reply.text, sizeof(reply.text), "%s", chapter_.title);
      reply.a = chapter_.paid;
      return true;
    }
    if (!std::strcmp(name, "weread.range")) {
      uint32_t first = 0, last = 0;
      char tail;
      if (std::sscanf(argument, "%u:%u%c", &first, &last, &tail) != 2 || !operation_.setChapterRange(first, last))
        return false;
      setState(State::Busy);
      return true;
    }
    if (!std::strcmp(name, "weread.detail")) {
      if (!readBook(index)) return false;
      book_ = scratch_;
      HalFile file;
      if (!WeReadStore::openBookDetail(WeReadStore::bookDirectory(scratch_.bookId), detail_, file)) return false;
      std::snprintf(reply.text, sizeof(reply.text), "%s", detail_.title);
      std::snprintf(reply.extra, sizeof(reply.extra), "%s", detail_.publisher);
      reply.extra[utf8SafeTruncateBuffer(reply.extra, std::strlen(reply.extra))] = 0;
      reply.a = detail_.totalWords;
      reply.b = detail_.newRating;
      reply.c = detail_.introLength;
      return true;
    }
    if (!std::strcmp(name, "weread.intro")) {
      if (index < 0) return false;
      HalFile file;
      if (!WeReadStore::openBookDetail(WeReadStore::bookDirectory(book_.bookId), detail_, file) ||
          index >= detail_.introLength || !file.seek(sizeof(detail_) + index))
        return false;
      const size_t available = std::min<size_t>(192, detail_.introLength - index);
      const int bytes = file.read(reply.text, available);
      if (bytes <= 0) return false;
      size_t length = bytes;
      // Leave a partial UTF-8 scalar for the next chunk, never show a broken glyph.
      if (index + bytes < detail_.introLength) {
        size_t start = length - 1;
        while (start > 0 && (static_cast<unsigned char>(reply.text[start]) & 0xC0) == 0x80) --start;
        const auto lead = static_cast<unsigned char>(reply.text[start]);
        const size_t need = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
        if (length - start < need) length = start;
      }
      reply.text[length] = 0;
      reply.a = length;
      reply.b = detail_.introLength;
      return length > 0;
    }
    if (!std::strcmp(name, "weread.logout")) {
      logout_ = true;
      return true;
    }
    if (!std::strcmp(name, "weread.sync")) return queue(Job::Sync, 0);
    if (!std::strcmp(name, "weread.fetch_detail")) return queue(Job::Detail, index);
    if (!std::strcmp(name, "weread.download")) {
      options_.imagePolicy =
          std::strstr(argument, "no_images") ? WeReadStore::ImagePolicy::Exclude : WeReadStore::ImagePolicy::Embed;
      options_.chapterScope = std::strstr(argument, "range") ? WeReadClient::DownloadOptions::ChapterScope::SelectRange
                                                             : WeReadClient::DownloadOptions::ChapterScope::WholeBook;
      return queue(Job::Download, index);
    }
    return false;
  }
  void network(bool connected) override {
    if (!connected) {
      pending_ = Job::None;
      setState(State::Idle);
    }
  }
  void clearCanvas() override { covers_.clearPlacements(); }
  bool beginCanvasFrame(GfxRenderer& renderer) override { return covers_.beginFrame(renderer); }
  void displayCanvasFrame(GfxRenderer& renderer) override { covers_.displayFrame(renderer); }
  void pump() override {
    if (initialize_) {
      initialize_ = false;
      RenderLock lock;
      const bool rootReady = WeReadStore::ensureRoot();
      ready_ = true;
      accepted_ = WeReadStore::hasAcceptedDisclaimer();
      refreshShelf();
      error_ = rootReady ? WeReadClient::Error::Ok : WeReadClient::Error::SdCard;
      setState(!rootReady ? State::Error : accepted_ ? State::Idle : State::Disclaimer);
    }
    if (!ready_) return;
    bool pendingLocalCovers = false;
    {
      RenderLock lock;
      // Operation runs synchronously on this same activity thread. Reset/close
      // its transport before retiring a previous page; there is no worker to join.
      if (job_ == Job::Cover && (coverSlot_ >= covers_.count() || covers_.index(coverSlot_) != coverIndex_)) {
        operation_.reset();
        job_ = Job::None;
        coverStopped_ = false;
      }
      const uint32_t first = covers_.index(0);
      if (first != coverPageFirst_) {
        coverPageFirst_ = first;
        coverStopped_ = false;
      }
      covers_.prepare();
      // One bounded thumbnail per pump keeps SD work out of rendering and Lua.
      for (size_t slot = 0; slot < covers_.count(); ++slot) {
        if (covers_.state(slot) != LuaWeReadCovers::State::Empty) continue;
        loadCover(slot);
        owner_.dirty_ = true;
        break;
      }
      for (size_t slot = 0; slot < covers_.count(); ++slot)
        pendingLocalCovers |= covers_.state(slot) == LuaWeReadCovers::State::Empty;
    }
    if (logout_) {
      logout_ = false;
      RenderLock lock;
      operation_.reset();
      job_ = Job::None;
      pending_ = Job::None;
      shelf_.close();
      cache_.close();
      const bool saved = WeReadStore::clearSession() && WeReadStore::clearShelf();
      refreshShelf();
      error_ = saved ? WeReadClient::Error::Ok : WeReadClient::Error::SdCard;
      setState(saved ? State::Idle : State::Error);
    }
    if (!owner_.allowed("network") && (operation_.active() || pending_ != Job::None)) {
      RenderLock lock;
      operation_.reset();
      pending_ = job_ = Job::None;
      coverStopped_ = true;
      setState(State::Idle);
    }
    if (pending_ != Job::None) {
      if (WiFi.status() != WL_CONNECTED) return;
      RenderLock lock;
      job_ = pending_;
      pending_ = Job::None;
      if (job_ == Job::Sync) shelf_.close();
      WeReadClient::Operation::Kind kind = WeReadClient::Operation::Kind::Sync;
      switch (job_) {
        case Job::None:
          return;
        case Job::Sync:
          break;
        case Job::Detail:
          kind = WeReadClient::Operation::Kind::Detail;
          break;
        case Job::Download:
          kind = WeReadClient::Operation::Kind::Download;
          break;
        case Job::Cover:
          return;
      }
      if (!operation_.begin(kind, job_ == Job::Sync ? nullptr : &book_, options_)) {
        error_ = operation_.error();
        setState(State::Error);
        return;
      }
      setState(State::Busy);
    }
    if (!operation_.active() && pending_ == Job::None && accepted_ && !coverStopped_ && !pendingLocalCovers &&
        (state_ == State::Idle || state_ == State::Done) && owner_.allowed("network") &&
        WiFi.status() == WL_CONNECTED) {
      RenderLock lock;
      for (size_t slot = 0; slot < covers_.count(); ++slot) {
        if (covers_.state(slot) != LuaWeReadCovers::State::Missing) continue;
        if (!readBook(covers_.index(slot)) || !operation_.beginCover(scratch_)) {
          operation_.reset();
          covers_.unavailable(slot);
          continue;
        }
        job_ = Job::Cover;
        coverSlot_ = slot;
        coverIndex_ = covers_.index(slot);
        break;
      }
    }
    if (state_ == State::Range || !operation_.active()) return;
    WeReadClient::Operation::Event event;
    {
      RenderLock lock;
      if (auto* cache = owner_.renderer.getFontCacheManager()) cache->clearCache();
      std::optional<GfxRenderer::FrameBufferLoan> scratch;
      if (operation_.needsCoverConversionScratch()) scratch.emplace(owner_.renderer);
      event = operation_.step(
          nullptr, nullptr,
          [](void* context) {
            auto& self = *static_cast<LuaWeReadService*>(context);
            const auto now = millis();
            if (now - self.cancelPolled_ < 10) return false;
            self.cancelPolled_ = now;
            // A synchronous transport step owns input until it returns to the main loop.
            self.owner_.mappedInput.update(true);
            const bool cancel = self.owner_.mappedInput.wasReleased(MappedInputManager::Button::Back) ||
                                self.owner_.mappedInput.wasHomeGesture();
            self.owner_.nativeInputConsumed_ |= cancel;
            return cancel;
          },
          this);
    }
    if (job_ == Job::Cover) {
      RenderLock lock;
      switch (event) {
        case WeReadClient::Operation::Event::None:
        case WeReadClient::Operation::Event::DetailReady:
          return;
        case WeReadClient::Operation::Event::Complete: {
          loadCover(coverSlot_);
          if (covers_.state(coverSlot_) != LuaWeReadCovers::State::Ready) covers_.unavailable(coverSlot_);
          owner_.dirty_ = true;
          break;
        }
        case WeReadClient::Operation::Event::Failed:
          LOG_ERR("WR", "Visible cover failed: error=%u", static_cast<unsigned>(operation_.error()));
          covers_.unavailable(coverSlot_);
          break;
        case WeReadClient::Operation::Event::QrReady:
        case WeReadClient::Operation::Event::Authenticated:
        case WeReadClient::Operation::Event::ChapterRangeReady:
        case WeReadClient::Operation::Event::ChapterComplete:
        case WeReadClient::Operation::Event::Cancelled:
          coverStopped_ = true;
          break;
      }
      operation_.reset();
      job_ = Job::None;
      return;
    }
    switch (event) {
      case WeReadClient::Operation::Event::None:
        break;
      case WeReadClient::Operation::Event::QrReady:
        setState(State::Qr);
        break;
      case WeReadClient::Operation::Event::Authenticated:
        setState(State::Busy);
        break;
      case WeReadClient::Operation::Event::DetailReady:
        ++revision_;
        break;
      case WeReadClient::Operation::Event::ChapterRangeReady:
        setState(State::Range);
        break;
      case WeReadClient::Operation::Event::ChapterComplete:
        ++revision_;
        break;
      case WeReadClient::Operation::Event::Complete: {
        RenderLock lock;
        if (job_ == Job::Sync) refreshShelf();
        setState(State::Done);
        break;
      }
      case WeReadClient::Operation::Event::Cancelled: {
        RenderLock lock;
        refreshShelf();
        setState(State::Idle);
        break;
      }
      case WeReadClient::Operation::Event::Failed:
        error_ = operation_.error();
        setState(State::Error);
        break;
    }
    if (event != WeReadClient::Operation::Event::None) owner_.dispatch("on_service");
  }

 private:
  enum class State : uint8_t { Idle, Disclaimer, Connecting, Busy, Qr, Range, Done, Error };
  enum class Job : uint8_t { None, Sync, Detail, Download, Cover };
  LuaAppActivity& owner_;
  WeReadClient::Operation operation_;
  LuaWeReadCache cache_;
  LuaWeReadCovers covers_;
  WeReadStore::ShelfRecord book_{}, scratch_{};
  WeReadStore::BookDetailHeader detail_{};
  WeReadStore::TocRecord chapter_{};
  HalFile shelf_;
  WeReadClient::DownloadOptions options_;
  State state_ = State::Idle;
  Job pending_ = Job::None, job_ = Job::None;
  WeReadClient::Error error_ = WeReadClient::Error::Ok;
  bool initialize_ = false, ready_ = false, logout_ = false, accepted_ = false;
  uint32_t count_ = 0, revision_ = 0, cancelPolled_ = 0;
  size_t coverSlot_ = 0;
  uint32_t coverIndex_ = 0, coverPageFirst_ = 0;
  bool coverStopped_ = false;
  void setState(State state) {
    state_ = state;
    ++revision_;
  }
  void refreshShelf() {
    covers_.clear();
    coverStopped_ = false;
    shelf_.close();
    count_ = 0;
    WeReadStore::openShelf(shelf_, count_);
  }
  bool readBook(int64_t index) {
    return index >= 1 && static_cast<uint64_t>(index) <= count_ &&
           WeReadStore::readShelfRecord(shelf_, index - 1, scratch_) && scratch_.bookId[0] &&
           std::memchr(scratch_.bookId, 0, sizeof(scratch_.bookId)) &&
           std::memchr(scratch_.title, 0, sizeof(scratch_.title)) &&
           std::memchr(scratch_.author, 0, sizeof(scratch_.author));
  }
  bool queue(Job job, int64_t index) {
    if (!owner_.allowed("network")) return false;
    if (job_ == Job::Cover) {
      operation_.reset();
      job_ = Job::None;
    }
    if (operation_.active() || pending_ != Job::None) return false;
    if (job != Job::Sync) {
      if (!readBook(index)) return false;
      book_ = scratch_;
    }
    cache_.close();
    pending_ = job;
    error_ = WeReadClient::Error::Ok;
    if (WiFi.status() != WL_CONNECTED) {
      owner_.systemWifiOwned_ = owner_.systemWifiOwned_ || WiFi.getMode() == WIFI_OFF;
      owner_.wifiPending_ = true;
    }
    setState(State::Connecting);
    return true;
  }
  void loadCover(size_t slot) {
    if (!readBook(covers_.index(slot)) || !std::memchr(scratch_.bookId, 0, sizeof(scratch_.bookId))) {
      covers_.unavailable(slot);
      return;
    }
    const auto path = WeReadStore::coverPath(WeReadStore::bookDirectory(scratch_.bookId));
    const auto result = covers_.load(slot, path.c_str());
    if (result == LuaWeReadCovers::State::Invalid) LOG_ERR("WR", "Invalid visible cover cache");
  }
};

std::unique_ptr<crossstore::NativeService> crossstore::makeWeReadService(LuaAppActivity& owner) {
  // One activity-owned protocol operation and reusable SD records (~12 KiB).
  // Keeping records off the 8 KiB task stack also avoids per-query allocations.
  auto service = makeUniqueNoThrow<LuaWeReadService>(std::ref(owner));
  if (!service)
    LOG_ERR("STORE", "OOM allocating WeRead service (%u bytes)", static_cast<unsigned>(sizeof(LuaWeReadService)));
  return service;
}
