"""Exercise the firmware QR encoder and optionally decode with an independent reader.

Build a firmware environment first to obtain its pinned QRCode dependency.
Install pillow and zxing-cpp in a test venv to enable the decode checks.
"""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
HARNESS = r'''
#include "activities/apps/chat/WifiIdentityQr.h"
#include <cstdio>
int main(int argc, char **argv) {
  chat::WifiIdentityQr qr;
  if (argc != 2 || !qr.init(argv[1])) return 2;
  if (qr.code.size != 37 || qr.code.ecc != ECC_MEDIUM) return 3;
  constexpr int quiet = chat::WifiIdentityQr::QUIET_MODULES;
  const int side = qr.code.size + quiet * 2;
  std::printf("P1\n%d %d\n", side, side);
  for (int y = -quiet; y < qr.code.size + quiet; ++y) {
    for (int x = -quiet; x < qr.code.size + quiet; ++x) {
      const bool dark = x >= 0 && y >= 0 && x < qr.code.size && y < qr.code.size &&
                        qrcode_getModule(&qr.code, x, y);
      std::printf("%d ", dark ? 1 : 0);
    }
    std::puts("");
  }
}
'''


class WifiIdentityQr(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        sources = sorted((ROOT / ".pio/libdeps").glob("*/QRCode/src/qrcode.c"))
        if not sources or not shutil.which("cc") or not shutil.which("c++"):
            raise unittest.SkipTest("build a firmware environment to install QRCode; cc/c++ required")
        cls.tmp = tempfile.TemporaryDirectory(prefix="crossmux-identity-qr-")
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.work = pathlib.Path(cls.tmp.name)
        source = sources[0]
        obj = cls.work / "qrcode.o"
        subprocess.run(["cc", "-std=c99", "-c", str(source), "-o", str(obj)], check=True)
        harness = cls.work / "main.cpp"
        harness.write_text(HARNESS)
        cls.binary = cls.work / "identity-qr"
        subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fno-exceptions",
                        "-I", str(ROOT / "src"), "-I", str(source.parent), str(harness),
                        str(obj), "-o", str(cls.binary)], check=True)
        address = bytearray(range(1, 37))
        checksum = [0, 0]
        for i, byte in enumerate(address):
            checksum[i % 2] ^= byte
        address.extend(checksum)
        cls.identity = address.hex().upper()

    def encode(self, identity):
        return subprocess.run([str(self.binary), identity], capture_output=True, check=False)

    def test_complete_address_fits_and_invalid_id_fails_closed(self):
        result = self.encode(self.identity)
        self.assertEqual(result.returncode, 0)
        self.assertTrue(result.stdout.startswith(b"P1\n45 45\n"))
        for identity in ("", self.identity[:64], "0" * 76, "G" + self.identity[1:],
                         self.identity[:-2] + "FF", self.identity + "0"):
            self.assertEqual(self.encode(identity).returncode, 2)

    def test_independent_decoder_recovers_exact_tox_uri_at_device_scales(self):
        try:
            from PIL import Image
            import zxingcpp
        except ImportError:
            self.skipTest("install pillow and zxing-cpp for independent QR decoding")
        bitmap = self.work / "identity.pbm"
        result = self.encode(self.identity)
        self.assertEqual(result.returncode, 0)
        bitmap.write_bytes(result.stdout)
        with Image.open(bitmap) as original:
            for scale in (2, 4, 6, 10):
                with self.subTest(scale=scale):
                    image = original.convert("L").resize((45 * scale, 45 * scale), Image.Resampling.NEAREST)
                    codes = zxingcpp.read_barcodes(image)
                    self.assertEqual(len(codes), 1)
                    self.assertEqual(codes[0].text, "tox:" + self.identity)


if __name__ == "__main__":
    unittest.main()
