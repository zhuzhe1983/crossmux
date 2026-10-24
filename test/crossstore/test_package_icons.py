#!/usr/bin/env python3
"""Run production icon loading/rendering against bounded fake storage and display."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).parent
headers = list((ROOT / '.pio/libdeps').glob('*/ArduinoJson/src/ArduinoJson.h'))
if not headers:
    raise SystemExit('ArduinoJson dependency missing: prepare a PlatformIO target first')
with tempfile.TemporaryDirectory(prefix='crossstore-icons-') as temporary:
    binary = Path(temporary) / 'icons'
    subprocess.run(['c++', '-std=c++17', '-fno-exceptions', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(HERE / 'icon_stubs'), '-I' + str(headers[0].parent),
                    '-I' + str(ROOT / 'src'), '-I' + str(ROOT / 'lib/GfxRenderer'),
                    str(ROOT / 'src/crossstore/PackageIcons.cpp'), str(HERE / 'PackageIconsTest.cpp'),
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=10)
