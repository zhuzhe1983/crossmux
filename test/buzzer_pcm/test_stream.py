#!/usr/bin/env python3
"""Exercise production PCM streaming/HAL with failure-injected hardware calls."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TEST = Path(__file__).parent
SDK = ROOT / "freeink-sdk/libs/hardware/Buzzer"

with tempfile.TemporaryDirectory(prefix="buzzer-stream-") as temp:
    variants = [
        ("mono", ["-DFREEINK_CAP_BUZZER=1", "-DFREEINK_DEVICE_PAPERMONO=1", "-DCONFIG_IDF_TARGET_ESP32S3=1"]),
        ("other_s3", ["-DFREEINK_CAP_BUZZER=1", "-DFREEINK_DEVICE_PAPERMONO=0", "-DCONFIG_IDF_TARGET_ESP32S3=1"]),
        ("tone", ["-DFREEINK_CAP_BUZZER=1"]),
        ("none", ["-DFREEINK_CAP_BUZZER=0"]),
        ("emulated", ["-DFREEINK_CAP_BUZZER=1", "-DCROSSPOINT_EMULATED=1"]),
    ]
    for name, flags in variants:
        binary = str(Path(temp) / name)
        subprocess.run([
            "c++", "-std=c++17", "-pthread", "-fno-exceptions", "-Wall", "-Wextra", "-Werror",
            *flags, "-I" + str(TEST / "stubs"), "-I" + str(SDK / "include"),
            "-I" + str(ROOT / "lib/hal"),
            str(SDK / "src/Buzzer.cpp"), str(ROOT / "lib/hal/HalBuzzer.cpp"),
            str(TEST / "BuzzerStreamTest.cpp"), "-o", binary,
        ], check=True)
        subprocess.run([binary], check=True)
        print(name, "stream passed")
