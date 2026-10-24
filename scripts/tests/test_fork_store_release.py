import configparser
import hashlib
import json
import re
import shutil
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import fork_store_release as release
import nightly_targets

SHA = 'a' * 40
SDK_SHA = 'b' * 40


def image(board=None, version=None, chip=9):
    raw = bytearray(256)
    raw[0] = 0xE9
    raw[2:4] = b'\x02\x4f'
    raw[12:14] = chip.to_bytes(2, 'little')
    if board:
        raw.extend(f'CROSSPOINT-BOARD-V1:{board};'.encode())
    if version:
        raw.extend(version.encode() + b'\0')
    return bytes(raw)


def partitions():
    entries = ((1, 2, 0x9000, 0x5000, 'nvs'), (1, 0, 0xe000, 0x2000, 'otadata'),
               (0, 0x10, 0x10000, 0x640000, 'app0'), (0, 0x11, 0x650000, 0x640000, 'app1'),
               (1, 0x82, 0xc90000, 0x360000, 'spiffs'), (1, 3, 0xff0000, 0x10000, 'coredump'))
    raw = b''.join(struct.pack('<HBBII16sI', 0x50aa, kind, sub, off, size, name.encode(), 0)
                   for kind, sub, off, size, name in entries)
    raw += b'\xeb\xeb' + b'\xff' * 14 + hashlib.md5(raw).digest()
    return raw.ljust(3072, b'\xff')


class ForkStoreReleaseTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='fork-store-release-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.sdk = self.root / 'freeink-sdk'
        self.sdk.mkdir()
        shutil.copyfile(ROOT / 'partitions.csv', self.root / 'partitions.csv')
        config = ('[crosspoint]\nversion=1.6.5\n[base]\nboard_build.partitions=partitions.csv\n'
                  'board_build.flash_size=16MB\nboard_build.flash_mode=dio\nboard_build.mcu=esp32s3\n'
                  'build_flags=-DCROSSMUX_STORE=1\n')
        for target in release.TARGETS.values():
            config += (f"[env:{target['environment']}]\nextends=base\n"
                       f"board_build.flash_mode={target['flashMode']}\nbuild_flags=\n"
                       ' ${base.build_flags}\n'
                       f" -DCROSSPOINT_VERSION=\\\"${{crosspoint.version}}-{target['deviceSlug']}\\\"\n")
            build = self.root / '.pio/build' / target['environment']
            build.mkdir(parents=True)
            (build / 'firmware.bin').write_bytes(image(target['boardTag'], '1.6.5-' + target['deviceSlug']))
            (build / 'bootloader.bin').write_bytes(image())
            (build / 'partitions.bin').write_bytes(partitions())
        (self.root / 'platformio.ini').write_text(config)
        self.boot_app0 = self.root / '.pio/boot_app0.bin'
        self.boot_app0.write_bytes(b'\xff' * 8192)

    def git(self, folder, *args):
        if args == ('rev-parse', 'HEAD'):
            return SDK_SHA if folder == self.sdk else SHA
        if args == ('ls-tree', 'HEAD', 'freeink-sdk'):
            return f'160000 commit {SDK_SHA}\tfreeink-sdk'
        if args[:2] == ('status', '--porcelain'):
            return ''
        raise AssertionError((folder, args))

    def package(self, target_id, output=None):
        with mock.patch.object(release, 'git_value', side_effect=self.git), mock.patch.object(
                release, 'find_boot_app0', return_value=self.boot_app0):
            return release.package(self.root, target_id, SHA, output or self.root / target_id)

    def complete(self):
        output = self.root / 'complete'
        output.mkdir()
        for key in release.TARGETS:
            self.package(key)
            for path in (self.root / key).iterdir():
                shutil.copyfile(path, output / path.name)
        return output

    def test_fixed_three_store_targets_preserve_official_channels(self):
        self.assertEqual([v['environment'] for v in release.matrix()['include']],
                         ['papermono_store', 'papers3_store', 'waveshare_epaper_397_store'])
        self.assertNotIn('m5stack_papers3', nightly_targets.TARGETS)
        self.assertEqual(set(nightly_targets.targets_for('stable')), {'xteink_x4', 'sticky'})
        self.assertEqual(release.TARGETS['m5stack_papers3']['models'], ['m5paper_s3'])
        config = configparser.ConfigParser(interpolation=None)
        config.read(ROOT / 'platformio.ini')
        for target in release.TARGETS.values():
            self.assertTrue(release.target_version(ROOT, target).startswith(config['crosspoint']['version'] + '-'))

    def test_source_sha_gitlink_and_clean_worktrees_required(self):
        with mock.patch.object(release, 'git_value', side_effect=self.git):
            self.assertEqual(release.preflight(self.root, SHA), SDK_SHA)
            with self.assertRaises(ValueError): release.preflight(self.root, 'c' * 40)
            with self.assertRaises(ValueError): release.preflight(self.root, '$(touch injected)')
        for folder in (self.root, self.sdk):
            def dirty(path, *args):
                return ' M private.cpp' if path == folder and args[0] == 'status' else self.git(path, *args)
            with mock.patch.object(release, 'git_value', side_effect=dirty):
                with self.assertRaises(ValueError): release.preflight(self.root, SHA)
        def wrong_link(path, *args):
            return f'160000 commit {"c" * 40}\tfreeink-sdk' if args[0] == 'ls-tree' else self.git(path, *args)
        with mock.patch.object(release, 'git_value', side_effect=wrong_link):
            with self.assertRaises(ValueError): release.preflight(self.root, SHA)
        (self.root / 'platformio.local.ini').write_text('[env:papermono_store]\nbuild_flags=-DSECRET=1\n')
        with mock.patch.object(release, 'git_value', side_effect=self.git):
            with self.assertRaises(ValueError): release.preflight(self.root, SHA)

    def test_only_new_exact_source_build_tags_or_dispatch_can_start_release(self):
        payload = {'created': True, 'deleted': False, 'forced': False}
        ref = 'refs/tags/crossstore-build-' + SHA
        release.validate_trigger('push', ref, SHA, payload)
        release.validate_trigger('workflow_dispatch', 'refs/heads/codex/crossstore-apps', SHA, {})
        release.validate_trigger('workflow_dispatch', ref, SHA, {})
        for event, bad_ref, bad_sha, bad_payload in (
                ('push', ref, 'c' * 40, payload),
                ('push', 'refs/heads/crossstore-build-' + SHA, SHA, payload),
                ('push', 'refs/tags/crossstore-' + SHA + '-123-1', SHA, payload),
                ('push', ref + '-extra', SHA, payload),
                ('push', ref, SHA, {**payload, 'created': False}),
                ('push', ref, SHA, {**payload, 'deleted': True}),
                ('push', ref, SHA, {**payload, 'forced': True}),
                ('push', ref, SHA, {**payload, 'created': 'true'}),
                ('push', ref, SHA, {}),
                ('pull_request', ref, SHA, payload),
                ('workflow_dispatch', 'refs/pull/1/merge', SHA, {})):
            with self.subTest(event=event, ref=bad_ref, payload=bad_payload):
                with self.assertRaises(ValueError):
                    release.validate_trigger(event, bad_ref, bad_sha, bad_payload)

    def test_store_flag_and_explicit_version_required(self):
        config = self.root / 'platformio.ini'
        original = config.read_text()
        for before, after in (('CROSSMUX_STORE=1', 'CROSSMUX_STORE=0'),
                              ('CROSSPOINT_VERSION', 'OTHER_VERSION'),
                              ('16MB', '8MB'), ('esp32s3', 'esp32c3')):
            config.write_text(original.replace(before, after))
            with self.assertRaises(ValueError):
                release.target_version(self.root, next(iter(release.TARGETS.values())))
        config.write_text(original)

    def test_fresh_checkout_has_all_store_hooks_local_libraries_and_icon_inputs(self):
        config = configparser.ConfigParser(interpolation=None, inline_comment_prefixes=(';',))
        config.read(ROOT / 'platformio.ini')
        for target in release.TARGETS.values():
            section = 'env:' + target['environment']
            hooks = release.option(config, section, 'extra_scripts').split()
            self.assertIn('pre:scripts/configure_chat.py', hooks)
            self.assertIn('post:scripts/build_chat.py', hooks)
            self.assertIn('pre:scripts/generate_store_icon.py', hooks)
            self.assertIn('pre:scripts/generate_woodfish_audio.py', hooks)
            for hook in hooks:
                path = hook.split(':', 1)[-1]
                self.assertFalse(Path(path).is_absolute())
                self.assertNotIn('..', Path(path).parts)
                self.assertTrue((ROOT / path).is_file(), path)
            for dependency in release.option(config, section, 'lib_deps').splitlines():
                if 'symlink://' not in dependency:
                    continue
                path = dependency.split('symlink://', 1)[1].strip()
                self.assertTrue(path.startswith('freeink-sdk/'), path)
                self.assertNotIn('..', Path(path).parts)
                self.assertTrue((ROOT / path).is_dir(), path)
        for path in ('lib/CrossStoreLua/src/lapi.c', 'lib/CrossStoreLua/LICENSE',
                     'lib/RadioDecoder/vendor/minimp3.h', 'lib/RadioDecoder/vendor/LICENSE',
                     'freeink-sdk/libs/hardware/Buzzer/include/BuzzerPcm.h'):
            self.assertTrue((ROOT / path).is_file(), path)
        source = ROOT / 'src/components/icons/SemanticAppIcons.cpp'
        headers = re.findall(r'#include "([^"]+/icons\.h)"', source.read_text())
        self.assertEqual(len(headers), 6)
        for header in headers:
            self.assertTrue((source.parent / header).resolve().is_file(), header)
        icon = (ROOT / 'assets/app-icons/appstore/icon.csi').read_bytes()
        self.assertEqual(len(icon), 7048)
        self.assertEqual(icon[:8], b'CSI1\0\0\0\0')

    def test_complete_package_offsets_checksums_and_merged_bytes(self):
        output = self.complete()
        summary = release.verify_set(output, SHA, SDK_SHA)
        self.assertEqual(len(summary['targets']), 3)
        self.assertEqual(len(list(output.iterdir())), 21)
        release.json_write(output / 'crossstore-release.json', summary)
        self.assertEqual(release.verify_set(output, SHA, SDK_SHA, aggregated=True), summary)
        for target in release.TARGETS.values():
            slug = target['deviceSlug']
            full = (output / f'{slug}-full-install.bin').read_bytes()
            self.assertEqual(full[0x10000:], (output / f'{slug}-firmware.bin').read_bytes())
            self.assertEqual(full[0x9000:0xe000], b'\xff' * 0x5000)

    def test_reject_stale_output_symlink_wrong_chip_board_and_slot_overflow(self):
        key = 'm5stack_paper_mono'
        output = self.root / 'stale'
        output.mkdir()
        (output / 'old.bin').write_bytes(b'old')
        with self.assertRaises(ValueError): self.package(key, output)
        firmware = self.root / '.pio/build/papermono_store/firmware.bin'
        for number, raw in enumerate((image('papers3', '1.6.5-papermono'),
                                     image('papermono', '1.6.5-papermono', chip=5),
                                     image('papermono', 'wrong-version'),
                                     image('papermono', '1.6.5-papermono') + b'x' * 0x640000)):
            firmware.write_bytes(raw)
            with self.assertRaises((ValueError, SystemExit)):
                self.package(key, self.root / f'invalid-{number}')
        firmware.unlink()
        firmware.symlink_to(self.boot_app0)
        with self.assertRaises(ValueError): self.package(key, self.root / 'symlink')

    def test_binary_partition_digest_and_layout_are_verified(self):
        path = self.root / '.pio/build/papermono_store/partitions.bin'
        raw = bytearray(partitions())
        struct.pack_into('<I', raw, 64 + 8, 0x630000)  # Fresh MD5, but wrong app-slot size.
        raw[192 + 16:224] = hashlib.md5(raw[:192]).digest()
        path.write_bytes(raw)
        with self.assertRaisesRegex(ValueError, 'wrong app0'): release.verify_partition_binary(path)
        raw = bytearray(partitions())
        raw[192 + 16] ^= 1
        path.write_bytes(raw)
        with self.assertRaisesRegex(ValueError, 'MD5'): release.verify_partition_binary(path)

    def test_all_targets_same_sha_no_extra_or_traversal_assets(self):
        output = self.complete()
        extra = output / 'secret.txt'
        extra.write_text('not a firmware artifact')
        with self.assertRaises(ValueError): release.verify_set(output, SHA, SDK_SHA)
        extra.unlink()
        manifest_path = output / 'papers3-install-manifest.json'
        original = manifest_path.read_text()
        manifest = json.loads(original)
        manifest['crossmuxSha'] = 'c' * 40
        release.json_write(manifest_path, manifest)
        with self.assertRaisesRegex(ValueError, 'crossmuxSha'): release.verify_set(output, SHA, SDK_SHA)
        manifest = json.loads(original)
        manifest['assets'][0]['name'] = '../secret.txt'
        release.json_write(manifest_path, manifest)
        with self.assertRaisesRegex(ValueError, 'name/offset'): release.verify_set(output, SHA, SDK_SHA)
        manifest_path.unlink()
        with self.assertRaises(ValueError): release.verify_set(output, SHA, SDK_SHA)

    def test_merge_and_sha_corruption_are_rejected(self):
        output = self.complete()
        manifest_path = output / 'papers3-install-manifest.json'
        manifest = json.loads(manifest_path.read_text())
        full = output / 'papers3-full-install.bin'
        raw = bytearray(full.read_bytes())
        raw[0x9000] = 0
        full.write_bytes(raw)
        with self.assertRaisesRegex(ValueError, 'SHA-256'): release.verify_set(output, SHA, SDK_SHA)
        manifest['assets'][-1]['sha256'] = release.sha256(full)
        release.json_write(manifest_path, manifest)
        with self.assertRaisesRegex(ValueError, 'gap differs'): release.verify_set(output, SHA, SDK_SHA)

    def test_manual_workflow_is_sha_pinned_and_publish_is_gated(self):
        workflow = (ROOT / '.github/workflows/fork-store-release.yml').read_text()
        self.assertIn('workflow_dispatch:', workflow)
        self.assertNotIn('pull_request', workflow)
        self.assertIn("tags:\n      - 'crossstore-build-*'", workflow)
        self.assertNotIn('branches:', workflow)
        self.assertIn('trigger --event "$GITHUB_EVENT_NAME"', workflow)
        self.assertIn('--event-file "$GITHUB_EVENT_PATH"', workflow)
        self.assertNotIn('self-hosted', workflow)
        self.assertNotIn('COS_', workflow)
        self.assertIn("if: github.repository == 'zhuzhe1983/crossmux'", workflow)
        self.assertEqual(workflow.count('ref: ${{ github.sha }}'), 3)
        self.assertEqual(workflow.count('persist-credentials: false'), 3)
        publish = workflow.split('  publish:\n', 1)[1]
        self.assertIn('needs: [prepare, build]', publish)
        self.assertIn("if: github.event_name == 'push' || inputs.publish_release", publish)
        self.assertIn('contents: write', publish)
        self.assertNotIn('contents: write', workflow.split('  publish:\n')[0])
        self.assertIn('git/refs', publish)
        self.assertIn('--verify-tag --target "$GITHUB_SHA" --draft --prerelease --latest=false', publish)
        self.assertLess(publish.index('--input downloaded'), publish.index('--draft=false'))
        # Expressions are never interpolated into shell code; all variable
        # values use env/with fields or trusted GitHub environment variables.
        in_run = False
        for line in workflow.splitlines():
            if line.lstrip().startswith('run:'):
                in_run = True
            elif line.startswith('      - ') or (line.strip() and len(line) - len(line.lstrip()) < 8):
                in_run = False
            if in_run:
                self.assertNotIn('${{', line)


if __name__ == '__main__':
    unittest.main()
