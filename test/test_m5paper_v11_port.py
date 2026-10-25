"""Compile the actual classic-ESP32 board profile/tag and IT8951 transaction tests."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / 'freeink-sdk'
HARDWARE = SDK / 'libs/hardware'


def run():
    with tempfile.TemporaryDirectory(prefix='m5paper-port-') as directory:
        tmp = Path(directory)
        (tmp / 'driver').mkdir()
        (tmp / 'driver/gpio.h').write_text('#pragma once\nusing gpio_num_t=int; inline void gpio_hold_dis(int) {} inline void gpio_hold_en(int) {}\n')
        (tmp / 'esp_rom_sys.h').write_text('#pragma once\ninline void esp_rom_printf(const char*, ...) {}\n')
        (tmp / 'board.cpp').write_text(r'''
#include <BoardConfig.h>
#include "FirmwareBoardTag.h"
#include <cassert>
#include <cstring>
#include <string>
static_assert(FREEINK_DRIVER_IT8951 && FREEINK_CAP_RTC);
static_assert(BoardConfig::M5PAPER_V11.grayscaleLevels == 16);
int main() {
 assert(std::string(board_tag::boardName(), board_tag::boardNameLen()) == "m5paper");
 for (const char* name : {"m5paper", "papers3", "papermono", "x4"}) {
   const std::string image = std::string("binaryCROSSPOINT-BOARD-V1:") + name + ";suffix";
   for (size_t split = 0; split <= image.size(); ++split) {
     board_tag::Scanner scanner;
     scanner.feed(reinterpret_cast<const uint8_t*>(image.data()), split);
     scanner.feed(reinterpret_cast<const uint8_t*>(image.data() + split), image.size() - split);
     assert(scanner.mismatch() == (std::strcmp(name, "m5paper") != 0));
   }
 }
}
''')
        includes = (tmp, HARDWARE / 'BoardConfig/include', HARDWARE / 'Rtc/test/host/stubs',
                    HARDWARE / 'BatteryMonitor/test/host/stubs', ROOT / 'src/network')
        args = ['c++', '-std=c++20', '-include', 'initializer_list', '-DFREEINK_DEVICE_M5PAPER=1', '-fsanitize=address,undefined']
        for path in includes:
            args += ['-I', str(path)]
        subprocess.run([*args, str(tmp / 'board.cpp'), str(ROOT / 'src/network/FirmwareBoardTag.cpp'),
                        '-o', str(tmp / 'board')], check=True, env=dict(os.environ, CCACHE_DISABLE='1'))
        subprocess.run([str(tmp / 'board')], check=True)
    subprocess.run([sys.executable, str(SDK / 'libs/display/FreeInkDisplay/test/host/test_it8951_native.py')],
                   check=True)
    print('M5Paper board capabilities, cross-board update rejection and IT8951 checks passed')


if __name__ == '__main__':
    run()
