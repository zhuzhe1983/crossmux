#!/usr/bin/env python3
"""Exercise production store geometry and semantic update markers on the host."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def method(source, signature):
    """Compile actual method bodies, replacing only platform collaborators."""
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

with tempfile.TemporaryDirectory(prefix='crossstore-grid-') as directory:
    binary = Path(directory) / 'store-grid-test'
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fno-exceptions',
                    '-I' + str(ROOT / 'src'), str(Path(__file__).with_name('StoreGridTest.cpp')),
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=10)
    grid = (ROOT / 'src/activities/apps/appstore/AppStoreGrid.cpp').read_text()
    network = (ROOT / 'src/activities/apps/appstore/AppStoreNetwork.cpp').read_text()
    methods = [method(grid, signature) for signature in (
        'void AppStoreActivity::guardHeldButtons()',
        'bool AppStoreActivity::consumeHeldButtons()',
        'bool AppStoreActivity::inputActive() const',
        'void AppStoreActivity::loop()')]
    methods.append(method(network, 'void AppStoreActivity::pumpRefresh()'))
    harness = Path(__file__).with_name('StoreInputHarness.cpp').read_text()
    source = Path(directory) / 'input.cpp'
    source.write_text(harness.replace('// PRODUCTION_METHODS', '\n\n'.join(methods)))
    binary = Path(directory) / 'store-input-test'
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fno-exceptions',
                    str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=10)
