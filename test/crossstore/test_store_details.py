#!/usr/bin/env python3
"""Exercise production details methods against the real FreeInkUI renderer."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SDK = ROOT / 'freeink-sdk/libs/ui/FreeInkUI'
details = (ROOT / 'src/activities/apps/appstore/AppStoreDetails.cpp').read_text()
details = '\n'.join(line for line in details.splitlines() if not line.startswith('#include'))
harness = Path(__file__).with_name('StoreDetailsHarness.cpp').read_text()
with tempfile.TemporaryDirectory(prefix='crossstore-details-') as directory:
    source = Path(directory) / 'details.cpp'
    source.write_text(harness.replace('// PRODUCTION_DETAILS', details))
    binary = Path(directory) / 'store-details-test'
    sdk_object = Path(directory) / 'freeink-ui.o'
    subprocess.run(['c++', '-std=c++17', '-fno-exceptions', '-I' + str(SDK / 'include'),
                    '-c', str(SDK / 'src/FreeInkUI.cpp'), '-o', str(sdk_object)], check=True)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fno-exceptions',
                    '-I' + str(ROOT / 'src'), '-I' + str(SDK / 'include'),
                    str(source), str(sdk_object), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=10)
