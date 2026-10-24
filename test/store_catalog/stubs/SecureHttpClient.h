#pragma once
#include <HalMemory.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace fakeHttp {
struct Response {
  std::string body;
  int status = 200;
  bool block = false;
  bool complete = true;
  unsigned transientFailures = 0;
};
inline std::map<std::string, Response> responses;
inline std::atomic<unsigned> requests{0}, blocked{0}, timeout{0};
inline auto& liveSockets = fakeMemory::liveNetworkSockets;
inline std::vector<size_t> workerMemoryAtRequest;
}  // namespace fakeHttp
namespace freeink {
class SecureHttpClient {
 public:
  ~SecureHttpClient() { end(); }
  void setTimeout(unsigned timeout) { fakeHttp::timeout.store(timeout); }
  void setFollowRedirects(int) {}
  void setInsecure() {}
  void setReuse(bool) {}
  void setUserAgent(const char*) {}
  bool begin(const char* url) {
    url_ = url;
    return true;
  }
  int getStatus() const { return status_; }
  bool responseComplete() const { return complete_; }
  void end() {
    if (connected_) --fakeHttp::liveSockets;
    connected_ = false;
  }
  int GET(const std::function<bool(const uint8_t*, size_t)>& writer, const std::function<bool()>& abort) {
    connected_ = true;
    ++fakeHttp::liveSockets;
    fakeHttp::workerMemoryAtRequest.push_back(fakeMemory::liveBytes(true));
    ++fakeHttp::requests;
    complete_ = false;
    const auto found = fakeHttp::responses.find(url_);
    if (found == fakeHttp::responses.end()) {
      status_ = 404;
      return status_;
    }
    if (found->second.transientFailures) {
      --found->second.transientFailures;
      return -1;
    }
    const auto response = found->second;
    status_ = response.status;
    if (response.block) {
      ++fakeHttp::blocked;
      while (!abort()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
      return -1;
    }
    for (size_t i = 0; i < response.body.size(); i += 127) {
      if (abort() || !writer(reinterpret_cast<const uint8_t*>(response.body.data() + i),
                             std::min<size_t>(127, response.body.size() - i)))
        return status_;
    }
    complete_ = response.complete;
    return status_;
  }

 private:
  std::string url_;
  int status_ = 0;
  bool complete_ = false;
  bool connected_ = false;
};
}  // namespace freeink
