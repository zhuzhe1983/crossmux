#!/usr/bin/env python3
"""Build deterministic, device-sized app assets from RGBA color masters.

Requires Pillow. No firmware or display driver changes are made by this tool.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PACK = ROOT / "assets/app-icons/semantic-v1"


def fitted_master(path: Path, size: int) -> Image.Image:
    """Fit alpha bounds once at final size; all outputs share identical geometry."""
    image = Image.open(path).convert("RGBA")
    alpha = image.getchannel("A")
    # Ignore stray nearly transparent generator pixels when measuring bounds.
    bounds = alpha.point(lambda value: 255 if value >= 16 else 0).getbbox()
    if bounds is None:
        raise ValueError(f"Empty image: {path}")
    image = image.crop(bounds)
    extent = max(1, round(size * 0.875))
    image.thumbnail((extent, extent), Image.Resampling.LANCZOS)
    result = Image.new("RGBA", (size, size))
    result.alpha_composite(image, ((size - image.width) // 2, (size - image.height) // 2))
    return result


def gray_source(color: Image.Image) -> Image.Image:
    """White matte, perceptual sRGB luma, modest contrast; never quantize early."""
    matte = Image.new("RGBA", color.size, "white")
    matte.alpha_composite(color)
    rgb = matte.convert("RGB")
    # Integer Rec.709 luma approximation in display-referred sRGB. This is a
    # perceptual asset mapping, not a claim about measured panel reflectance.
    channels = rgb.tobytes()
    values = [(54 * r + 183 * g + 19 * b + 128) // 256
              for r, g, b in zip(channels[0::3], channels[1::3], channels[2::3])]
    gray = Image.new("L", color.size)
    gray.putdata(values)
    # Expand the dark ink and cream ends without per-icon histogram stretching.
    gray = gray.point(lambda v: max(0, min(255, round((v - 10) * 255 / 235))))
    return gray.filter(ImageFilter.UnsharpMask(radius=0.55, percent=80, threshold=4))


def quantize(gray: Image.Image, levels: int, dither: bool) -> Image.Image:
    """Fixed palette; restrained serpentine Floyd-Steinberg inside midtones."""
    width, height = gray.size
    step = 255 / (levels - 1)
    source = list(gray.tobytes())
    if levels == 2 and not dither:
        # Small outlines average with white during downsampling. Preserve their
        # coverage optically instead of imposing the large-icon midpoint.
        threshold = 166 if width <= 48 else 146
        result = Image.new("P", gray.size)
        result.putdata([0 if value < threshold else 1 for value in source])
        result.putpalette([0, 0, 0, 255, 255, 255] + [255] * 762)
        return result
    indices = [0] * len(source)
    current = [0.0] * (width + 2)
    following = [0.0] * (width + 2)
    for y in range(height):
        direction = -1 if dither and y % 2 else 1
        columns = range(width) if direction == 1 else range(width - 1, -1, -1)
        for x in columns:
            original = source[y * width + x]
            # Keep light paper, dark outlines and already representable tones
            # clean. Only diffuse midtones; high-frequency spots on cream pages
            # obscure small icon semantics more than the extra tone helps.
            nearest = min(levels - 1, int(original / step + 0.5))
            locked = dither and (original <= 28 or original >= 220 or abs(original - nearest * step) <= 7)
            if locked:
                indices[y * width + x] = 0 if original <= 28 else levels - 1 if original >= 220 else nearest
                continue
            value = max(0.0, min(255.0, original + current[x + 1]))
            index = min(levels - 1, max(0, int(value / step + 0.5)))
            indices[y * width + x] = index
            if dither:
                error = (value - index * step) * 0.9
                current[x + 1 + direction] += error * 7 / 16
                following[x + 1 - direction] += error * 3 / 16
                following[x + 1] += error * 5 / 16
                following[x + 1 + direction] += error / 16
        current, following = following, [0.0] * (width + 2)
    result = Image.new("P", gray.size)
    result.putdata(indices)
    palette = []
    for i in range(256):
        value = round(min(i, levels - 1) * step)
        palette.extend((value, value, value))
    result.putpalette(palette)
    return result


def pack_pixels(image: Image.Image, bits: int) -> bytes:
    """Top-down rows, left-to-right MSB first, 0=black, max=white."""
    width, height = image.size
    stride = (width * bits + 7) // 8
    data = bytearray([255] * (stride * height))
    mask = (1 << bits) - 1
    for y in range(height):
        for x in range(width):
            bit = x * bits
            offset = y * stride + bit // 8
            shift = 8 - bits - bit % 8
            data[offset] = (data[offset] & ~(mask << shift)) | (image.getpixel((x, y)) << shift)
    return bytes(data)


def unpack_pixels(data: bytes, width: int, height: int, bits: int) -> list[int]:
    stride = (width * bits + 7) // 8
    mask = (1 << bits) - 1
    return [(data[y * stride + x * bits // 8] >> (8 - bits - x * bits % 8)) & mask
            for y in range(height) for x in range(width)]


def write_header(path: Path, icons: list[dict], arrays: list[bytes], size: int, levels: int, mode: str):
    bits = int(math.log2(levels))
    namespace = f"crossmux_icons_g{levels}_{size}_{mode}"
    lines = ["// Generated by scripts/prepare_app_icons.py; edit masters/catalog, not this file.",
             "#pragma once", "#include <cstdint>", f"namespace {namespace} {{",
             f"inline constexpr unsigned width = {size}, height = {size}, bitsPerPixel = {bits};",
             f"inline constexpr unsigned stride = {(size * bits + 7) // 8};",
             "// Row-major, MSB first. 0 = black; all bits set = white. Opaque white matte."]
    for icon, data in zip(icons, arrays):
        lines.append(f"inline constexpr std::uint8_t {icon['id']}[{len(data)}] = {{")
        for start in range(0, len(data), 16):
            lines.append("  " + ", ".join(f"0x{byte:02x}" for byte in data[start:start + 16]) + ",")
        lines.append("};")
    lines.append(f"}}  // namespace {namespace}")
    path.write_text("\n".join(lines) + "\n")


def load_font(size: int):
    candidates = ["/System/Library/Fonts/PingFang.ttc", "/System/Library/Fonts/STHeiti Medium.ttc",
                  "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"]
    for candidate in candidates:
        if Path(candidate).is_file():
            return ImageFont.truetype(candidate, size)
    return ImageFont.load_default(size=size)


def make_previews(pack: Path, icons: list[dict], sizes: list[int]):
    previews = pack / "previews"
    previews.mkdir(exist_ok=True)
    title_font, label_font = load_font(26), load_font(16)
    columns, cell, top = 6, 180, 76
    sheet = Image.new("RGB", (columns * cell, top + math.ceil(len(icons) / columns) * 202), "#faf9f6")
    draw = ImageDraw.Draw(sheet)
    draw.text((24, 18), "CrossMux · 应用图标彩色母版", fill="#14212a", font=title_font)
    for index, icon in enumerate(icons):
        x, y = index % columns * cell, top + index // columns * 202
        color = fitted_master(pack / icon["master"], 152)
        sheet.paste(color, (x + 14, y), color)
        draw.text((x + cell // 2, y + 162), icon["label"], anchor="mt", fill="#14212a", font=label_font)
    sheet.save(previews / "color-contact-sheet.png")
    all_modes = [("color", "彩色"), ("g2/plain", "2级 · 直接量化"), ("g2/dither", "2级 · 抖动"),
                 ("g4/plain", "4级 · 直接量化"), ("g4/dither", "4级 · 抖动"), ("g16/plain", "16级")]
    for size in sizes:
        modes = [(path, label) for path, label in all_modes if size >= 64 or not path.endswith("dither")]
        cell_w, row_h, left, top = 176, size + 42, 150, 70
        comparison = Image.new("RGB", (left + len(modes) * cell_w, top + len(icons) * row_h), "white")
        draw = ImageDraw.Draw(comparison)
        draw.text((16, 12), f"{size} px · 实际像素尺寸对照（请按 100% 查看）", fill="black", font=title_font)
        for column, (_, label) in enumerate(modes):
            draw.text((left + column * cell_w + cell_w // 2, 50), label, anchor="mm", fill="black", font=label_font)
        for row, icon in enumerate(icons):
            y = top + row * row_h
            draw.text((12, y + size // 2), icon["label"], anchor="lm", fill="black", font=label_font)
            for column, (mode, _) in enumerate(modes):
                if mode == "color":
                    source = pack / "color" / str(size) / f"{icon['id']}.png"
                else:
                    level, style = mode.split("/")
                    source = pack / level / str(size) / style / f"{icon['id']}.png"
                image = Image.open(source).convert("RGBA")
                comparison.paste(image, (left + column * cell_w + (cell_w - size) // 2, y), image)
            draw.line((0, y + row_h - 8, comparison.width, y + row_h - 8), fill="#dddddd")
        comparison.save(previews / f"comparison-{size}.png")


def build(pack: Path, only_available: bool = False):
    catalog = json.loads((pack / "catalog.json").read_text())
    icons = catalog["icons"]
    if only_available:
        icons = [icon for icon in icons if (pack / icon["master"]).is_file()]
    if not icons:
        raise ValueError("No color masters found")
    manifest = {"version": 1, "polarity": "0=black,max=white", "order": "row-major-msb-first",
                "matte": "white", "sources": [], "variants": []}
    for icon in icons:
        path = pack / icon["master"]
        rgba = Image.open(path).convert("RGBA")
        if rgba.getchannel("A").getextrema()[0] == 255:
            raise ValueError(f"Master has no transparency: {path}")
        manifest["sources"].append({"id": icon["id"], "path": icon["master"],
                                    "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                                    "dimensions": list(rgba.size)})
    for size in catalog["sizes"]:
        color_dir = pack / "color" / str(size)
        color_dir.mkdir(parents=True, exist_ok=True)
        grays = []
        for icon in icons:
            color = fitted_master(pack / icon["master"], size)
            color.save(color_dir / f"{icon['id']}.png", optimize=True)
            grays.append(gray_source(color))
        for levels in catalog["levels"]:
            bits = int(math.log2(levels))
            modes = ["plain", "dither"] if levels <= 4 and size >= 64 else ["plain"]
            for mode in modes:
                directory = pack / f"g{levels}" / str(size) / mode
                directory.mkdir(parents=True, exist_ok=True)
                arrays = []
                for icon, gray in zip(icons, grays):
                    image = quantize(gray, levels, mode == "dither")
                    data = pack_pixels(image, bits)
                    assert unpack_pixels(data, size, size, bits) == list(image.tobytes())
                    assert len(set(image.convert("L").tobytes())) <= levels
                    png, raw = directory / f"{icon['id']}.png", directory / f"{icon['id']}.bin"
                    image.save(png, bits=bits, optimize=True)
                    raw.write_bytes(data)
                    arrays.append(data)
                    manifest["variants"].append({"id": icon["id"], "size": size, "levels": levels,
                        "bits_per_pixel": bits, "mode": mode, "stride": (size * bits + 7) // 8,
                        "bytes": len(data), "png": str(png.relative_to(pack)), "raw": str(raw.relative_to(pack)),
                        "sha256": hashlib.sha256(data).hexdigest()})
                write_header(directory / "icons.h", icons, arrays, size, levels, mode)
    (pack / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
    make_previews(pack, icons, catalog["sizes"])
    print(f"Built {len(icons)} icons, {len(manifest['variants'])} grayscale variants; packed round-trips passed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pack", type=Path, default=DEFAULT_PACK)
    parser.add_argument("--only-available", action="store_true", help="Preview the masters generated so far")
    args = parser.parse_args()
    build(args.pack.resolve(), args.only_available)


if __name__ == "__main__":
    main()
