#pragma once

inline void testLog(const char*, const char*, ...) {}
#define LOG_ERR testLog
#define LOG_INF testLog
