#!/usr/bin/env python3
"""Compile the actual SDK buzzer, exercising PCM lifecycle and fallback paths."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TEST = Path(__file__).parent
SDK = ROOT / 'freeink-sdk/libs/hardware/Buzzer'
with tempfile.TemporaryDirectory(prefix='buzzer-pcm-') as temp:
    for name, flags in [('s3', ['-DFREEINK_CAP_BUZZER=1', '-DCONFIG_IDF_TARGET_ESP32S3=1']),
                        ('tone', ['-DFREEINK_CAP_BUZZER=1']), ('none', ['-DFREEINK_CAP_BUZZER=0'])]:
        binary = str(Path(temp) / name)
        subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', *flags,
                        '-I'+str(TEST/'stubs'), '-I'+str(SDK/'include'),
                        str(SDK/'src/Buzzer.cpp'), str(TEST/'BuzzerPcmTest.cpp'), '-o', binary], check=True)
        subprocess.run([binary], check=True)
        print(name, 'passed')
