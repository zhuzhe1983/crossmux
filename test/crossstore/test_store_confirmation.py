#!/usr/bin/env python3
"""Compile production confirmation and input methods with real SDK UI."""
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

confirm = (ROOT / 'src/activities/apps/appstore/AppStoreConfirm.cpp').read_text()
confirm = '\n'.join(line for line in confirm.splitlines() if not line.startswith('#include'))
grid = (ROOT / 'src/activities/apps/appstore/AppStoreGrid.cpp').read_text()
activity = (ROOT / 'src/activities/apps/appstore/AppStoreActivity.cpp').read_text()
methods = [method(grid, signature) for signature in (
    'void AppStoreActivity::activateFocus()',
    'void AppStoreActivity::guardHeldButtons()',
    'bool AppStoreActivity::consumeHeldButtons()',
    'bool AppStoreActivity::inputActive() const',
    'void AppStoreActivity::loop()')]
methods.append(method(activity, 'void AppStoreActivity::onBackButton()'))
harness = Path(__file__).with_name('StoreConfirmationHarness.cpp').read_text()
with tempfile.TemporaryDirectory(prefix='crossstore-confirmation-') as directory:
    source = Path(directory) / 'confirmation.cpp'
    source.write_text(harness.replace('// PRODUCTION_CONFIRMATION', confirm)
                      .replace('// PRODUCTION_INPUT', '\n\n'.join(methods)))
    binary = Path(directory) / 'store-confirmation-test'
    sdk_object = Path(directory) / 'freeink-ui.o'
    subprocess.run(['c++', '-std=c++17', '-fno-exceptions', '-I' + str(SDK / 'include'),
                    '-c', str(SDK / 'src/FreeInkUI.cpp'), '-o', str(sdk_object)], check=True)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fno-exceptions',
                    '-I' + str(ROOT / 'src'), '-I' + str(SDK / 'include'),
                    str(source), str(sdk_object), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=10)
