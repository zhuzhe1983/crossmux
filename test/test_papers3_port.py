"""Exercise PaperS3 shutdown integration and cross-board firmware rejection."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

from test_hal_power_sources import function

ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / "freeink-sdk"


def run():
    power = (ROOT / "lib/hal/HalPowerManager.cpp").read_text()
    begin = function(power, "void HalPowerManager::begin()")
    sleep = function(power, "void HalPowerManager::startDeepSleep(HalGPIO& gpio) const")
    with tempfile.TemporaryDirectory(prefix="papers3-port-") as directory:
        tmp = Path(directory)
        (tmp / "BoardConfig.h").write_text("#pragma once\n")
        (tmp / "Arduino.h").write_text(r"""#pragma once
#include <cstdint>
#include <vector>
#include <utility>
#include <cassert>
constexpr int LOW=0, HIGH=1, INPUT=2, OUTPUT=3;
inline std::vector<std::pair<int,int>> writes;
inline int elapsed=0;
inline void pinMode(int,int) {}
inline void digitalWrite(int pin,int value) {writes.emplace_back(pin,value);}
inline void delay(int ms) {elapsed+=ms;}
""")
        (tmp / "power.cpp").write_text(r"""
#include <Arduino.h>
#include <BoardPaperS3.h>
#include <string>
using gpio_num_t=int;
constexpr int XTEINK_C3_GPIO13=13;
std::string events;
void gpio_hold_dis(int) {assert(false);}
void gpio_hold_en(int) {assert(false);}
int getCpuFrequencyMhz() {return 240;}
void* xSemaphoreCreateMutex() {static int mutex;return &mutex;}
struct {void end() {assert(false);}} logSerial;
struct {void setOn(bool on) {assert(!on);events+='L';}} Frontlight;
namespace BoardConfig {
struct Power {int8_t latch0=-1,latch1=-1;};
struct Profile {Power power;int batteryAdc=3;};Profile ACTIVE;
}
struct HalGPIO {void prepareForDeepSleep() {events+='T';}};
namespace freeink {
struct PowerManager {
 static inline void (*shutdown)()=nullptr;
 static void setHostShutdownHook(void (*hook)()) {shutdown=hook;}
 static void powerDownRailsForSleep() {events+='R';}
 static void deepSleepUntilPowerButton() {
   // The existing SDK contract invokes this hook and idles if it returns.
   assert(shutdown);events+='S';shutdown();
 }
};
}
struct HalPowerManager {
 int normalFreq=0;void* modeMutex=nullptr;
 void begin();void startDeepSleep(HalGPIO&) const;
};
""" + begin + "\n" + sleep + r"""
int main() {
 HalPowerManager power;power.begin();
 assert(freeink::PowerManager::shutdown==&BoardPaperS3::powerOff);
 assert(writes.empty()); // Initialization must not pulse the power-off line.
 HalGPIO gpio;power.startDeepSleep(gpio);
 assert(events=="LTRS"); // Frontlight/touch/rails are quiescent before shutdown.
 assert(elapsed==500 && writes.size()==11);
 for(int i=0;i<10;++i) assert(writes[i]==std::make_pair(44,i%2));
 assert(writes.back()==std::make_pair(44,LOW));
}
""")
        env = dict(os.environ, CCACHE_DISABLE="1")
        subprocess.run([
            "c++", "-std=c++17", "-DFREEINK_DEVICE_PAPERS3=1",
            "-DSOC_PM_SUPPORT_EXT1_WAKEUP=1", "-DENABLE_SERIAL_LOG",
            "-I" + str(tmp), "-I" + str(SDK / "libs/hardware/BoardPaperS3/include"),
            str(tmp / "power.cpp"), str(SDK / "libs/hardware/BoardPaperS3/src/BoardPaperS3.cpp"),
            "-o", str(tmp / "power"),
        ], check=True, env=env)
        subprocess.run([str(tmp / "power")], check=True)
        (tmp / "tag.cpp").write_text(r"""
#include "FirmwareBoardTag.h"
#include <cassert>
#include <cstring>
#include <string>
int main() {
 assert(std::string(board_tag::boardName(),board_tag::boardNameLen())=="papers3");
 for(const char* name : {"papers3","papermono","lilygo","x4"}) {
   const std::string image=std::string("binary-prefixCROSSPOINT-BOARD-V1:")+name+";suffix";
   for(size_t split=0;split<=image.size();++split) {
     board_tag::Scanner scanner;
     scanner.feed(reinterpret_cast<const uint8_t*>(image.data()),split);
     scanner.feed(reinterpret_cast<const uint8_t*>(image.data()+split),image.size()-split);
     assert(scanner.mismatch() == (std::strcmp(name,"papers3")!=0));
   }
 }
}
""")
        subprocess.run([
            "c++", "-std=c++17", "-DFREEINK_DEVICE_PAPERS3=1",
            "-I" + str(tmp), "-I" + str(ROOT / "src/network"),
            str(tmp / "tag.cpp"), str(ROOT / "src/network/FirmwareBoardTag.cpp"),
            "-o", str(tmp / "tag"),
        ], check=True, env=env)
        subprocess.run([str(tmp / "tag")], check=True)
    subprocess.run([sys.executable, str(SDK / "libs/hardware/InputManager/test/host/test_gt911_poll_gap.py")],
                   check=True)
    print("PaperS3 shutdown ordering/pulses, firmware board guard and GT911 recovery passed")


if __name__ == "__main__":
    run()
