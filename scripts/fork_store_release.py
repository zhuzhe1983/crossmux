#!/usr/bin/env python3
"""Build/verify an isolated three-device CrossStore release, never an OTA index."""
import argparse
import configparser
import hashlib
import json
import re
import shutil
import struct
from pathlib import Path

from nightly_targets import TARGETS as OFFICIAL_TARGETS
from package_nightly_target import (
    EXPECTED_PARTITIONS, FULL_INSTALL_SEGMENTS, find_boot_app0, git_value,
    sha256, verify_firmware, verify_partition_csv,
)

ROOT = Path(__file__).resolve().parents[1]
KIND = 'crossstore-fork'
FLASH_SIZE = 16 * 1024 * 1024
TARGETS = {
    'm5stack_paper_mono': {**OFFICIAL_TARGETS['m5stack_paper_mono'], 'environment': 'papermono_store', 'flashMode': 'dio'},
    'm5stack_papers3': {'deviceSlug': 'papers3', 'models': ['m5paper_s3'], 'boardTag': 'papers3',
                      'chip': 'ESP32-S3', 'chipId': 9, 'environment': 'papers3_store', 'flashMode': 'qio'},
    'waveshare_epaper_397': {**OFFICIAL_TARGETS['waveshare_epaper_397'],
                           'environment': 'waveshare_epaper_397_store', 'flashMode': 'dio'},
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def require_sha(value):
    require(bool(re.fullmatch(r'[0-9a-f]{40}', value)), 'expected a full lowercase Git SHA')
    return value


def matrix():
    return {'include': [{'targetId': key, 'environment': target['environment']}
                        for key, target in TARGETS.items()]}


def validate_trigger(event, ref, expected_sha, payload):
    """Tag pushes bootstrap a feature-branch workflow without changing main."""
    require_sha(expected_sha)
    if event == 'workflow_dispatch':
        require(ref.startswith(('refs/heads/', 'refs/tags/')), 'dispatch requires a branch or tag ref')
        return
    require(event == 'push', 'unsupported release trigger')
    require(ref == 'refs/tags/crossstore-build-' + expected_sha, 'build tag must name the exact source SHA')
    require(payload.get('created') is True and payload.get('deleted') is False
            and payload.get('forced') is False, 'only a newly created, non-forced build tag can publish')


def option(config, section, key, visiting=()):
    """Resolve the project's explicit INI references/extends without shell eval."""
    token = (section, key)
    require(token not in visiting, 'cyclic PlatformIO configuration')
    visiting = (*visiting, token)
    if not config.has_option(section, key):
        parents = config.get(section, 'extends', fallback='').replace(',', ' ').split()
        for parent in parents:
            try:
                return option(config, parent, key, visiting)
            except KeyError:
                pass
        raise KeyError(token)
    value = config.get(section, key)
    def replace(match):
        reference = match.group(1).rsplit('.', 1)
        require(len(reference) == 2, 'invalid PlatformIO reference')
        return option(config, *reference, visiting)
    return re.sub(r'\$\{([^}]+)\}', replace, value)


def target_version(root, target):
    config = configparser.ConfigParser(interpolation=None, inline_comment_prefixes=(';',))
    config.read(root / 'platformio.ini')
    section = 'env:' + target['environment']
    flags = option(config, section, 'build_flags').split()
    require([f for f in flags if f.startswith('-DCROSSMUX_STORE=')] == ['-DCROSSMUX_STORE=1'],
            'release target must explicitly enable CrossStore once')
    versions = [f.split('=', 1)[1].replace('\\', '').strip('"')
                for f in flags if f.startswith('-DCROSSPOINT_VERSION=')]
    require(len(versions) == 1 and bool(re.fullmatch(r'[A-Za-z0-9.+_-]{1,95}', versions[0])),
            'release target must set one explicit safe CROSSPOINT_VERSION')
    require(option(config, section, 'board_build.partitions') == 'partitions.csv', 'unsupported partition CSV')
    require(option(config, section, 'board_build.flash_size') == '16MB', 'expected 16 MB flash')
    require(option(config, section, 'board_build.flash_mode') == target['flashMode'], 'wrong configured flash mode')
    require(option(config, section, 'board_build.mcu') == 'esp32s3', 'expected ESP32-S3')
    return versions[0]


def preflight(root, expected_sha):
    require_sha(expected_sha)
    require(git_value(root, 'rev-parse', 'HEAD') == expected_sha, 'checkout does not match dispatched SHA')
    for folder in (root, root / 'freeink-sdk'):
        require(not git_value(folder, 'status', '--porcelain', '--untracked-files=normal'),
                f'{folder.name} contains uncommitted/untracked source changes')
    link = git_value(root, 'ls-tree', 'HEAD', 'freeink-sdk').split()
    sdk_sha = git_value(root / 'freeink-sdk', 'rev-parse', 'HEAD')
    require(len(link) == 4 and link[:2] == ['160000', 'commit'] and link[2] == sdk_sha,
            'SDK checkout must equal the committed gitlink')
    require_sha(sdk_sha)
    require(not (root / 'platformio.local.ini').exists(), 'local PlatformIO overrides are not release inputs')
    for target in TARGETS.values():
        target_version(root, target)
    return sdk_sha


def verify_partition_binary(path):
    raw = path.read_bytes()
    require(0 < len(raw) <= 4096 and len(raw) % 32 == 0, 'invalid partition binary length')
    found, spans = {}, []
    for position in range(0, len(raw), 32):
        entry = raw[position:position + 32]
        if entry[:2] == b'\xeb\xeb':
            require(entry[2:16] == b'\xff' * 14 and entry[16:] == hashlib.md5(raw[:position]).digest(),
                    'invalid partition MD5')
            require(raw[position + 32:] == b'\xff' * (len(raw) - position - 32), 'partition trailer is not erased')
            break
        magic, kind, subtype, offset, size, name, flags = struct.unpack('<HBBII16sI', entry)
        require(magic == 0x50AA and size > 0 and offset >= 0x9000 and offset + size <= FLASH_SIZE,
                'invalid/out-of-flash partition')
        name = name.rstrip(b'\0').decode('ascii')
        require(name not in found, 'duplicate partition label')
        found[name] = (offset, size, kind, subtype)
        spans.append((offset, offset + size))
        require(flags == 0, 'encrypted/unknown partition flags are not supported')
    else:
        raise ValueError('partition MD5 entry is missing')
    spans.sort()
    require(all(a[1] <= b[0] for a, b in zip(spans, spans[1:])), 'overlapping partitions')
    types = {'otadata': (1, 0), 'app0': (0, 0x10), 'app1': (0, 0x11)}
    for label, bounds in EXPECTED_PARTITIONS.items():
        require(found.get(label) == (*bounds, *types[label]), f'wrong {label} partition')


def regular(path):
    require(path.is_file() and not path.is_symlink(), f'expected regular file: {path.name}')


def verify_images(folder, target, assets, version):
    by_role = {item['role']: item for item in assets}
    expected_roles = {role for role, _, _ in FULL_INSTALL_SEGMENTS} | {'full_install'}
    require(set(by_role) == expected_roles and len(assets) == len(expected_roles), 'missing/duplicate image role')
    slug = target['deviceSlug']
    for role, filename, offset in (*FULL_INSTALL_SEGMENTS, ('full_install', 'full-install.bin', 0)):
        item = by_role[role]
        require(item['name'] == f'{slug}-{filename}' and item['offset'] == offset, 'wrong asset name/offset')
        path = folder / item['name']
        regular(path)
        require(type(item['size']) is int and 0 < item['size'] <= FLASH_SIZE and path.stat().st_size == item['size'],
                'wrong image size')
        require(sha256(path) == item['sha256'], 'image SHA-256 mismatch')
    boot = (folder / by_role['bootloader']['name']).read_bytes()
    require(24 <= len(boot) <= 0x8000 and boot[0] == 0xE9 and int.from_bytes(boot[12:14], 'little') == 9,
            'wrong S3 bootloader')
    # ESP bootloaders can begin in DIO before enabling configured QIO. Preserve
    # the built header; the manifest records the board's upload mode separately.
    require(boot[2] in (0, 2) and boot[3] == 0x4f, 'bootloader must declare 16 MB / 80 MHz')
    verify_partition_binary(folder / by_role['partitions']['name'])
    require(by_role['boot_app0']['size'] == 0x2000, 'wrong boot_app0 size')
    firmware = folder / by_role['firmware']['name']
    require(firmware.stat().st_size <= EXPECTED_PARTITIONS['app0'][1], 'firmware exceeds OTA slot')
    verify_firmware(firmware, target['chipId'], target['boardTag'])
    require(version.encode() + b'\0' in firmware.read_bytes(), 'firmware does not contain configured version')
    full = (folder / by_role['full_install']['name']).read_bytes()
    require(len(full) == 0x10000 + firmware.stat().st_size, 'wrong merged image length')
    cursor = 0
    for role, _, offset in FULL_INSTALL_SEGMENTS:
        data = (folder / by_role[role]['name']).read_bytes()
        require(offset >= cursor and full[cursor:offset] == b'\xff' * (offset - cursor), 'merged gap differs')
        require(full[offset:offset + len(data)] == data, 'merged segment differs')
        cursor = offset + len(data)


def json_write(path, document):
    path.write_text(json.dumps(document, indent=2, sort_keys=True) + '\n')


def empty_directory(path):
    require(not path.is_symlink(), 'output cannot be a symlink')
    path.mkdir(parents=True, exist_ok=True)
    require(not any(path.iterdir()), 'output must be empty; refusing stale assets')


def package(root, target_id, expected_sha, output):
    sdk_sha = preflight(root, expected_sha)
    target = TARGETS[target_id]
    version = target_version(root, target)
    verify_partition_csv(root)
    empty_directory(output)
    build = root / '.pio/build' / target['environment']
    slug = target['deviceSlug']
    assets = []
    for role, source_name, offset in FULL_INSTALL_SEGMENTS:
        source = find_boot_app0() if role == 'boot_app0' else build / source_name
        regular(source)
        destination = output / f'{slug}-{source_name}'
        shutil.copyfile(source, destination)
        assets.append({'role': role, 'name': destination.name, 'offset': offset,
                       'size': destination.stat().st_size, 'sha256': sha256(destination)})
    full_path = output / f'{slug}-full-install.bin'
    with full_path.open('wb') as stream:
        for asset in assets:
            require(stream.tell() <= asset['offset'], 'segments overlap')
            stream.write(b'\xff' * (asset['offset'] - stream.tell()))
            stream.write((output / asset['name']).read_bytes())
    assets.append({'role': 'full_install', 'name': full_path.name, 'offset': 0,
                   'size': full_path.stat().st_size, 'sha256': sha256(full_path)})
    verify_images(output, target, assets, version)
    manifest = {'schemaVersion': 1, 'releaseKind': KIND, 'targetId': target_id,
                'models': target['models'], 'deviceSlug': slug, 'boardTag': target['boardTag'],
                'chip': target['chip'], 'environment': target['environment'], 'appStore': True,
                'version': version, 'crossmuxSha': expected_sha, 'sdkSha': sdk_sha,
                'partitionProfile': 'crossmux-sticky-v1',
                'flash': {'size': FLASH_SIZE, 'mode': target['flashMode'], 'frequency': '80m'}, 'assets': assets}
    manifest_path = output / f'{slug}-install-manifest.json'
    json_write(manifest_path, manifest)
    files = [output / asset['name'] for asset in assets] + [manifest_path]
    (output / f'{slug}-SHA256SUMS').write_text(''.join(f'{sha256(p)}  {p.name}\n' for p in files))
    return manifest


def verify_set(folder, expected_sha, sdk_sha, aggregated=False):
    require_sha(expected_sha)
    require_sha(sdk_sha)
    expected, summaries = set(), []
    for target_id, target in TARGETS.items():
        slug = target['deviceSlug']
        name = f'{slug}-install-manifest.json'
        path = folder / name
        regular(path)
        require(path.stat().st_size <= 16384, 'oversized manifest')
        manifest = json.loads(path.read_text())
        for key, value in {'schemaVersion': 1, 'releaseKind': KIND, 'targetId': target_id,
                           'deviceSlug': slug, 'models': target['models'], 'boardTag': target['boardTag'],
                           'chip': 'ESP32-S3', 'environment': target['environment'], 'appStore': True,
                           'crossmuxSha': expected_sha, 'sdkSha': sdk_sha,
                           'partitionProfile': 'crossmux-sticky-v1',
                           'flash': {'size': FLASH_SIZE, 'mode': target['flashMode'], 'frequency': '80m'}}.items():
            require(manifest.get(key) == value, f'{name}: wrong {key}')
        require(bool(re.fullmatch(r'[A-Za-z0-9.+_-]{1,95}', manifest['version'])), 'invalid version')
        verify_images(folder, target, manifest['assets'], manifest['version'])
        asset_names = [a['name'] for a in manifest['assets']] + [name]
        checksum_name = f'{slug}-SHA256SUMS'
        regular(folder / checksum_name)
        require((folder / checksum_name).read_text() == ''.join(f'{sha256(folder / n)}  {n}\n' for n in asset_names),
                'invalid checksum list')
        expected.update([*asset_names, checksum_name])
        summaries.append({'targetId': target_id, 'manifest': name, 'sha256': sha256(path)})
    summary = {'schemaVersion': 1, 'releaseKind': KIND, 'crossmuxSha': expected_sha,
               'sdkSha': sdk_sha, 'targets': summaries}
    if aggregated:
        summary_path = folder / 'crossstore-release.json'
        regular(summary_path)
        require(json.loads(summary_path.read_text()) == summary, 'release summary mismatch')
        expected.add(summary_path.name)
    require({p.name for p in folder.iterdir()} == expected, 'unexpected/missing files in release directory')
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    commands.add_parser('matrix')
    trigger = commands.add_parser('trigger')
    trigger.add_argument('--event', required=True)
    trigger.add_argument('--ref', required=True)
    trigger.add_argument('--sha', required=True)
    trigger.add_argument('--event-file', type=Path, required=True)
    for command in ('preflight', 'package', 'assemble', 'verify'):
        sub = commands.add_parser(command)
        sub.add_argument('--sha', required=True)
        if command == 'package':
            sub.add_argument('--target', required=True, choices=TARGETS)
        if command in ('assemble', 'verify'):
            sub.add_argument('--input', required=True, type=Path)
            sub.add_argument('--sdk-sha', required=True)
        if command != 'verify':
            sub.add_argument('--output', type=Path, required=command != 'preflight')
    args = parser.parse_args()
    if args.command == 'matrix':
        print(json.dumps(matrix(), separators=(',', ':')))
    elif args.command == 'trigger':
        validate_trigger(args.event, args.ref, args.sha, json.loads(args.event_file.read_text()))
    elif args.command == 'preflight':
        sdk_sha = preflight(ROOT, args.sha)
        if args.output:
            with args.output.open('a') as stream:
                stream.write('matrix=' + json.dumps(matrix(), separators=(',', ':')) + '\n')
                stream.write('sdk_sha=' + sdk_sha + '\n')
        print('Source preflight passed: ' + args.sha + ' / SDK ' + sdk_sha)
    elif args.command == 'package':
        package(ROOT, args.target, args.sha, args.output)
    elif args.command == 'assemble':
        require(git_value(ROOT, 'rev-parse', 'HEAD') == args.sha, 'publish checkout SHA differs')
        summary = verify_set(args.input, args.sha, args.sdk_sha)
        empty_directory(args.output)
        for path in args.input.iterdir():
            shutil.copyfile(path, args.output / path.name)
        json_write(args.output / 'crossstore-release.json', summary)
    else:
        verify_set(args.input, args.sha, args.sdk_sha, aggregated=True)


if __name__ == '__main__':
    main()
