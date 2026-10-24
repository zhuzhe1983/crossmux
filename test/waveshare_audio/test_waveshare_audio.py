#!/usr/bin/env python3
"""Compile complete production audio/power HALs against bounded hardware fakes."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TEST = Path(__file__).resolve().parent
SDK = ROOT / "freeink-sdk/libs/hardware/AudioManager/include"
COMPILER = shlex.split(os.environ.get("CXX", "c++"))
COMMON = [
    "-std=c++20", "-pthread", "-fno-exceptions", "-Wall", "-Wextra", "-Werror",
    "-I" + str(TEST / "stubs"), "-I" + str(SDK), "-I" + str(ROOT / "lib/hal"),
]

# The same translation unit also serves other sound-feedback boards and builds
# without sound. Compile every remaining macro combination without copying its
# guarded source or linking the Waveshare-specific runtime fixture into it.
for name, sound, waveshare in (
    ("other-board-sound", 1, 0),
    ("other-board-no-sound", 0, 0),
    ("waveshare-no-sound", 0, 1),
):
    subprocess.run(
        COMPILER + COMMON + [
            f"-DCROSSPOINT_CAP_SOUND_FEEDBACK={sound}",
            f"-DFREEINK_DEVICE_WAVESHARE_EPAPER_397={waveshare}",
            "-fsyntax-only", str(ROOT / "lib/hal/HalAudioOutput.cpp"),
        ],
        check=True, timeout=60,
    )
    print(name, "HAL syntax/warnings passed", flush=True)

with tempfile.TemporaryDirectory(prefix="waveshare-audio-") as temporary:
    binary = Path(temporary) / "waveshare-audio-test"
    subprocess.run(
        COMPILER + COMMON
        + [
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            "-DCROSSPOINT_CAP_SOUND_FEEDBACK=1", "-DFREEINK_DEVICE_WAVESHARE_EPAPER_397=1",
            str(ROOT / "lib/hal/HalAudioOutput.cpp"),
            str(ROOT / "lib/hal/Waveshare397Power.cpp"),
            str(TEST / "WaveshareAudioTest.cpp"), "-o", str(binary),
        ],
        check=True, timeout=60,
    )
    subprocess.run([str(binary)], check=True, timeout=30)
