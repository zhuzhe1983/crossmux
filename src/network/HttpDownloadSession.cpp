#include "HttpDownloadSession.h"

#include <Arduino.h>
#include <HalMemory.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <SecureHttpClient.h>

#include <cstring>

#include "HttpDownloader.h"

class HttpDownloadSession::Impl {
 public:
  freeink::SecureHttpClient client;
};

namespace {
void logHeap(const char* phase) {
  const auto internal = HalMemory::getInternalHeap();
  const auto psram = HalMemory::getPsramHeap();
  LOG_DBG("STOREHTTP", "%s internal=%zu largest=%zu min=%zu psram=%zu", phase, internal.freeBytes,
          internal.largestBlockBytes, internal.minFreeBytes, psram.freeBytes);
}
}  // namespace

HttpDownloadSession::HttpDownloadSession() : impl_(makeUniqueNoThrow<Impl>()) {
  // One independent client for the refresh lifetime; its strings/socket/TLS
  // state cannot live on the small worker stack or be shared with foreground
  // downloads. The fallible object allocation is sizeof(Impl), below 1 KiB.
  static_assert(sizeof(Impl) < 1024);
  if (!impl_) LOG_ERR("STORE", "Cannot allocate background HTTP session (%zu bytes)", sizeof(Impl));
}

HttpDownloadSession::~HttpDownloadSession() = default;

HttpDownloadSession::Result HttpDownloadSession::fetch(const char* url, const char* destination, size_t maximum,
                                                       const std::atomic<bool>& cancel) {
  if (!impl_) return Result::NoMemory;
  if (cancel.load()) return Result::Cancelled;
  auto& http = impl_->client;
  http.end();
  if (!std::strncmp(url, "https://", 8) && !HttpDownloader::hasMemoryForTls()) {
    logHeap("TLS preflight rejected");
    return Result::NoMemory;
  }
  ScopedCleanup release([&] {
    // SecureHttpClient::end -> SecureClient::stop frees wolfSSL session/context
    // and closes transport. A successful response must release these too,
    // before the caller allocates a JSON tree or copies icons.
    http.end();
    logHeap("socket released");
  });
  http.setTimeout(3000);
  http.setFollowRedirects(3);
  // Preserve the current preview firmware's trust policy; no CA bundle is
  // configured by HttpDownloader either. Content hashes still check integrity.
  http.setInsecure();
  http.setReuse(false);
  http.setUserAgent("CrossMux-CrossStore");
  if (!http.begin(url)) return Result::Network;
  logHeap("before request");

  const uint32_t started = millis();
  Result failure = Result::Network;
  bool complete = false;
  {
    size_t received = 0;
    auto file = Storage.open(destination, O_WRONLY | O_CREAT | O_TRUNC);
    if (!file) return Result::StorageFailure;
    const int status = http.GET(
        [&](const uint8_t* data, size_t bytes) {
          if (http.getStatus() != 200) return true;
          if (bytes > maximum - received) {
            failure = Result::TooLarge;
            return false;
          }
          if (file.write(data, bytes) != bytes) {
            failure = Result::StorageFailure;
            return false;
          }
          received += bytes;
          return true;
        },
        [&] { return cancel.load() || static_cast<uint32_t>(millis() - started) >= 12000; });
    file.flush();
    complete = status == 200 && http.responseComplete() && received != 0;
  }
  if (complete && !cancel.load()) return Result::Ok;
  Storage.remove(destination);
  return cancel.load() ? Result::Cancelled : failure;
}
