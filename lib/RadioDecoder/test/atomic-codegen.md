# ESP32-S3 atomic queue audit

The installed SDK is ESP-IDF 5.5.5 (Arduino library package
`5.5.5+sha.b774170ff46`). Its `esp32s3/flags/cpp_flags` includes
`-mdisable-hardware-atomics`; `esp32s3/dio_opi/include/sdkconfig.h:1255`
enables `CONFIG_STDATOMIC_S32C1I_SPIRAM_WORKAROUND`.

Consequently `std::atomic<uint32_t>::is_always_lock_free` is false. That is
a property of all operations on the type, not evidence that this queue's
aligned word loads/stores take a lock. The queue never uses compare/exchange
or other read/modify/write operations.

Run the reproducible target-compiler check (also included in `test_radio.py`):

```sh
python3 lib/RadioDecoder/test/test_atomic_codegen.py
```

The script creates and deletes its own temporary source/assembly files. Its
exact compiler command is printed, with these flags:

```sh
~/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-g++ \
  -std=gnu++20 -Os -fno-exceptions -fno-rtti -mlongcalls \
  -mdisable-hardware-atomics -fno-builtin-memcpy -DCROSSMUX_RADIO=1 \
  -Ilib/RadioDecoder -S atomic-probe.cpp -o atomic-probe.s
```

Observed with `GCC: (crosstool-NG esp-14.2.0_20260121) 14.2.0`:

```asm
queue_load:                  # std::atomic<uint32_t>::load(acquire)
    entry sp, 32
    memw
    l32i a2, a2, 0
    memw
    retw.n
queue_store:                 # std::atomic<uint32_t>::store(release)
    entry sp, 32
    memw
    memw
    s32i a3, a2, 0
    retw.n
```

The same compile check covers **production `PcmQueue.cpp`**, and rejects
atomic helper or critical-section calls. The queue retains standard C++
acquire/release ordering and alignment/size assertions, without depending
on the incorrect whole-type lock-free assertion. No SDK flag is overridden.

The installed `esp32s3/lib/libnewlib.a` was also checked with:

```sh
~/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-objdump \
  -dr ~/.platformio/packages/framework-arduinoespressif32-libs/esp32s3/lib/libnewlib.a
```

`__atomic_load_4`/`__atomic_store_4` first classify the pointer: internal RAM
branches to `__atomic_s32c1i_load_4`/`__atomic_s32c1i_store_4`, while external
RAM branches to `xPortEnterCriticalTimeout` around the operation. This agrees
with Espressif's [atomic dispatch implementation](https://github.com/espressif/esp-idf/blob/v5.5.2/components/newlib/priv_include/esp_stdatomic.h).
Read/modify/write helpers use that same address-dependent dispatch.

Player's small `Impl`, including queue indexes and **all** state atomics, now
uses `memory::makeInternalByteBufferNoThrow`. This avoids default `new` falling
back to PSRAM and makes its infrequent state RMW operations take the internal
hardware path. The sample ring remains PSRAM. The existing nothrow C++ factory
still constructs the entire object; normal `unique_ptr` deletion invokes its
destructor and the matching capability-buffer free deleter. A static assertion
checks the allocator's supported alignment. Host tests cover allocation failure
and assert both control-buffer and HTTP-session construction/destruction counts
return to zero on every lifecycle path.

The audit establishes the generated queue operations and allocation placement;
it does not certify real-time latency on a running device. Use the output
worker's `max-refill-us` and stack high-water diagnostics for that check.
