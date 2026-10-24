"""Regression: USB ownership drops SD font handles before the first USB page."""
from pathlib import Path
from test_reading_ui_regressions import method,run_cpp
ROOT=Path(__file__).resolve().parents[2]
system=(ROOT/'src/SdCardFontSystem.cpp').read_text()
activity=(ROOT/'src/activities/network/UsbDriveActivity.cpp').read_text()
source=r'''
#include <cassert>
#include <map>
#include <string>
static bool locked=false,detached=false;
static std::string order;
constexpr int CONTROL_18_FONT_ID=4,NOTOSANS_18_FONT_ID=5;
struct UiFontSize {int fontId,pointSize,builtinFallbackId;};
constexpr UiFontSize kUiFontSizes[]={{1,8,108},{2,10,110},{3,12,112}};
struct GfxRenderer {std::map<int,int> fallback;void setFallbackFont(int id,int fb){assert(locked&&!detached);fallback[id]=fb;}};
struct SdCardFontSystem {
 bool vectorLive=true;
 struct Manager {bool loaded=true;void unloadAll(GfxRenderer&){assert(locked&&!detached);loaded=false;order+='U';}} manager_;
 void unloadTtf(GfxRenderer& r){assert(locked&&!detached);vectorLive=false;r.fallback.clear();order+='V';}
 void releaseForStorageHandoff(GfxRenderer&);
} sdFontSystem;
struct RenderLock {template<class T>explicit RenderLock(T&){assert(!locked);locked=true;}~RenderLock(){locked=false;}};
struct StorageType {bool fail=false;bool beginUsbDrive(){assert(!sdFontSystem.manager_.loaded&&!locked);detached=true;order+='D';return !fail;}} Storage;
namespace bleinput {void stop(){order+='B';}}
struct Activity {void onEnter(){order+='E';}};
struct App {template<class T>void setScreen(T,void*){}};
struct UsbDriveActivity:Activity {
 enum class State {IoError,WaitingForHost};
 GfxRenderer renderer;App app;bool preparing=true,startFailed=false;
 State state=State::IoError;unsigned startFailureStartedAt=0,hostWaitStartedAt=0;
 static void driveScreen(){};void resetUi(){}
 void requestUpdateAndWait(){assert(!detached&&!locked);assert(renderer.fallback[1]==108&&renderer.fallback[4]==112);order+='R';}
 void requestUpdate(){}
 void onEnter();
};
unsigned millis(){return 42;}
#define LOG_ERR(...) ((void)0)
'''+method(system,'void SdCardFontSystem::releaseForStorageHandoff(')+method(activity,'void UsbDriveActivity::onEnter()')+r'''
int main(){
 for(bool fail:{false,true}){
  detached=false;order.clear();Storage.fail=fail;sdFontSystem.manager_.loaded=true;
  UsbDriveActivity usb;usb.onEnter();
#if CROSSPOINT_VECTOR_FONTS
  assert(order=="BEVURD"&&!sdFontSystem.vectorLive);
#else
  assert(order=="BEURD");
#endif
  assert(usb.renderer.fallback.size()==5);
  assert(usb.startFailed==fail&&!usb.preparing);
  assert(usb.state==(fail?UsbDriveActivity::State::IoError:UsbDriveActivity::State::WaitingForHost));
 }
}
'''
for enabled in ('0','1'):run_cpp(source,defines=('CROSSPOINT_VECTOR_FONTS='+enabled,))
print('USB font detach ordering, builtin fallback, vector/non-vector and startup failure passed')
