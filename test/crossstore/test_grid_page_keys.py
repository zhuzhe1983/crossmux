#!/usr/bin/env python3
"""Exercise production launcher keys with the real SDK ListNav implementation."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SDK = ROOT / 'freeink-sdk/libs/ui/FreeInkUI'


def method(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


launcher = (ROOT / 'src/activities/apps/AppsMenuActivity.cpp').read_text()
base = (ROOT / 'src/activities/UiListActivity.cpp').read_text()
sdk = (SDK / 'src/FreeInkUI.cpp').read_text()
methods = [method(launcher, signature) for signature in (
    'void AppsMenuActivity::guardGridButtons()',
    'bool AppsMenuActivity::handleCustomInput()',
    'void AppsMenuActivity::turnIconPage(',
    'void AppsMenuActivity::selectMainTabContentEdge(',
    'bool AppsMenuActivity::handleListSwipe(')]
methods += [method(base, signature) for signature in (
    'void UiListActivity::syncPagedViewport(',
    'bool UiListActivity::handleListSwipe(')]
helpers = [method(sdk, signature) for signature in (
    'uint16_t listVisibleRows(', 'uint16_t listTopIndexFor(')]
with tempfile.TemporaryDirectory(prefix='crossstore-grid-keys-') as directory:
    source = Path(directory) / 'grid.cpp'
    text = Path(__file__).with_name('GridPageKeysHarness.cpp').read_text()
    text = text.replace('// PRODUCTION_METHODS', '\n\n'.join(methods))
    text = text.replace('// SDK_HELPERS', '\n\n'.join(helpers))
    source.write_text(text)
    binary = Path(directory) / 'grid-test'
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fno-exceptions',
                    '-I'+str(SDK/'include'), '-I'+str(ROOT/'src'), str(source),
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=10)
