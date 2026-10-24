#pragma once
void recordDnsError();
#define LOG_ERR(...) recordDnsError()
