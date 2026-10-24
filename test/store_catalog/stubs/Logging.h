#pragma once
#define LOG_ERR(...) ((void)0)
template <typename... Args>
void fakeLog(const char*, const char*, Args&&...) {}
#define LOG_DBG(...) fakeLog(__VA_ARGS__)
