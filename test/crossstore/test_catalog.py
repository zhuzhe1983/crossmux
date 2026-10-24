#!/usr/bin/env python3
"""Compile production manifest parsing and bounded catalog navigation metadata."""
from pathlib import Path
import subprocess
import tempfile
import sys

ROOT = Path(__file__).resolve().parents[2]
TEST = Path(__file__).parent
headers = list((ROOT/'.pio/libdeps').glob('*/ArduinoJson/src/ArduinoJson.h'))
if not headers:
    raise SystemExit('ArduinoJson dependency missing: prepare a PlatformIO target first')
with tempfile.TemporaryDirectory(prefix='crossstore-catalog-') as temp:
    binary = str(Path(temp)/'catalog')
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fno-exceptions',
                    '-I'+str(TEST/'catalog_stubs'), '-I'+str(headers[0].parent),
                    '-I'+str(ROOT/'src'), '-I'+str(ROOT/'lib/CrossStoreRuntime'),
                    str(ROOT/'src/crossstore/PackageManifest.cpp'), str(TEST/'CatalogTest.cpp'), '-o', binary], check=True)
    manifests = sorted(Path(sys.argv[1]).glob('*/manifest.json')) if len(sys.argv)>1 else []
    subprocess.run([binary, *map(str, manifests)], check=True)
    print('Manifest install/catalog isolation and two-page navigation passed')
