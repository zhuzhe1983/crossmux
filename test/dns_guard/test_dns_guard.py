"""Compile the production DNS wrapper with IDF's pinned core-dispatch fixture."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def check():
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    flags = (ROOT / "platformio.ini").read_text().split("[base]", 1)[1].split("\n[", 1)[0]
    assert "-Wl,--wrap=dns_clear_cache" in flags, "HAL guard must intercept Arduino's external reference"
    with tempfile.TemporaryDirectory(prefix="hal-dns-guard-") as directory:
        binary = Path(directory) / "check"
        command = compiler + [
            "-std=c++20", "-Wall", "-Wextra", "-Werror", "-Wno-missing-field-initializers",
            "-pthread", "-fno-exceptions", f"-I{HERE / 'stubs'}",
            str(ROOT / "lib/hal/HalDnsGuard.cpp"), str(HERE / "HalDnsGuardTest.cpp"),
            "-o", str(binary),
        ]
        for sanitizers in ([], ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]):
            subprocess.run(command + sanitizers, check=True, timeout=45)
            subprocess.run([str(binary)], check=True, timeout=20)
        # The native simulator has neither esp-netif nor the firmware wrap flag.
        # It must not inherit an unresolved hardware-only compatibility symbol.
        object_file = Path(directory) / "simulator.o"
        subprocess.run(compiler + ["-DSIMULATOR", "-c", str(ROOT / "lib/hal/HalDnsGuard.cpp"),
                                   "-o", str(object_file)], check=True, timeout=20)
        undefined = subprocess.check_output(["nm", "-u", str(object_file)], text=True)
        assert "dns_clear_cache" not in undefined and "esp_netif" not in undefined


if __name__ == "__main__":
    check()
