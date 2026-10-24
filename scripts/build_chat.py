"""Build the pinned Tox library after PlatformIO configures the target compiler."""
from pathlib import Path

Import("env")  # noqa: F821

tox = Path(env.subst("$PROJECT_DIR")) / ".cache/chat-deps/c-toxcore-0.2.23"
library_env = env.Clone()
library_env.AppendUnique(CPPPATH=[str(tox)])
# ESP-IDF's wpa_supplicant exports unsigned long os_random(void), while Tox
# exports const Random *os_random(void). Archive ordering can silently resolve
# Tox's call to the Wi-Fi function and use random bits as a pointer. Rename only
# inside the Tox compilation environment; the Wi-Fi ABI must remain unchanged.
library_env.AppendUnique(CPPDEFINES=[("os_random", "crossmux_tox_os_random")])
library_env.Append(CFLAGS=["-Wno-unused-parameter", "-Wno-sign-compare"])
library = library_env.BuildLibrary(
    env.subst("$BUILD_DIR") + "/libtoxcore",
    str(tox),
    src_filter=["-<*>", "+<toxcore/*.c>", "+<toxcore/events/*.c>",
                "+<third_party/cmp/cmp.c>"],
)
env.Append(LIBS=[library])
