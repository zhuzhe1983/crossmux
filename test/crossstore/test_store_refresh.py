#!/usr/bin/env python3
"""Compile production refresh/status/icon methods against bounded fake I/O."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def method(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


activity = (ROOT / 'src/activities/apps/appstore/AppStoreActivity.cpp').read_text()
network = (ROOT / 'src/activities/apps/appstore/AppStoreNetwork.cpp').read_text()
methods = [method(activity, signature) for signature in (
    'void AppStoreActivity::loadIcons()', 'bool AppStoreActivity::refreshIcons()')]
methods += [method(network, signature) for signature in (
    'void AppStoreActivity::pumpRefresh()', 'bool AppStoreActivity::updateStatus()')]
with tempfile.TemporaryDirectory(prefix='crossstore-refresh-') as directory:
    source = Path(directory) / 'test.cpp'
    source.write_text(Path(__file__).with_name('StoreRefreshHarness.cpp').read_text().replace(
        '// PRODUCTION_METHODS', '\n\n'.join(methods)))
    binary = Path(directory) / 'test'
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fno-exceptions',
                    str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=10)
