#!/usr/bin/env python3
"""Compile the production Lua adapter with deterministic network/audio owners."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'src/activities/apps/appstore/LuaRadioService.cpp').read_text()
# Hardware owners are replaced, but every command, queue and lifecycle method
# below is the actual firmware implementation, including its service factory.
body = source[source.index('namespace {'):source.rindex('#else')]
prefix = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <memory>
#include "activities/apps/appstore/LuaNativeService.h"
#define LOG_ERR(...) ((void)0)
constexpr int WL_CONNECTED=3, WIFI_OFF=0;
struct Wifi { int state=0, mode=0; int status() const{return state;} int getMode() const{return mode;} } WiFi;
struct Renderer {};
class LuaAppActivity {
 public:
  bool keepAwake_=false,systemWifiOwned_=false,wifiPending_=false,awakeAllowed=true;
  Renderer renderer;
  bool allowed(const char*)const{return awakeAllowed;}
};
namespace NetworkStartup { inline int prepares=0; void prepare(Renderer&){++prepares;} }
namespace HalRadioAudio { inline bool enabled=true; bool supports(){return enabled;} }
namespace HalMemory { struct Heap {unsigned totalBytes;}; Heap getPsramHeap(){return {1024*1024};} }
template<class T,class... Args> std::unique_ptr<T> makeUniqueNoThrow(Args&&...args){return std::make_unique<T>(std::forward<Args>(args)...);}
namespace radio {
constexpr uint8_t DEFAULT_VOLUME=50;
enum class State:uint8_t{Idle,Connecting,Buffering,Playing,Error};
enum class Error:uint8_t{None,InvalidUrl,Unsupported,Network,Timeout,NoMemory,Audio};
struct Snapshot {State state=State::Idle;Error error=Error::None;uint32_t bytes=0,frames=0;};
inline int starts=0,joins=0; inline uint8_t gain=DEFAULT_VOLUME; inline Snapshot current;
class Player {
 public:
  bool start(const char*){++starts;current.state=State::Connecting;return true;}
  void stopAndJoin(){++joins;current={};}
  Snapshot snapshot()const{return current;}
  void setVolume(uint8_t value){gain=value;}
};
}
'''
tests = r'''
int main(){
  LuaAppActivity owner;
  auto service=crossstore::makeRadioService(owner);assert(service);
  crossstore::ServiceReply reply;
  assert(service->command("radio.start","",0,reply));assert(radio::starts==0);
  assert(service->command("radio.status","",0,reply));assert(reply.a==50);
  assert(!service->command("radio.play","file:///book",0,reply));
  assert(!service->command("radio.play","https://u:p@host/stream",0,reply));
  assert(service->command("radio.play","https://example.test/live.mp3",0,reply));
  assert(!owner.wifiPending_ && radio::starts==0);
  service->pump();assert(owner.wifiPending_ && owner.systemWifiOwned_ && radio::starts==0);
  int joins=radio::joins;service->pump();assert(radio::joins==joins);
  service->network(false);service->pump();assert(radio::starts==0 && !owner.keepAwake_);
  assert(service->command("radio.status","",0,reply));assert(!strcmp(reply.text,"error")&&!strcmp(reply.extra,"offline"));
  WiFi.state=WL_CONNECTED;WiFi.mode=1;
  assert(service->command("radio.play","http://example.test/live.mp3",0,reply));
  service->pump();assert(radio::starts==1 && NetworkStartup::prepares==1 && owner.keepAwake_);
  radio::current.state=radio::State::Playing;radio::current.bytes=1024;service->pump();
  service->command("radio.status","",0,reply);assert(!strcmp(reply.text,"playing") && reply.b==1024);
  const int64_t revision=reply.c;
  assert(service->command("radio.volume","",80,reply));assert(radio::gain==80);
  assert(!service->command("radio.volume","",101,reply));
  service->command("radio.status","",0,reply);assert(reply.a==80 && reply.c>revision);
  owner.awakeAllowed=false;service->pump();assert(!owner.keepAwake_);
  radio::current.state=radio::State::Error;radio::current.error=radio::Error::NoMemory;
  service->pump();service->command("radio.status","",0,reply);assert(!strcmp(reply.extra,"no_memory"));
  assert(service->command("radio.stop","",0,reply));service->pump();
  service->command("radio.status","",0,reply);assert(!strcmp(reply.text,"idle"));
  joins=radio::joins;service.reset();assert(radio::joins>joins && !owner.keepAwake_);
  HalRadioAudio::enabled=false;assert(!crossstore::makeRadioService(owner));
  puts("Lua radio: deferred play, Wi-Fi cancellation, volume, errors, capability and joined cleanup passed");
}
'''
with tempfile.TemporaryDirectory(prefix='crossstore-radio-service-') as directory:
    cpp=Path(directory)/'test.cpp';binary=Path(directory)/'test'
    cpp.write_text(prefix+body+tests)
    subprocess.run(['c++','-std=c++17','-fno-exceptions','-Wall','-Wextra','-Werror',
                    '-I'+str(ROOT/'src'),'-I'+str(ROOT/'lib/CrossStoreRuntime'),str(cpp),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,timeout=10)
