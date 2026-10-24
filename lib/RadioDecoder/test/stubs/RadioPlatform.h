#pragma once
#include <pthread.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <vector>

inline bool failTask = false, failOutputTask = false, failWorkspace = false, failControl = false, failBegin = false,
            failAudio = false, paceAudio = true;
inline size_t internalFree = 100000;
inline std::atomic<unsigned> workspaces{0}, tasks{0}, audioOwners{0}, writes{0}, capsDeleted{0};
inline std::atomic<unsigned> controls{0}, sessions{0};
inline std::atomic<unsigned> outputTasksDeleted{0}, dmaMisses{0}, audioSamples{0};
inline std::vector<int16_t> capturedPcm;
struct PcmWrite {
  size_t offset, count;
};
inline std::vector<PcmWrite> pcmWrites;
inline std::chrono::steady_clock::time_point dmaUntil;
inline std::atomic<uint32_t> extraTime{0};
inline uint32_t millis() {
  static const auto origin = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - origin).count() +
         extraTime.load();
}
inline uint32_t micros() {
  static const auto origin = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - origin).count();
}
inline void testLog(const char*, const char*, ...) {}
#define LOG_INF testLog
#define LOG_ERR testLog
#define LOG_DBG testLog

enum eTaskState { eRunning, eSuspended };
struct FakeTask {
  std::thread thread;
  std::atomic<eTaskState> state{eRunning};
  std::atomic<bool> erase{false};
  bool output = false;
};
using TaskHandle_t = FakeTask*;
using BaseType_t = int;
inline constexpr int pdPASS = 1;
inline constexpr unsigned MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2;
inline thread_local FakeTask* currentTask = nullptr;
inline unsigned pdMS_TO_TICKS(unsigned value) { return value; }
inline void vTaskDelay(unsigned value) { std::this_thread::sleep_for(std::chrono::milliseconds(value)); }
inline eTaskState eTaskGetState(TaskHandle_t task) { return task->state.load(); }
inline unsigned uxTaskGetStackHighWaterMark(TaskHandle_t) { return 12000; }
inline void vTaskSuspend(TaskHandle_t) {
  currentTask->state.store(eSuspended);
  while (!currentTask->erase.load()) vTaskDelay(1);
  pthread_exit(nullptr);
}
inline BaseType_t xTaskCreateWithCaps(void (*entry)(void*), const char* name, unsigned stack, void* context,
                                      unsigned priority, TaskHandle_t* handle, unsigned caps) {
  const bool output = std::string(name) == "AirTunePCM";
  if (failTask || (output && failOutputTask)) return 0;
  if (stack != (output ? 4096u : 32768u) || priority != (output ? 2u : 1u) ||
      caps != (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))
    std::abort();
  auto* task = new (std::nothrow) FakeTask;
  if (!task) return 0;
  task->output = output;
  ++tasks;
  *handle = task;
  task->thread = std::thread([task, entry, context] {
    currentTask = task;
    entry(context);
  });
  return pdPASS;
}
inline void vTaskDeleteWithCaps(TaskHandle_t task) {
  if (task->state.load() != eSuspended || audioOwners.load() || workspaces.load() != (task->output ? 1u : 0u))
    std::abort();
  task->erase.store(true);
  task->thread.join();
  if (task->output) ++outputTasksDeleted;
  delete task;
  --tasks;
  ++capsDeleted;
}

class HalMemory {
 public:
  struct HeapStats {
    size_t freeBytes, totalBytes, minFreeBytes, largestBlockBytes;
  };
  struct PsramDeleter {
    void operator()(uint8_t* p) const {
      delete[] p;
      --workspaces;
    }
  };
  using PsramBuffer = std::unique_ptr<uint8_t[], PsramDeleter>;
  static PsramBuffer allocatePsram(size_t size) {
    if (failWorkspace) return {};
    auto* data = new (std::nothrow) uint8_t[size];
    if (data) ++workspaces;
    return PsramBuffer(data);
  }
  static HeapStats getInternalHeap() { return {internalFree, 100000, internalFree, internalFree}; }
  static HeapStats getPsramHeap() { return {1000000, 1000000, 1000000, 1000000}; }
};
namespace HalRadioAudio {
enum class OutputKind { None, Buzzer, Speaker };
inline OutputKind configuredOutput = OutputKind::Buzzer;
inline OutputKind outputKind() { return configuredOutput; }
inline bool supports() { return outputKind() != OutputKind::None; }
inline bool begin(uint8_t volume) {
  if (volume != 50 || failBegin) return false;
  ++audioOwners;
  dmaUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(8);
  return true;
}
inline void end() { --audioOwners; }
inline void setVolume(uint8_t) {}
inline bool write(const int16_t* samples, size_t size) {
  if (!audioOwners.load() || !size || size > 512) std::abort();
  ++writes;
  if (failAudio) return false;
  pcmWrites.push_back({capturedPcm.size(), size});
  capturedPcm.insert(capturedPcm.end(), samples, samples + size);
  audioSamples.fetch_add(static_cast<unsigned>(size));
  if (paceAudio) {
    const auto now = std::chrono::steady_clock::now();
    if (now > dmaUntil) ++dmaMisses;
    // The real producer blocks until only the 3*64 sample (8 ms) DMA tail
    // remains. Network sleeps must not delay this separate 24 kHz consumer.
    const auto finish =
        std::max(now, dmaUntil) + std::chrono::nanoseconds(size * 1000000000ULL / 24000) - std::chrono::milliseconds(8);
    std::this_thread::sleep_until(finish);
    // macOS sleep coalescing is not a DMA device. Model the final DMA write
    // completing at wakeup; measure starvation only between HAL calls, not
    // scheduler oversleep inside this substitute driver.
    dmaUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(8);
  }
  return true;
}
}  // namespace HalRadioAudio

enum class NetworkMode {
  Complete,
  HoldBefore,
  HoldAfter,
  Hls,
  Redirect,
  RedirectTooLong,
  Jitter,
  LongGap,
  FailAfter,
  TimeoutAfter,
  HoldFull,
  HoldEarly
};
inline NetworkMode networkMode = NetworkMode::Complete;
inline std::vector<uint8_t> mp3Bytes;
inline std::atomic<unsigned> requests{0};
inline std::atomic<bool> networkPaused{false}, networkBlocked{false};

namespace freeink {
class SecureHttpClient {
 public:
  SecureHttpClient() { ++sessions; }
  ~SecureHttpClient() { --sessions; }
  int status = 0;
  bool complete = false;
  std::string mime = "audio/mpeg", location = "https://radio.example/final";
  void end() {}
  void setTimeout(unsigned value) {
    if (value != 3000) std::abort();
  }
  void setFollowRedirects(unsigned value) {
    if (value) std::abort();
  }
  void setReuse(bool) {}
  void setInsecure() {}
  void setUserAgent(const char*) {}
  bool begin(const char*) { return true; }
  void addHeader(const char*, const char*) {}
  int getStatus() const { return status; }
  bool responseComplete() const { return complete; }
  const std::string* getUniqueHeader(const char* name) const {
    if (std::string(name) == "content-type") return &mime;
    if (std::string(name) == "location") return &location;
    return nullptr;
  }
  static bool resolveUrl(const char*, const std::string& value, std::string& next) {
    next = value;
    return true;
  }
  template <typename Body, typename Abort>
  int GET(Body body, Abort abort) {
    const unsigned request = ++requests;
    status = 200;
    if ((networkMode == NetworkMode::Redirect || networkMode == NetworkMode::RedirectTooLong) && request == 1) {
      status = 302;
      if (networkMode == NetworkMode::RedirectTooLong) location = "https://radio.example/" + std::string(512, 'x');
      body(mp3Bytes.data(), 1);
      return status;
    }
    if (networkMode == NetworkMode::HoldBefore)
      while (!abort()) vTaskDelay(1);
    else {
      if (networkMode == NetworkMode::Hls) mime = "application/vnd.apple.mpegurl";
      for (size_t at = 0; at < mp3Bytes.size(); at += 2048) {
        if (at == 2048 && networkMode == NetworkMode::HoldEarly) {
          while (!abort()) vTaskDelay(1);
          return status;
        }
        if (at == 16384 && (networkMode == NetworkMode::Jitter || networkMode == NetworkMode::LongGap)) {
          networkPaused.store(true);
          const auto until = std::chrono::steady_clock::now() +
                             std::chrono::milliseconds(networkMode == NetworkMode::Jitter ? 300 : 1800);
          while (std::chrono::steady_clock::now() < until) {
            if (abort()) return status;
            vTaskDelay(1);
          }
          networkPaused.store(false);
        }
        if (at == 16384 && networkMode == NetworkMode::TimeoutAfter) {
          extraTime.store(11000);
          while (!abort()) vTaskDelay(1);
          return status;
        }
        if (at == 16384 && networkMode == NetworkMode::FailAfter) return status;
        networkBlocked.store(true);
        if (abort() || !body(mp3Bytes.data() + at, std::min(size_t{2048}, mp3Bytes.size() - at))) return status;
        networkBlocked.store(false);
      }
      if (networkMode == NetworkMode::HoldAfter)
        while (!abort()) vTaskDelay(1);
      else
        complete = true;
    }
    return status;
  }
};
}  // namespace freeink
