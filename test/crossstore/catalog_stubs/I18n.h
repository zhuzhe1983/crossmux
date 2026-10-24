#pragma once
#include <cstdint>
enum class Language { EN, ZH_CN };
enum class StrId : uint16_t {
  STR_PERMISSION_KEEP_AWAKE,
  STR_PERMISSION_FILES_EXPORT,
  STR_PERMISSION_DEVICE_INFO,
  STR_PERMISSION_SOUND,
  STR_PERMISSION_NETWORK,
  STR_PERMISSION_RADIO,
  STR_PERMISSION_NOTIFICATIONS,
  STR_PERMISSION_BACKGROUND,
  STR_PERMISSION_READING_DATA,
  STR_PERMISSION_WALLPAPER
};
struct FakeI18n {
  Language language = Language::EN;
  Language getLanguage() const { return language; }
};
inline FakeI18n fakeI18n;
#define I18N fakeI18n
