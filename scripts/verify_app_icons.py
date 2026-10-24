#!/usr/bin/env python3
"""Independently verify on-disk PNG palette, packed payload, bounds and hashes."""
import argparse
import hashlib
import json
from pathlib import Path
import re

from PIL import Image


def verify(pack):
    manifest = json.loads((pack / "manifest.json").read_text())
    catalog = json.loads((pack / "catalog.json").read_text())
    expected_ids = {icon["id"] for icon in catalog["icons"]}
    assert {source["id"] for source in manifest["sources"]} == expected_ids
    assert len(expected_ids) == len(catalog["icons"]), "Duplicate icon ID"
    expected = {(icon, size, level, mode)
                for icon in expected_ids for size in catalog["sizes"] for level in catalog["levels"]
                for mode in (["plain", "dither"] if size >= 64 and level <= 4 else ["plain"])}
    seen = set()
    headers = {}
    for source in manifest["sources"]:
        path = pack / source["path"]
        assert hashlib.sha256(path.read_bytes()).hexdigest() == source["sha256"]
        with Image.open(path) as image:
            assert list(image.size) == source["dimensions"]
            assert image.mode == "RGBA" and image.getchannel("A").getextrema() == (0, 255)
    for variant in manifest["variants"]:
        key = (variant["id"], variant["size"], variant["levels"], variant["mode"])
        assert key not in seen, f"Duplicate variant {key}"
        seen.add(key)
        png, raw = pack / variant["png"], pack / variant["raw"]
        size, bits, levels = variant["size"], variant["bits_per_pixel"], variant["levels"]
        data = raw.read_bytes()
        assert len(data) == variant["bytes"] == variant["stride"] * size
        assert hashlib.sha256(data).hexdigest() == variant["sha256"]
        assert png.read_bytes()[24] == bits, f"PNG bit depth: {png}"
        if png.parent not in headers:
            arrays = re.findall(r"std::uint8_t (\w+)\[(\d+)\] = \{(.*?)\};",
                                (png.parent / "icons.h").read_text(), re.S)
            headers[png.parent] = {name: bytes(int(value, 16) for value in re.findall(r"0x[0-9a-f]+", body))
                                   for name, _, body in arrays}
            assert set(headers[png.parent]) == expected_ids
        assert headers[png.parent][variant["id"]] == data, f"C++/BIN mismatch: {key}"
        with Image.open(png) as image:
            assert image.size == (size, size)
            assert "transparency" not in image.info
            luminances = image.convert("L").tobytes()
            expected_palette = {round(i * 255 / (levels - 1)) for i in range(levels)}
            assert set(luminances) <= expected_palette
            decoded = []
            for row in range(size):
                for byte in data[row * variant["stride"]:(row + 1) * variant["stride"]]:
                    decoded.extend(round(((byte >> shift) & (levels - 1)) * 255 / (levels - 1))
                                   for shift in range(8 - bits, -1, -bits))
            assert bytes(decoded) == luminances, f"PNG/BIN mismatch: {key}"
            # A white outer border prevents clipped strokes and makes matte explicit.
            assert all(luminances[x] == 255 and luminances[(size - 1) * size + x] == 255 for x in range(size))
            assert all(luminances[y * size] == 255 and luminances[y * size + size - 1] == 255 for y in range(size))
    assert seen == expected, f"Missing/extra variants: {seen ^ expected}"
    for icon in catalog["icons"]:
        for size in catalog["sizes"]:
            with Image.open(pack / "color" / str(size) / f"{icon['id']}.png") as image:
                assert image.mode == "RGBA" and image.size == (size, size)
                assert image.getchannel("A").getextrema() == (0, 255)
    print(f"Verified {len(expected_ids)} masters, {len(seen)} grayscale variants and all color sizes")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pack", type=Path,
                        default=Path(__file__).resolve().parents[1] / "assets/app-icons/semantic-v1")
    verify(parser.parse_args().pack)
