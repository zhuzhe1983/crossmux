#!/usr/bin/env python3
"""Compile the actual embedded VM against its pinned Lua sources on the host."""
from pathlib import Path
import subprocess
import tempfile
import sys

ROOT = Path(__file__).resolve().parents[2]
EXCLUDED = {'lua.c', 'luac.c', 'linit.c', 'liolib.c', 'loslib.c', 'loadlib.c', 'ldblib.c', 'lcorolib.c'}

def main():
    lua = ROOT / 'lib/CrossStoreLua/src'
    runtime = ROOT / 'lib/CrossStoreRuntime'
    with tempfile.TemporaryDirectory(prefix='crossstore-runtime-') as directory:
        build = Path(directory)
        objects = []
        for source in sorted(lua.glob('*.c')):
            if source.name in EXCLUDED:
                continue
            obj = build / (source.stem + '.o')
            subprocess.run(['cc', '-O1', '-DLUAI_MAXCCALLS=50', '-c', str(source), '-o', str(obj)], check=True)
            objects.append(str(obj))
        binary = build / 'runtime-test'
        subprocess.run(['c++', '-std=c++17', '-O1', '-fno-exceptions', '-Wall', '-Wextra', '-Werror',
                        '-I'+str(lua), '-I'+str(runtime), '-I'+str(ROOT/'src'), '-I'+str(ROOT/'lib/hal'),
                        '-DENABLE_CHINESE_VERSION', '-Wno-unused-function',
                        str(ROOT/'src/activities/apps/standby/ChineseAlmanac.cpp'),
                        str(ROOT/'src/activities/apps/buddy/BuddyGenerator.cpp'), str(runtime/'CrossStoreRuntime.cpp'),
                        str(Path(__file__).with_name('RuntimeTest.cpp')), *objects, '-o', str(binary)], check=True)
        subprocess.run([str(binary), *sys.argv[1:]], check=True, timeout=10)
        commit_test = build / 'commit-test'
        subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I'+str(ROOT/'src'),
                        str(Path(__file__).with_name('RecoverableFileTest.cpp')), '-o', str(commit_test)], check=True)
        subprocess.run([str(commit_test)], check=True, timeout=10)

if __name__ == '__main__':
    main()
