"""Exercise the pinned Tox RNG against the colliding ESP Wi-Fi C ABI."""

import os
from pathlib import Path
import runpy
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
TOX = ROOT / ".cache/chat-deps/c-toxcore-0.2.23"


class BuildEnvironment:
    def __init__(self):
        self.defines = []
        self.library_environment = None

    def subst(self, value):
        return value.replace("$PROJECT_DIR", str(ROOT)).replace("$BUILD_DIR", "unused")

    def Clone(self):
        self.library_environment = BuildEnvironment()
        return self.library_environment

    def AppendUnique(self, **kwargs):
        self.defines.extend(kwargs.get("CPPDEFINES", []))

    def Append(self, **kwargs):
        pass

    def BuildLibrary(self, *args, **kwargs):
        return "unused"


class ToxLinkageTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not (TOX / "toxcore/os_random.c").exists():
            raise unittest.SkipTest("Pinned Tox sources unavailable; build papermono_chat first")
        cls.compiler = shutil.which(os.environ.get("CC", "cc"))
        cls.archiver = shutil.which("ar")
        if not cls.compiler or not cls.archiver:
            raise unittest.SkipTest("Host C compiler and ar required")

    def test_wifi_and_tox_random_abis_coexist(self):
        env = BuildEnvironment()
        runpy.run_path(str(ROOT / "scripts/build_chat.py"), init_globals={"env": env, "Import": lambda _: None})
        self.assertEqual(env.defines, [], "Tox aliases must not change the Wi-Fi build environment")
        definitions = [f"-D{name}={value}" for name, value in env.library_environment.defines]
        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            # Deterministic entropy only in this host linkage fixture. Production
            # still compiles the pinned os_random.c against ESP-IDF libsodium.
            (tmp / "sodium.h").write_text("""
#include <stdint.h>
#include <string.h>
static inline int sodium_init(void) { return 0; }
static inline void randombytes(unsigned char *out, unsigned long long size) { memset(out, 0x5a, size); }
static inline uint32_t randombytes_uniform(uint32_t upper) { return upper ? 0 : 0; }
""")
            (tmp / "wifi.c").write_text("unsigned long os_random(void) { return 0x12345678UL; }\n")
            (tmp / "tox_entry.c").write_text("""
#include "os_random.h"
int tox_rng_probe(void) {
    const Random *rng = os_random();
    if ((uintptr_t)rng == 0x12345678UL) return 1;
    if (!rng) return 2;
    uint8_t bytes[32] = {0};
    rng_bytes(rng, bytes, sizeof(bytes));
    for (unsigned i = 0; i < sizeof(bytes); ++i) if (bytes[i] != 0x5a) return 2;
    return 0;
}
""")
            (tmp / "main.c").write_text("""
unsigned long os_random(void);
int tox_rng_probe(void);
int main(void) {
    if (os_random() != 0x12345678UL) return 3;
    return tox_rng_probe();
}
""")

            def run(*args):
                result = subprocess.run(args, cwd=tmp, capture_output=True, text=True, check=False)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                return result

            common = [self.compiler, "-std=c11", f"-I{tmp}", f"-I{TOX / 'toxcore'}"]
            run(*common, "-c", "wifi.c", "-o", "wifi.o")
            run(*common, "-c", "main.c", "-o", "main.o")
            run(self.archiver, "rcs", "libwifi.a", "wifi.o")
            for name, flags in (("original", []), ("isolated", definitions)):
                objects = []
                for source in (TOX / "toxcore/os_random.c", TOX / "toxcore/rng.c", tmp / "tox_entry.c"):
                    obj = f"{name}-{source.stem}.o"
                    run(*common, *flags, "-c", str(source), "-o", obj)
                    objects.append(obj)
                archive = f"libtox-{name}.a"
                run(self.archiver, "rcs", archive, *objects)
                run(self.compiler, "main.o", "libwifi.a", archive, "-o", name)
                result = subprocess.run([str(tmp / name)], check=False)
                self.assertEqual(result.returncode, 1 if name == "original" else 0,
                                 "Unisolated RNG must reproduce the collision; isolated RNG must preserve both ABIs")


if __name__ == "__main__":
    unittest.main()
