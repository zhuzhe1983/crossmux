#!/usr/bin/env python3
"""Compile the actual visible-cover cache and Bitmap reader, never real storage."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='weread-covers-') as temporary:
    binary = Path(temporary) / 'covers'
    subprocess.run(['c++', '-std=c++20', '-O1', '-fno-exceptions', '-Wall', '-Wextra', '-Werror',
                    '-I'+str(HERE/'stubs'), '-I'+str(ROOT/'src'), '-I'+str(ROOT/'lib/GfxRenderer'),
                    '-I'+str(ROOT/'lib/Memory'), str(HERE/'CoversTest.cpp'),
                    str(ROOT/'src/activities/apps/appstore/LuaWeReadCovers.cpp'),
                    str(ROOT/'lib/GfxRenderer/Bitmap.cpp'), str(ROOT/'lib/GfxRenderer/BitmapHelpers.cpp'),
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=15)
