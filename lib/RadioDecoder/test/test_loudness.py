#!/usr/bin/env python3
"""Exercise the production speaker DSP without firmware, SDK, or an audio device."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile


def check():
    source = Path(__file__).resolve().parent.parent
    with tempfile.TemporaryDirectory(prefix="crossmux-loudness-") as temporary:
        executable = Path(temporary) / "speaker-loudness-test"
        subprocess.run(
            shlex.split(os.environ.get("CXX", "c++"))
            + [
                "-std=c++17",
                "-DCROSSMUX_RADIO=1",
                "-fno-exceptions",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fsanitize=address,undefined",
                "-fno-omit-frame-pointer",
                "-I",
                str(source),
                str(source / "SpeakerLoudness.cpp"),
                str(source / "test" / "SpeakerLoudnessTest.cpp"),
                "-o",
                str(executable),
            ],
            check=True,
        )
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    check()
