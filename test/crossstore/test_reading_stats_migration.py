#!/usr/bin/env python3
"""Exercise reading history queries and the actual bounded Lua VM, without SD writes."""
from pathlib import Path
import subprocess
import tempfile
import sys

ROOT = Path(__file__).resolve().parents[2]
EXCLUDED = {'lua.c', 'luac.c', 'linit.c', 'liolib.c', 'loslib.c', 'loadlib.c', 'ldblib.c', 'lcorolib.c'}

with tempfile.TemporaryDirectory(prefix='reading-stats-migration-') as directory:
    build = Path(directory)
    lua = ROOT / 'lib/CrossStoreLua/src'
    runtime = ROOT / 'lib/CrossStoreRuntime'
    objects = []
    for source in sorted(lua.glob('*.c')):
        if source.name in EXCLUDED:
            continue
        obj = build / (source.stem + '.o')
        subprocess.run(['cc', '-O1', '-DLUAI_MAXCCALLS=50', '-c', str(source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    binary = build / 'reading-stats-test'
    subprocess.run(['c++', '-std=c++17', '-O1', '-fno-exceptions', '-Wall', '-Wextra', '-Werror',
                    '-I'+str(lua), '-I'+str(runtime), '-I'+str(ROOT/'src'),
                    str(runtime/'CrossStoreRuntime.cpp'),
                    str(Path(__file__).with_name('ReadingStatsMigrationTest.cpp')),
                    *objects, '-o', str(binary)], check=True)
    source = sys.argv[1] if len(sys.argv)>1 else str(ROOT.parent/'CrossStore/packages/reading_stats/main.lua')
    subprocess.run([str(binary), source], check=True, timeout=10)
