#!/usr/bin/env python3
"""Exercise the production cache owner and WeRead package with isolated fake storage/services."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
EXCLUDED = {'lua.c', 'luac.c', 'linit.c', 'liolib.c', 'loslib.c', 'loadlib.c', 'ldblib.c', 'lcorolib.c'}


def main():
    package = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT.parent / 'CrossStore/packages/weread/main.lua'
    with tempfile.TemporaryDirectory(prefix='crossstore-weread-cache-') as directory:
        build = Path(directory)
        binary = build / 'cache-test'
        subprocess.run(['c++', '-std=c++17', '-fno-exceptions', '-Wall', '-Wextra', '-Werror',
                        '-I'+str(HERE/'stubs'), '-I'+str(ROOT/'src'),
                        str(HERE/'CacheTest.cpp'), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=10)
        lua = ROOT / 'lib/CrossStoreLua/src'
        objects = []
        for source in sorted(lua.glob('*.c')):
            if source.name in EXCLUDED:
                continue
            obj = build / (source.stem + '.o')
            subprocess.run(['cc', '-O1', '-DLUAI_MAXCCALLS=50', '-c', str(source), '-o', str(obj)], check=True)
            objects.append(str(obj))
        binary = build / 'package-test'
        runtime = ROOT / 'lib/CrossStoreRuntime'
        subprocess.run(['c++', '-std=c++17', '-O1', '-fno-exceptions', '-Wall', '-Wextra', '-Werror',
                        '-I'+str(lua), '-I'+str(runtime), str(runtime/'CrossStoreRuntime.cpp'),
                        str(HERE/'PackageTest.cpp'), *objects, '-o', str(binary)], check=True)
        subprocess.run([str(binary), str(package)], check=True, timeout=10)


if __name__ == '__main__':
    main()
