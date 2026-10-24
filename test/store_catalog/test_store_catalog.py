#!/usr/bin/env python3
"""Run production store cache/session against fault-injected storage/transport."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
headers = list((ROOT / '.pio/libdeps').glob('*/ArduinoJson/src/ArduinoJson.h'))
if not headers:
    raise SystemExit('Prepare PlatformIO dependencies before running this test')
with tempfile.TemporaryDirectory(prefix='crossstore-cache-') as directory:
    binary = str(Path(directory) / 'store-cache')
    subprocess.run(['c++', '-std=c++20', '-O1', '-fno-exceptions', '-Wall', '-Wextra', '-Werror', '-pthread',
                    '-I'+str(HERE/'stubs'), '-I'+str(ROOT/'test/crossstore/catalog_stubs'),
                    '-I'+str(headers[0].parent), '-I'+str(ROOT/'src'),
                    '-I'+str(ROOT/'lib/CrossStoreRuntime'),
                    str(ROOT/'src/crossstore/StoreCatalog.cpp'),
                    str(ROOT/'src/crossstore/PackageManifest.cpp'),
                    str(ROOT/'src/network/HttpDownloadSession.cpp'),
                    str(HERE/'StoreCatalogTest.cpp'), '-o', binary], check=True)
    for mode in ('internal', 'psram'):
        subprocess.run([binary, str(Path(directory)/mode), mode], check=True, timeout=20)
