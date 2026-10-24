#pragma once

#include <I18n.h>

#include <cstdint>

namespace crossstore {
struct Permission {
  const char* name;
  uint32_t bit;
  StrId title;
};
constexpr Permission PERMISSIONS[] = {
    {"keep_awake", 1u << 9, StrId::STR_PERMISSION_KEEP_AWAKE},
    {"files_export", 1u << 8, StrId::STR_PERMISSION_FILES_EXPORT},
    {"device_info", 1u << 7, StrId::STR_PERMISSION_DEVICE_INFO},
    {"sound", 1u << 6, StrId::STR_PERMISSION_SOUND},
    {"network", 1u << 0, StrId::STR_PERMISSION_NETWORK},
    {"radio", 1u << 1, StrId::STR_PERMISSION_RADIO},
    {"notifications", 1u << 2, StrId::STR_PERMISSION_NOTIFICATIONS},
    {"background", 1u << 3, StrId::STR_PERMISSION_BACKGROUND},
    {"reading_data", 1u << 4, StrId::STR_PERMISSION_READING_DATA},
    {"wallpaper", 1u << 5, StrId::STR_PERMISSION_WALLPAPER},
};
uint32_t permissionBit(const char* name);
uint32_t grantedPermissions(const char* id);
bool setGrantedPermissions(const char* id, uint32_t granted);
// Caller must also check that the permission was declared by this package.
bool permissionGranted(const char* id, const char* permission);
}  // namespace crossstore
