"""Resolve the store target's hardware/feature inheritance without running PIO."""

import configparser
import csv
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
TARGET = "env:waveshare_epaper_397_store"


class WaveshareStoreConfigTest(unittest.TestCase):
    def setUp(self):
        self.config = configparser.ConfigParser(interpolation=None)
        self.config.read(ROOT / "platformio.ini")

    def option(self, section, key, visited=()):
        identity = (section, key)
        self.assertNotIn(identity, visited, "Cyclic PlatformIO inheritance")
        visited = (*visited, identity)
        own = self.config[section]
        if key not in own:
            for parent in own.get("extends", "").replace(",", " ").split():
                try:
                    return self.option(parent, key, visited)
                except KeyError:
                    pass
            raise KeyError(identity)
        return re.sub(
            r"\$\{([^{}]+)\}",
            lambda match: self.option(*match[1].split(".", 1), visited),
            own[key],
        )

    def test_uses_native_usb_s3_psram_profile_and_only_matching_board(self):
        self.assertEqual(self.option(TARGET, "board"), "esp32-s3-devkitc1-n16r8")
        self.assertEqual(self.option(TARGET, "board_build.mcu"), "esp32s3")
        self.assertEqual(self.option(TARGET, "board_build.arduino.memory_type"), "dio_opi")
        flags = self.option(TARGET, "build_flags").split()
        self.assertEqual(
            sorted(set(flag for flag in flags if flag.startswith("-DFREEINK_DEVICE_"))),
            ["-DFREEINK_DEVICE_WAVESHARE_EPAPER_397=1"],
        )
        for flag in ("-DBOARD_HAS_PSRAM", "-DFREEINK_CAP_USB_MSC=1", "-DUSE_BLOCK_DEVICE_INTERFACE=1",
                     "-DARDUINO_USB_MODE=1", "-DARDUINO_USB_CDC_ON_BOOT=1"):
            self.assertIn(flag, flags)

    def test_features_require_their_real_native_backends(self):
        flags = self.option(TARGET, "build_flags").split()
        for feature in ("STORE", "TOX_CHAT", "ESP_CHAT"):
            self.assertIn(f"-DCROSSMUX_{feature}=1", flags)
        for unavailable in ("LORA_CHAT", "RADIO"):
            self.assertFalse(any(flag.startswith(f"-DCROSSMUX_{unavailable}=") for flag in flags))
        scripts = self.option(TARGET, "extra_scripts").split()
        for script in ("pre:scripts/configure_chat.py", "post:scripts/build_chat.py",
                       "pre:scripts/configure_nimble_psram.py", "pre:scripts/generate_store_icon.py"):
            self.assertEqual(scripts.count(script), 1)
            self.assertTrue((ROOT / script.split(":", 1)[1]).is_file())

    def test_existing_codec_feedback_is_kept_without_claiming_radio_audio(self):
        self.assertIn("-DCROSSPOINT_CAP_SOUND_FEEDBACK=1", self.option(TARGET, "build_flags").split())
        self.assertIn("AudioManager=symlink://freeink-sdk/libs/hardware/AudioManager",
                      self.option(TARGET, "lib_deps").split())
        for asset in ("assets/sounds/select.wav", "assets/sounds/tap.wav"):
            self.assertIn(asset, self.option(TARGET, "board_build.embed_files").split())
            self.assertTrue((ROOT / asset).is_file())

    def test_version_is_explicit_and_not_dependent_on_git_branch_hook(self):
        versions = [flag for flag in self.option(TARGET, "build_flags").split()
                    if flag.startswith("-DCROSSPOINT_VERSION=")]
        self.assertEqual(versions, [
            '-DCROSSPOINT_VERSION=\\"' + self.config["crosspoint"]["version"] + '-waveshare-epaper-397\\"'
        ])

    def test_flash_offsets_match_the_existing_16_mib_update_contract(self):
        self.assertEqual(self.option(TARGET, "board_build.flash_size"), "16MB")
        self.assertEqual(self.option(TARGET, "board_build.partitions"), "partitions.csv")
        rows = list(csv.reader(line for line in (ROOT / "partitions.csv").read_text().splitlines()
                               if line.strip() and not line.lstrip().startswith("#")))
        regions = {row[0].strip(): (int(row[3].strip(), 0), int(row[4].strip(), 0)) for row in rows}
        self.assertEqual(regions["app0"], (0x10000, 0x640000))
        self.assertEqual(regions["app1"], (0x650000, 0x640000))
        self.assertEqual(max(start + size for start, size in regions.values()), 16 * 1024 * 1024)
        ordered = sorted(regions.values())
        for left, right in zip(ordered, ordered[1:]):
            self.assertLessEqual(left[0] + left[1], right[0])

    def test_standard_targets_keep_their_existing_feature_set(self):
        for section in ("env:waveshare_epaper_397", "env:waveshare_epaper_397_nightly"):
            # Ignore the nightly hash placeholder; it does not affect feature flags.
            direct = self.config[section]["build_flags"]
            self.assertNotIn("CROSSMUX_STORE", direct)
            self.assertNotIn("configure_chat", self.option(section, "extra_scripts"))


if __name__ == "__main__":
    unittest.main()
