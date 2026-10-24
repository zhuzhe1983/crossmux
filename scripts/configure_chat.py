"""Pinned, optional Tox build. No downloads or objects in ordinary firmware."""

import hashlib
from pathlib import Path
import tarfile
import urllib.request

Import("env")  # noqa: F821

root = Path(env.subst("$PROJECT_DIR"))
cache = root / ".cache" / "chat-deps"
cache.mkdir(parents=True, exist_ok=True)


def unpack(url, digest, directory):
    destination = cache / directory
    marker = destination / ".crossmux-verified"
    if marker.exists() and marker.read_text() == digest:
        return destination
    archive = cache / (directory + ".tar.gz")
    if not archive.exists() or hashlib.sha256(archive.read_bytes()).hexdigest() != digest:
        with urllib.request.urlopen(url, timeout=60) as response:
            data = response.read(8 * 1024 * 1024)
        if hashlib.sha256(data).hexdigest() != digest:
            raise RuntimeError("Chat dependency checksum mismatch: " + directory)
        archive.write_bytes(data)
    destination.mkdir(exist_ok=True)
    with tarfile.open(archive) as source:
        for member in source.getmembers():
            parts = Path(member.name).parts[1:]
            if not parts:
                continue
            if ".." in parts:
                raise RuntimeError("Unsafe chat dependency archive")
            if member.issym() or member.islnk():
                continue  # packaging-only LICENSE aliases are not build inputs
            target = destination.joinpath(*parts)
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
            elif member.isfile():
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(source.extractfile(member).read())
    marker.write_text(digest)
    return destination


tox = unpack(
    "https://github.com/TokTok/c-toxcore/releases/download/v0.2.23/c-toxcore-0.2.23.tar.gz",
    "b0349f4829d3d1699a77e199850f870f48d376e2baaf2c69d27b28571c498cfe",
    "c-toxcore-0.2.23",
)
cmp = unpack(
    "https://codeload.github.com/TokTok/cmp/tar.gz/52bfcfa17d2eb4322da2037ad625f5575129cece",
    "4abfd641dd5ccba04b6e0ced04a79755fa70709290b3ba15dbd4b4a2de345ed0",
    "cmp-52bfcfa",
)
for name in ("cmp.c", "cmp.h"):
    target = tox / "third_party" / "cmp" / name
    target.parent.mkdir(exist_ok=True)
    content = (cmp / name).read_bytes()
    if not target.exists() or target.read_bytes() != content:
        target.write_bytes(content)

# A bounded text client does not need the desktop file-transfer window. These
# are local queue slots, not a wire-format change. Keep the upstream power-of-2
# invariant and its minimum congestion window (64 packets).
header = tox / "toxcore" / "net_crypto.h"
content = header.read_text()
old = "#define CRYPTO_PACKET_BUFFER_SIZE 32768"
new = "#define CRYPTO_PACKET_BUFFER_SIZE 1024"
if old in content:
    header.write_text(content.replace(old, new))
elif new not in content:
    raise RuntimeError("Unexpected pinned Tox packet-buffer definition")

env.AppendUnique(CPPPATH=[str(tox)])

# ESP-IDF's newlib exposes GNU strerror_r when _GNU_SOURCE is enabled too.
# Select by libc ABI, not by pretending to be glibc.
network = tox / "toxcore" / "os_network.c"
content = network.read_text()
content = content.replace(
    "#if defined(_GNU_SOURCE) && defined(__GLIBC__)",
    "#if defined(_GNU_SOURCE) && (defined(__GLIBC__) || defined(__NEWLIB__))",
)
# GNU strerror_r may return static storage; net_strerror must return buf->data.
content = content.replace(
    "    const size_t retstr_len = strlen(retstr);",
    '    if (retstr != buf->data) snprintf(buf->data, NET_STRERROR_SIZE, "%s", retstr);\n'
    "    const size_t retstr_len = strlen(buf->data);",
)
if content != network.read_text():
    network.write_text(content)
