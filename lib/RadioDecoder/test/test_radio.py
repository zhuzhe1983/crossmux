#!/usr/bin/env python3
"""Test real MP3 decoding, stream boundaries and duration-preserving mono conversion."""
from pathlib import Path
import shutil
import subprocess
import tempfile

from test_atomic_codegen import check as check_atomic_codegen
from test_loudness import check as check_loudness
from test_buzzer_profile import check as check_buzzer_profile

ROOT = Path(__file__).resolve().parents[3]
LIB = ROOT / 'lib/RadioDecoder'
ffmpeg = shutil.which('ffmpeg')
check_atomic_codegen()
check_loudness()
check_buzzer_profile()
with tempfile.TemporaryDirectory(prefix='crossmux-radio-') as directory:
    temp = Path(directory)
    binary = temp / 'radio-test'
    subprocess.run([
        'c++', '-std=c++20', '-O2', '-fno-exceptions', '-DCROSSMUX_RADIO=1', '-pthread',
        '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
        '-I' + str(LIB), '-I' + str(ROOT / 'src'),
        str(LIB / 'RadioDecoder.cpp'), str(LIB / 'MonoResampler.cpp'), str(LIB / 'PcmQueue.cpp'),
        str(ROOT / 'src/services/radio/StreamFormat.cpp'), str(LIB / 'test/RadioTest.cpp'),
        '-o', str(binary)], check=True, timeout=60)
    fixtures = []
    if ffmpeg:
        for rate in (8000, 44100, 48000):
            output = temp / f'tone-{rate}.mp3'
            subprocess.run([ffmpeg, '-v', 'error', '-f', 'lavfi', '-i',
                            f'sine=frequency=997:sample_rate={rate}:duration=1',
                            '-ac', '2', '-c:a', 'libmp3lame', '-b:a', '32k' if rate == 8000 else '128k',
                            '-write_xing', '0', '-id3v2_version', '0', str(output)],
                           check=True, timeout=20)
            fixtures.append(str(output))
    else:
        print('ffmpeg unavailable: generated MP3 fixture cases skipped; parser/resampler cases still run')
    subprocess.run([str(binary), *fixtures], check=True, timeout=30)
    if fixtures:
        long_fixture = temp / 'tone-long.mp3'
        subprocess.run([ffmpeg, '-v', 'error', '-f', 'lavfi', '-i',
                        'sine=frequency=997:sample_rate=48000:duration=4',
                        '-ac', '2', '-c:a', 'libmp3lame', '-b:a', '128k',
                        '-write_xing', '0', '-id3v2_version', '0', str(long_fixture)],
                       check=True, timeout=20)
        player = temp / 'player-test'
        subprocess.run([
            'c++', '-std=c++20', '-O2', '-fno-exceptions', '-DCROSSMUX_RADIO=1', '-pthread',
            '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
            '-I' + str(LIB / 'test/stubs'), '-I' + str(LIB), '-I' + str(ROOT / 'src'),
            str(LIB / 'RadioDecoder.cpp'), str(LIB / 'MonoResampler.cpp'), str(LIB / 'PcmQueue.cpp'),
            str(LIB / 'SpeakerLoudness.cpp'),
            str(ROOT / 'src/services/radio/StreamFormat.cpp'), str(ROOT / 'src/services/radio/Player.cpp'),
            str(LIB / 'test/PlayerTest.cpp'), '-o', str(player)], check=True, timeout=60)
        subprocess.run([str(player), fixtures[-1], str(long_fixture)], check=True, timeout=60)
