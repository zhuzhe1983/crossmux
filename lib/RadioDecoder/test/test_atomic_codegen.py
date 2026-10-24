#!/usr/bin/env python3
"""Check the actual Xtensa S3 queue code under the SDK's PSRAM atomic workaround."""
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

LIB = Path(__file__).resolve().parents[1]
COMPILER = (Path.home() / '.platformio/packages/toolchain-xtensa-esp-elf/bin/'
            'xtensa-esp32s3-elf-g++')


def check():
    if not COMPILER.is_file():
        print('Xtensa S3 compiler unavailable: target atomic-codegen check skipped')
        return
    flags = ['-std=gnu++20', '-Os', '-fno-exceptions', '-fno-rtti', '-mlongcalls',
             '-mdisable-hardware-atomics', '-fno-builtin-memcpy', '-DCROSSMUX_RADIO=1',
             '-I' + str(LIB), '-S']
    with tempfile.TemporaryDirectory(prefix='radio-atomic-codegen-') as folder:
        temp = Path(folder)
        source = temp / 'atomic-probe.cpp'
        source.write_text('''#include <atomic>
#include <cstdint>
extern "C" uint32_t queue_load(const std::atomic<uint32_t>* p) {
  return p->load(std::memory_order_acquire);
}
extern "C" void queue_store(std::atomic<uint32_t>* p, uint32_t value) {
  p->store(value, std::memory_order_release);
}
extern "C" bool whole_type_lock_free() {
  return std::atomic<uint32_t>::is_always_lock_free;
}
extern "C" uint32_t state_rmw(std::atomic<uint32_t>* p) {
  return p->fetch_add(1);
}
''')
        probe = temp / 'atomic-probe.s'
        command = [str(COMPILER), *flags, str(source), '-o', str(probe)]
        subprocess.run(command, check=True, timeout=30)
        print('Xtensa atomic probe:', shlex.join(command))
        assembly = probe.read_text()

        def body(name):
            match = re.search(r'^' + name + r':\n(.*?)\n\s*\.size\s+' + name,
                              assembly, re.M | re.S)
            assert match, name
            return match.group(1)

        for name, instruction in [('queue_load', 'l32i'), ('queue_store', 's32i')]:
            emitted = body(name)
            assert instruction in emitted and 'memw' in emitted, emitted
            assert 'call' not in emitted and '__atomic' not in emitted, emitted
            print(name + ': ' + '; '.join(line.strip() for line in emitted.splitlines()))
        assert re.search(r'movi(?:\.n)?\s+a2,\s*0', body('whole_type_lock_free'))
        assert '__atomic_fetch_add_4' in assembly
        # Check production code too, rather than relying only on the tiny probe.
        queue = temp / 'PcmQueue.s'
        subprocess.run([str(COMPILER), *flags, str(LIB / 'PcmQueue.cpp'), '-o', str(queue)],
                       check=True, timeout=30)
        emitted = queue.read_text()
        assert '__atomic' not in emitted and 'Critical' not in emitted and 'portMUX' not in emitted
        assert 'memw' in emitted and 'l32i' in emitted and 's32i' in emitted
        print('Production PcmQueue: aligned word loads/stores + MEMW; no atomic helper or lock calls')


if __name__ == '__main__':
    check()
