#!/usr/bin/env python3
"""Exercise actual WeRead adapter lifecycle with deterministic protocol/storage owners."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT/'src/activities/apps/appstore/LuaWeReadService.cpp').read_text()
body = source[source.index('class LuaWeReadService'):]
prefix = r'''
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include "activities/apps/appstore/LuaNativeService.h"
#define LOG_ERR(...) ((void)0)
inline int locks=0,resets=0,coverStarts=0,normalStarts=0,steps=0;
inline bool coverActive=false,available[4]={false,true,false,false},noMemory=false,badRecord=false;
struct RenderLock {RenderLock(){++locks;}~RenderLock(){--locks;}};
struct FontCacheManager {void clearCache(){}};
class GfxRenderer {public:struct FrameBufferLoan{explicit FrameBufferLoan(GfxRenderer&) {}};FontCacheManager*getFontCacheManager(){return nullptr;}};
struct Rect {int x,y,width,height;};
struct HalFile {void close(){}bool seek(size_t){return false;}int read(char*,size_t){return 0;}};
struct StorageType {bool exists(const char*){return false;}} Storage;
constexpr int WL_CONNECTED=3,WIFI_OFF=0;
struct Wifi {int status()const{return WL_CONNECTED;}int getMode()const{return 1;}} WiFi;
unsigned millis(){return 100;}
size_t utf8SafeTruncateBuffer(char*,size_t length){return length;}
struct MappedInputManager {enum class Button{Back};void update(bool){}bool wasReleased(Button){return false;}bool wasHomeGesture(){return false;}};
class LuaAppActivity {
 public:
  bool nativeInputConsumed_=false,canvasMode_=true,dirty_=false,systemWifiOwned_=false,wifiPending_=false;
  bool networkAllowed=false;
  char pendingBook_[512]{},pendingRoute_[32]{};
  GfxRenderer renderer; MappedInputManager mappedInput;
  Rect canvasBounds()const{return {10,20,300,400};}
  bool allowed(const char*)const{return networkAllowed;}
  void dispatch(const char*){assert(locks==0);}
};
namespace WeReadStore {
enum class ImagePolicy {Embed,Exclude};
struct BookOptions {ImagePolicy imagePolicy=ImagePolicy::Embed;};
struct ShelfRecord {char bookId[57]{},title[192]{},author[77]{};};
struct BookDetailHeader {char title[192]{},publisher[77]{};unsigned totalWords=0,newRating=0,introLength=0;};
struct TocRecord {char title[192]{};bool paid=false;unsigned wordCount=0;};
bool acceptDisclaimer(){return true;}bool hasAcceptedDisclaimer(){return true;}bool ensureRoot(){return true;}
bool clearSession(){return true;}bool clearShelf(){return true;}
bool openShelf(HalFile&,uint32_t&count){count=3;return true;}
bool readShelfRecord(HalFile&,uint32_t index,ShelfRecord&record){
 if(index>=3)return false; record={}; snprintf(record.bookId,sizeof(record.bookId),"%u",index+1);
 strcpy(record.title,"Book");strcpy(record.author,"Author");
 if(badRecord)memset(record.bookId,'A',sizeof(record.bookId));return true;
}
std::string bookDirectory(const char*id){return id;}
std::string coverPath(const std::string&id){return id;}
std::string finalBookPath(const ShelfRecord&){return "/offline.epub";}
bool loadBookOptions(const std::string&,BookOptions&){return false;}
bool openBookDetail(const std::string&,BookDetailHeader&,HalFile&){return false;}
}
class LuaWeReadCache {
 public:enum class State{Missing,Ready,Invalid};void close(){}State open(const char*){return State::Missing;}
 unsigned count()const{return 0;}bool read(int64_t,WeReadStore::TocRecord&,bool&){return false;}
};
class LuaWeReadCovers {
 public:
  enum class State{Empty,Missing,Ready,Invalid,NoMemory,Unavailable};
  struct Slot {unsigned requested=0,loaded=0;State state=State::Empty;};Slot slots[12]{};size_t n=0;
  ~LuaWeReadCovers(){assert(!coverActive);}
  static bool parsePlacement(const char*text,int(&out)[4]){
    char tail;return sscanf(text,"%d:%d:%d:%d%c",&out[0],&out[1],&out[2],&out[3],&tail)==4 &&
      out[0]>=0&&out[1]>=0&&out[2]>0&&out[3]>0;
  }
  bool place(unsigned index,int,int,int,int){if(n==12)return false;slots[n++].requested=index;return true;}
  void clearPlacements(){n=0;}void clear(){n=0;for(auto&s:slots)s={};}
  void prepare(){for(size_t i=0;i<12;++i){auto&s=slots[i];if(i>=n)s={};else if(s.requested!=s.loaded){s.loaded=s.requested;s.state=State::Empty;}}}
  size_t count()const{return n;}unsigned index(size_t i)const{return i<n?slots[i].requested:0;}
  State state(size_t i)const{return i<n?slots[i].state:State::Unavailable;}
  void unavailable(size_t i){assert(locks);if(i<n)slots[i].state=State::Unavailable;}
  State load(size_t i,const char*){assert(locks);return slots[i].state=noMemory?State::NoMemory:available[slots[i].requested]?State::Ready:State::Missing;}
  bool beginFrame(GfxRenderer&){return false;}void displayFrame(GfxRenderer&){}
};
namespace WeReadClient {
enum class Error{Ok,SdCard};
struct DownloadOptions {WeReadStore::ImagePolicy imagePolicy;enum class ChapterScope{SelectRange,WholeBook};ChapterScope chapterScope;};
class Operation {
 public:
  enum class Kind{Sync,Detail,Download};
  enum class Event{None,QrReady,Authenticated,DetailReady,ChapterRangeReady,ChapterComplete,Complete,Cancelled,Failed};
  inline static Event next=Event::None;
  bool running=false;
  void reset(){++resets;running=false;coverActive=false;}void cancel(){next=Event::Cancelled;}
  bool active()const{return running;}Error error()const{return Error::Ok;}
  const char*progressTitle()const{return "";}unsigned progressCompleted()const{return 0;}unsigned progressTotal()const{return 0;}unsigned progressStage()const{return 0;}
  const char*qrUrl()const{return "";}unsigned chapterCount()const{return 0;}bool readChapter(int64_t,WeReadStore::TocRecord&){return false;}
  bool setChapterRange(unsigned,unsigned){return false;}
  bool begin(Kind,const WeReadStore::ShelfRecord*,DownloadOptions){++normalStarts;running=true;return true;}
  bool beginCover(const WeReadStore::ShelfRecord&){++coverStarts;running=coverActive=true;return true;}
  bool needsCoverConversionScratch()const{return false;}
  Event step(void*,void*,bool(*)(void*),void*){++steps;const auto event=next;next=Event::None;if(event==Event::Complete||event==Event::Failed||event==Event::Cancelled)running=false;return event;}
};
}
template<class T,class...A>std::unique_ptr<T>makeUniqueNoThrow(A&&...a){return std::make_unique<T>(std::forward<A>(a)...);}
'''
tests = r'''
int main(){
 LuaAppActivity owner;auto service=crossstore::makeWeReadService(owner);crossstore::ServiceReply reply;
 assert(service->command("weread.start","",0,reply));service->pump();
 assert(service->command("weread.cover","0:0:112:164",1,reply));service->pump();
 assert(coverStarts==0&&normalStarts==0&&!owner.wifiPending_); // Cached cover without network permission.
 assert(!service->command("weread.cover","299:0:112:164",1,reply));
 assert(!service->command("weread.cover","0:0:112:164",0,reply));
 assert(!service->command("weread.cover","/session.bin",1,reply));
 assert(!service->command("weread.sync","",0,reply));
 assert(!service->command("weread.fetch_detail","",1,reply));
 assert(!service->command("weread.download","",1,reply));
 service->clearCanvas();assert(service->command("weread.cover","0:0:112:164",2,reply));service->pump();
 assert(coverStarts==0); // Missing cover cannot implicitly request network.
 service->clearCanvas();service->pump();
 assert(service->command("weread.cover","0:0:112:164",2,reply));
 assert(service->command("weread.cover","120:0:112:164",1,reply));
 owner.networkAllowed=true;service->pump();assert(coverStarts==0); // Finish visible local loads before networking.
 service->pump();assert(coverStarts==1&&coverActive);
 const auto before=resets;
 service->clearCanvas();assert(service->command("weread.cover","0:0:112:164",3,reply));service->pump();
 assert(resets>before&&coverStarts==2&&coverActive); // Old page closed first.
 assert(service->command("weread.cache","",3,reply));assert(!coverActive);
 service->pump();assert(coverStarts==3);
 owner.networkAllowed=false;const auto beforeSteps=steps;service->pump();
 assert(!coverActive&&steps==beforeSteps); // Revoke stops before another transport step.
 service->clearCanvas();service->pump();owner.networkAllowed=true;
 assert(service->command("weread.cover","0:0:112:164",2,reply));
 WeReadClient::Operation::next=WeReadClient::Operation::Event::Failed;service->pump();
 const auto failedStarts=coverStarts;service->pump();assert(coverStarts==failedStarts); // No tight retry loop.
 service->clearCanvas();service->pump();noMemory=true;
 assert(service->command("weread.cover","0:0:112:164",3,reply));service->pump();
 assert(coverStarts==failedStarts);noMemory=false;
 service->clearCanvas();service->pump();badRecord=true;
 assert(service->command("weread.cover","0:0:112:164",3,reply));service->pump();assert(coverStarts==failedStarts);
 badRecord=false;service->clearCanvas();service->pump();
 assert(service->command("weread.cover","0:0:112:164",3,reply));service->pump();assert(coverActive);
 assert(service->command("weread.download","images",1,reply));assert(!coverActive);service->pump();
 assert(normalStarts==1);service.reset();assert(!coverActive&&locks==0);
 puts("WeRead service: cached offline, network denial/revocation, page cancellation, per-cover failure, OOM, priority and cleanup passed");
}
'''
with tempfile.TemporaryDirectory(prefix='weread-service-') as temporary:
    source = Path(temporary)/'test.cpp'; binary = Path(temporary)/'test'
    source.write_text(prefix+body+tests)
    subprocess.run(['c++','-std=c++20','-fno-exceptions','-Wall','-Wextra','-Werror',
                    '-I'+str(ROOT/'src'),'-I'+str(ROOT/'lib/CrossStoreRuntime'),str(source),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,timeout=15)
