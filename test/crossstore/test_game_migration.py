#!/usr/bin/env python3
"""Build the actual sandbox; check game rules, saves, AI and layout budgets."""
from pathlib import Path
import subprocess
import tempfile
import sys
ROOT = Path(__file__).resolve().parents[2]
EXCLUDED = {'lua.c', 'luac.c', 'linit.c', 'liolib.c', 'loslib.c', 'loadlib.c', 'ldblib.c', 'lcorolib.c'}
with tempfile.TemporaryDirectory(prefix='crossstore-games-') as directory:
    build = Path(directory)
    lua = ROOT/'lib/CrossStoreLua/src'
    objects=[]
    for source in sorted(lua.glob('*.c')):
        if source.name in EXCLUDED: continue
        obj=build/(source.stem+'.o')
        subprocess.run(['cc','-O1','-DLUAI_MAXCCALLS=50','-c',str(source),'-o',str(obj)],check=True)
        objects.append(str(obj))
    binary=build/'game-test'
    subprocess.run(['c++','-std=c++17','-O1','-fno-exceptions','-Wall','-Wextra','-Werror',
                    '-I'+str(lua),'-I'+str(ROOT/'lib/CrossStoreRuntime'),
                    str(ROOT/'lib/CrossStoreRuntime/CrossStoreRuntime.cpp'),
                    str(Path(__file__).with_name('GameMigrationTest.cpp')),*objects,'-o',str(binary)],check=True)
    subprocess.run([str(binary),sys.argv[1]],check=True,timeout=30)
