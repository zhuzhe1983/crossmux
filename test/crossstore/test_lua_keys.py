#!/usr/bin/env python3
"""Compile production VM/input methods; CROSSSTORE_SANITIZERS='' disables ASan/UBSan."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
EXCLUDED = {'lua.c', 'luac.c', 'linit.c', 'liolib.c', 'loslib.c', 'loadlib.c', 'ldblib.c', 'lcorolib.c'}


def method(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def check():
    lua = ROOT / 'lib/CrossStoreLua/src'
    runtime = ROOT / 'lib/CrossStoreRuntime'
    sanitizers = os.environ.get('CROSSSTORE_SANITIZERS', 'address,undefined')
    sanitizer_flags = ['-fsanitize=' + sanitizers, '-fno-omit-frame-pointer'] if sanitizers else []
    source = (ROOT / 'src/activities/apps/appstore/LuaAppActivity.cpp').read_text()
    bodies = '\n\n'.join(method(source, signature) for signature in (
        'void LuaAppActivity::onEnter()', 'void LuaAppActivity::showError(',
        'void LuaAppActivity::dispatch(', 'void LuaAppActivity::guardHeldButtons()',
        'bool LuaAppActivity::handleKeyInput()', 'void LuaAppActivity::loop()'))
    with tempfile.TemporaryDirectory(prefix='crossstore-key-input-') as directory:
        build = Path(directory)
        objects = []
        for src in sorted(lua.glob('*.c')):
            if src.name in EXCLUDED:
                continue
            obj = build / (src.stem + '.o')
            subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + [
                '-O1', '-DLUAI_MAXCCALLS=50', *sanitizer_flags,
                '-c', str(src), '-o', str(obj)], check=True, timeout=30)
            objects.append(str(obj))
        harness = build / 'keys.cpp'
        harness.write_text((HERE / 'LuaKeyInputHarness.cpp').read_text().replace('// PRODUCTION_METHODS', bodies))
        binary = build / 'key-input-test'
        subprocess.run(shlex.split(os.environ.get('CXX', 'c++')) + [
            '-std=c++17', '-O1', '-fno-exceptions', '-Wall', '-Wextra', '-Werror',
            *sanitizer_flags,
            '-I' + str(lua), '-I' + str(runtime), str(runtime / 'CrossStoreRuntime.cpp'),
            str(harness), *objects, '-o', str(binary)], check=True, timeout=45)
        subprocess.run([str(binary)], check=True, timeout=20)


if __name__ == '__main__':
    check()
