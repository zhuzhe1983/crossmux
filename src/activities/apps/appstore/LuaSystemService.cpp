#include <CrossStoreSvg.h>
#include <HalClock.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <I18n.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "AchievementsStore.h"
#include "LuaAppActivity.h"
#include "LuaReadingQueries.h"
#include "ReadingStatsStore.h"
#include "crossstore/RecoverableFile.h"
#include "util/TimeUtils.h"

namespace {
// Fixed caller buffers avoid the allocating date/title helpers on query paths.
void dateLabel(uint32_t ordinal, char* out, size_t capacity) {
  if (!ordinal) return;
  int year = 0;
  unsigned month = 0, day = 0;
  TimeUtils::getDateFromDayOrdinal(ordinal, year, month, day);
  std::snprintf(out, capacity, "%04d-%02u-%02u", year, month, day);
}

}  // namespace

bool LuaAppActivity::system(const char* command, const char* argument, int64_t index, crossstore::ServiceReply& reply) {
  if (!std::strncmp(command, "radio.", 6)) {
    if (!allowed("network") || !allowed("sound")) return false;
    if (!nativeService_ && !std::strcmp(command, "radio.start")) nativeService_ = crossstore::makeRadioService(*this);
    return nativeService_ && nativeService_->command(command, argument, index, reply);
  }
  if (!std::strncmp(command, "weread.", 7)) {
    // Cached shelf/details/covers remain available offline. The service gates
    // network jobs and automatic cover requests separately.
    if (!allowed("reading_data")) return false;
    if (!nativeService_ && !std::strcmp(command, "weread.start")) nativeService_ = crossstore::makeWeReadService(*this);
    return nativeService_ && nativeService_->command(command, argument, index, reply);
  }
  if (!std::strncmp(command, "airpage.", 8)) {
    if (!allowed("network") || !allowed("device_info")) return false;
    if (!nativeService_ && !std::strcmp(command, "airpage.start"))
      nativeService_ = crossstore::makeAirPageService(*this);
    return nativeService_ && nativeService_->command(command, argument, index, reply);
  }
  if (!std::strncmp(command, "pixel.", 6)) {
    if (!allowed("network")) return false;
    if (!nativeService_ && !std::strcmp(command, "pixel.start")) nativeService_ = crossstore::makePixelService(*this);
    return nativeService_ && nativeService_->command(command, argument, index, reply);
  }
  if (!std::strcmp(command, "canvas.export")) {
    if (!allowed("files_export") || !crossstore::validId(argument) || !canvasMode_ || !canvas_) return false;
    std::snprintf(pendingExport_, sizeof(pendingExport_), "%s", argument);
    return true;
  }
  if (!std::strcmp(command, "power.keep_awake")) {
    if (std::strcmp(argument, "on") && std::strcmp(argument, "off")) return false;
    const bool enable = !std::strcmp(argument, "on");
    if (enable && !allowed("keep_awake")) return false;
    keepAwake_ = enable;
    return true;
  }
  if (!std::strcmp(command, "device.sync")) {
    if (!allowed("network")) return false;
    if (WiFi.status() != WL_CONNECTED) {
      systemWifiOwned_ = systemWifiOwned_ || WiFi.getMode() == WIFI_OFF;
      wifiPending_ = true;
      std::strcpy(reply.text, "connecting");
      return false;
    }
    clockSyncPending_ = halClock.requestSync();
    clockSyncStarted_ = millis();
    if (!clockSyncPending_) releaseSystemWifi();
    return clockSyncPending_;
  }
  if (!std::strcmp(command, "device.sync_status")) {
    if (!allowed("network") || !clockSyncPending_) return false;
    switch (halClock.syncState()) {
      case ClockSyncState::Idle:
        std::strcpy(reply.text, "failed");
        break;
      case ClockSyncState::Syncing:
        if (millis() - clockSyncStarted_ < 15000) return false;
        std::strcpy(reply.text, "failed");
        break;
      case ClockSyncState::Succeeded:
        std::strcpy(reply.text, "success");
        break;
      case ClockSyncState::Failed:
        std::strcpy(reply.text, "failed");
        break;
    }
    releaseSystemWifi();
    clockSyncPending_ = false;
    return true;
  }
  if (!std::strcmp(command, "device.time")) {
    struct tm now{};
    if (!halClock.hasValidTime() || !halClock.localTime(now)) return false;
    std::strftime(reply.text, sizeof(reply.text), "%H:%M", &now);
    std::strftime(reply.extra, sizeof(reply.extra), "%Y-%m-%d", &now);
    reply.a = now.tm_wday;
    reply.b = now.tm_sec;
    reply.c = halClock.nowUtc();
    return true;
  }
  if (!std::strcmp(command, "device.battery")) {
    reply.a = powerManager.getBatteryPercentage();
    return true;
  }
  if (!std::strcmp(command, "device.id")) {
    if (!allowed("device_info")) return false;
    HalSystem::DeviceId id;
    if (!HalSystem::getDeviceId(id)) return false;
    std::snprintf(reply.text, sizeof(reply.text), "%02x%02x%02x%02x%02x%02x", id[0], id[1], id[2], id[3], id[4], id[5]);
    return true;
  }
  if (std::strncmp(command, "reading.", 8) || !allowed("reading_data")) return false;
  if (!std::strcmp(command, "reading.calendar")) {
    uint32_t reference = TimeUtils::getLocalDayOrdinal(READING_STATS.getDisplayTimestamp());
    const auto& days = READING_STATS.getReadingDays();
    if (!reference && !days.empty()) reference = days.back().dayOrdinal;
    int year = 0;
    unsigned month = 0;
    if (*argument) {
      if (!crossstore::reading::parseMonth(argument, year, month)) return false;
    } else {
      if (!reference) return false;
      unsigned day = 0;
      TimeUtils::getDateFromDayOrdinal(reference, year, month, day);
    }
    std::snprintf(reply.text, sizeof(reply.text), "%04d-%02u", year, month);
    reply.a = TimeUtils::getDayOrdinalForDate(year, month, 1);
    reply.b = TimeUtils::getDaysInMonth(year, month);
    reply.c = reference;
    return true;
  }
  if (!std::strcmp(command, "reading.date")) {
    if (index < 0 || index > TimeUtils::getDayOrdinalForDate(2106, 12, 31)) return false;
    if (index == 0)
      std::strcpy(reply.text, "1970-01-01");
    else
      dateLabel(static_cast<uint32_t>(index), reply.text, sizeof(reply.text));
    reply.a = index;
    reply.b = crossstore::reading::millisecondsOnDay(READING_STATS.getReadingDays(), static_cast<uint32_t>(index));
    return true;
  }
  if (!std::strcmp(command, "reading.date_book")) {
    uint32_t day = 0;
    bool reverse = false;
    if (!crossstore::reading::parseDay(argument, TimeUtils::getDayOrdinalForDate(2106, 12, 31), day, reverse))
      return false;
    const auto& books = READING_STATS.getBooks();
    if (index < 1 || static_cast<uint64_t>(index) > books.size()) return false;
    const auto match = crossstore::reading::nextBookOnDay(books, day, static_cast<size_t>(index), reverse);
    reply.a = match.book;
    reply.b = match.milliseconds;
    reply.c = match.next;
    return true;
  }
  if (!std::strcmp(command, "reading.achievements")) {
    reply.c = SETTINGS.achievementsEnabled;
    if (!reply.c) return true;
    reply.a = static_cast<size_t>(AchievementId::_COUNT);
    for (size_t i = 0; i < static_cast<size_t>(AchievementId::_COUNT); ++i)
      if (ACHIEVEMENTS.getState(static_cast<AchievementId>(i)).unlocked) ++reply.b;
    return true;
  }
  if (!std::strcmp(command, "reading.achievement")) {
    if (!SETTINGS.achievementsEnabled || index < 1 || index > static_cast<int64_t>(AchievementId::_COUNT)) return false;
    const auto id = static_cast<AchievementId>(index - 1);
    const auto& definition = ACHIEVEMENTS.getDefinition(id);
    const auto& state = ACHIEVEMENTS.getState(id);
    std::snprintf(reply.text, sizeof(reply.text), "%s", I18n::getInstance().get(definition.titleId));
    dateLabel(TimeUtils::getLocalDayOrdinal(state.unlockedAt), reply.extra, sizeof(reply.extra));
    reply.a = static_cast<uint8_t>(definition.metric);
    reply.b = definition.target;
    reply.c = state.unlocked;
    return true;
  }
  if (!std::strcmp(command, "reading.summary")) {
    reply.a = READING_STATS.getTotalReadingMs();
    reply.b = READING_STATS.getTodayReadingMs();
    reply.c = READING_STATS.getRecentReadingMs(7);
    return true;
  }
  if (!std::strcmp(command, "reading.counts")) {
    reply.a = READING_STATS.getBooksStartedCount();
    reply.b = READING_STATS.getBooksFinishedCount();
    reply.c = READING_STATS.getCurrentStreakDays();
    return true;
  }
  if (!std::strcmp(command, "reading.goal")) {
    reply.a = getDailyReadingGoalMs();
    reply.b = READING_STATS.getMaxStreakDays();
    reply.c = READING_STATS.getReadingDays().size();
    return true;
  }
  if (!std::strcmp(command, "reading.day")) {
    const auto& days = READING_STATS.getReadingDays();
    if (index < 1 || static_cast<size_t>(index) > days.size()) return false;
    const auto& day = days[index - 1];
    reply.a = day.dayOrdinal;
    reply.b = day.readingMs;
    return true;
  }
  if (!std::strcmp(command, "reading.book") || !std::strcmp(command, "reading.open") ||
      !std::strcmp(command, "reading.book_detail")) {
    const auto& books = READING_STATS.getBooks();
    if (index < 1 || static_cast<size_t>(index) > books.size()) return false;
    const auto& book = books[index - 1];
    if (!std::strcmp(command, "reading.book_detail")) {
      char first[12]{}, last[12]{}, completed[12]{};
      dateLabel(TimeUtils::getLocalDayOrdinal(book.firstReadAt), first, sizeof(first));
      dateLabel(TimeUtils::getLocalDayOrdinal(book.lastReadAt), last, sizeof(last));
      dateLabel(TimeUtils::getLocalDayOrdinal(book.completedAt), completed, sizeof(completed));
      std::snprintf(reply.text, sizeof(reply.text), "%s", book.chapterTitle.c_str());
      std::snprintf(reply.extra, sizeof(reply.extra), "%s|%s|%s", first, last, completed);
      reply.a = book.sessions;
      reply.b = book.lastSessionMs;
      reply.c = book.chapterProgressPercent;
      return true;
    }
    if (!std::strcmp(command, "reading.open")) {
      if (book.path.size() >= sizeof(pendingBook_)) return false;
      std::strcpy(pendingBook_, book.path.c_str());
      std::strcpy(pendingRoute_, "reading_book");
      return true;
    }
    std::snprintf(reply.text, sizeof(reply.text), "%s", book.title.empty() ? book.path.c_str() : book.title.c_str());
    std::snprintf(reply.extra, sizeof(reply.extra), "%s", book.author.c_str());
    reply.a = book.totalReadingMs;
    reply.b = book.lastProgressPercent;
    reply.c = book.completed;
    return true;
  }
  (void)argument;
  return false;
}

void LuaAppActivity::exportCanvas() {
  RenderLock renderBarrier;
  char directory[80], path[128], temporary[136];
  std::snprintf(directory, sizeof(directory), "/CrossStore/%s", id_);
  std::snprintf(path, sizeof(path), "%s/%s.svg", directory, pendingExport_);
  std::snprintf(temporary, sizeof(temporary), "%s.part", path);
  pendingExport_[0] = 0;
  bool success = allowed("files_export") && canvas_ && (Storage.exists(directory) || Storage.mkdir(directory));
  if (success && !Storage.exists(path)) {
    auto dir = Storage.open(directory);
    size_t count = 0;
    success = dir && dir.isDirectory();
    while (success) {
      auto file = dir.openNextFile();
      if (!file) break;
      if (++count >= 32) success = false;
    }
  }
  if (success) {
    HalPowerManager::Lock power;
    const auto bounds = canvasBounds();
    {
      auto file = Storage.open(temporary, O_WRONLY | O_CREAT | O_TRUNC);
      success = file &&
                crossstore::writeSvg(*canvas_, bounds.width, bounds.height,
                                     [&](const char* data, size_t bytes) { return file.write(data, bytes) == bytes; });
      if (file) file.flush();
    }
    if (success)
      success = crossstore::commitFile(Storage, temporary, path);
    else
      Storage.remove(temporary);
  }
  renderBarrier.unlock();
  dispatch("on_export", success ? 1 : 0, success ? path : "");
}
