#include <esp_netif.h>

#include <array>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <mutex>
#include <thread>

extern "C" void __wrap_dns_clear_cache();

namespace {
// Exercise IDF's actual core-context dispatch with a nonrecursive host mutex.
// The only substituted behavior is the RTOS primitive used to acquire it.
std::mutex coreMutex;
thread_local bool coreHeld = false;
thread_local bool reenterClear = false;
thread_local unsigned clearDepth = 0;
std::atomic<unsigned> lockCalls{0}, execCalls{0}, clearCalls{0}, retryCalls{0};
std::atomic<unsigned> activeClears{0}, unlockedCalls{0}, errorLogs{0};
bool injectDispatchError = false;
constexpr int LWIP_CORE_LOCK_QUERY_HOLDER = 0;

struct esp_netif_api_msg_t;
using esp_netif_api_fn = esp_err_t (*)(esp_netif_api_msg_t*);
struct esp_netif_api_msg_t {
  esp_netif_callback_fn user_fn;
  void* data;
  esp_netif_api_fn api_fn;
  esp_err_t ret;
};
using tcpip_callback_fn = void (*)(void*);
bool sys_thread_tcpip(int operation) {
  assert(operation == LWIP_CORE_LOCK_QUERY_HOLDER);
  return coreHeld;
}
void tcpip_send_msg_wait_sem(tcpip_callback_fn fn, void* data, void*) {
  // A second lock attempt by an already locked callback is a bug, not a
  // recursive mutex that silently hides a device deadlock.
  assert(!coreHeld);
  const std::lock_guard<std::mutex> lock(coreMutex);
  ++lockCalls;
  coreHeld = true;
  fn(data);
  assert(coreHeld);
  coreHeld = false;
}
}  // namespace

// These are copied verbatim IDF dispatch functions, not a reimplementation of
// HalDnsGuard. Override only the exported name so one test can inject failure.
#define LWIP_TCPIP_CORE_LOCKING 1
#define ESP_LOGD(...) ((void)0)
#define esp_netif_tcpip_exec idf_esp_netif_tcpip_exec
#include "IdfTcpipExecFixture.inc"
#undef esp_netif_tcpip_exec
#undef ESP_LOGD
#undef LWIP_TCPIP_CORE_LOCKING

extern "C" esp_err_t esp_netif_tcpip_exec(esp_netif_callback_fn fn, void* context) {
  ++execCalls;
  if (injectDispatchError) return -1;
  return idf_esp_netif_tcpip_exec(fn, context);
}

void recordDnsError() { ++errorLogs; }

// Models the important part of the observed crash: clearing an in-flight DNS
// query calls SNTP's failure callback synchronously, which retries a raw lwIP
// operation requiring the same core lock. There is no deferred queue here.
static void sntpRetryFromEviction() {
  assert(coreHeld);
  ++retryCalls;
  if (reenterClear) {
    reenterClear = false;
    __wrap_dns_clear_cache();
    assert(coreHeld);
  }
}

extern "C" void __real_dns_clear_cache() {
  if (!coreHeld) {
    ++unlockedCalls;
    return;
  }
  const unsigned active = ++activeClears;
  ++clearDepth;
  assert(active == clearDepth);  // Only same-thread recursive callbacks overlap.
  // Give competing callers ample opportunity to expose a missing guard.
  std::this_thread::yield();
  ++clearCalls;
  sntpRetryFromEviction();
  --clearDepth;
  --activeClears;
}

int main() {
  // Negative control: the old unguarded call really reaches the unsafe path.
  __real_dns_clear_cache();
  assert(unlockedCalls == 1 && clearCalls == 0);
  unlockedCalls = 0;

  __wrap_dns_clear_cache();
  assert(clearCalls == 1 && retryCalls == 1 && lockCalls == 1 && !coreHeld);

  // TCP/IP task or a caller already owning the lock must not acquire it twice
  // or release a lock owned by its caller.
  {
    const std::lock_guard<std::mutex> lock(coreMutex);
    coreHeld = true;
    __wrap_dns_clear_cache();
    assert(coreHeld && lockCalls == 1 && clearCalls == 2);
    coreHeld = false;
  }

  reenterClear = true;
  __wrap_dns_clear_cache();
  assert(clearCalls == 4 && lockCalls == 2 && !coreHeld);

  std::array<std::thread, 8> workers;
  std::atomic<bool> start{false};
  for (auto& worker : workers) {
    worker = std::thread([&start]() {
      while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
      for (unsigned i = 0; i < 512; ++i) {
        __wrap_dns_clear_cache();
        assert(!coreHeld);
      }
    });
  }
  start.store(true, std::memory_order_release);
  for (auto& worker : workers) worker.join();
  assert(clearCalls == 4100 && retryCalls == clearCalls);
  assert(execCalls == clearCalls && lockCalls == 4098);
  assert(activeClears == 0 && unlockedCalls == 0 && errorLogs == 0);

  // An API failure must not cause an unsafe fallback or a second clear.
  injectDispatchError = true;
  __wrap_dns_clear_cache();
  assert(clearCalls == 4100 && unlockedCalls == 0 && errorLogs == 1);
  injectDispatchError = false;
  __wrap_dns_clear_cache();
  assert(clearCalls == 4101 && !coreHeld && activeClears == 0);
  std::puts("HAL DNS guard: worker/core callbacks, SNTP retry, reentry, concurrency and failure passed");
}
