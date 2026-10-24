#!/usr/bin/env python3
"""Compile actual uninstall/permissions/service code, replacing only device boundaries."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).parent
headers = list((ROOT / '.pio/libdeps').glob('*/ArduinoJson/src/ArduinoJson.h'))
if not headers:
    raise SystemExit('ArduinoJson dependency missing: prepare a PlatformIO target first')
common = ['invalid', 'keep', 'all', 'weread', 'storage_unavailable', 'permission_failure',
          'sidecar_failure', 'partial_retry', 'deep', 'long_name', 'colon_name', 'large',
          'file_instead_of_directory', 'chat_keep', 'chat_all', 'rf_all', 'esp_all']
with tempfile.TemporaryDirectory(prefix='crossstore-uninstall-') as temporary:
    folder = Path(temporary)
    # Production Session owns FreeRTOS/backend code. Replace that boundary only;
    # the ownership counters, main-task check, persistence and release are real.
    service = (ROOT / 'src/activities/apps/chat/ChatService.cpp').read_text()
    service = service.replace('"ChatSession.h"', '<UninstallTestSession.h>')
    service_path = folder / 'ChatServiceHost.cpp'
    service_path.write_text(service)
    for chat in (False, True):
        binary = folder / ('uninstall-chat' if chat else 'uninstall-nochat')
        command = ['c++', '-std=c++20', '-fno-exceptions', '-Wall', '-Wextra', '-Werror',
                   '-DCROSSMUX_STORE=1']
        if chat:
            # Existing ChatService preference reader compares HAL's int read
            # result with a bounded size_t; keep its warning without rewriting it.
            command += ['-Wno-error=sign-compare']
            command += ['-DCROSSMUX_TOX_CHAT=1', '-DCROSSMUX_LORA_CHAT=1', '-DCROSSMUX_ESP_CHAT=1']
        for include in (HERE / 'stubs', ROOT / 'lib/Memory', ROOT / 'test/crossstore/catalog_stubs',
                        headers[0].parent, ROOT / 'src', ROOT / 'src/activities/apps/chat',
                        ROOT / 'lib/CrossStoreRuntime'):
            command += ['-I' + str(include)]
        command += [str(ROOT / 'src/crossstore' / name) for name in
                    ('Uninstall.cpp', 'Permissions.cpp', 'PackageManifest.cpp')]
        if chat:
            command.append(str(service_path))
        command += [str(HERE / 'UninstallTest.cpp'), '-o', str(binary)]
        subprocess.run(command, check=True, timeout=60)
        scenarios = common + (['foreground', 'wrong_task', 'unregistered', 'stop_failure'] if chat else [])
        for scenario in scenarios:
            storage = folder / (binary.name + '-' + scenario)
            storage.mkdir()
            subprocess.run([str(binary), str(storage), scenario], check=True, timeout=10)
    print('Uninstall: 38 host scenarios passed (chat enabled/disabled)')
