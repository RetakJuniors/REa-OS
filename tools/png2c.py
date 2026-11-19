#!/usr/bin/env python3
"""Generate immutable ARGB cursor data from PNG images using Pillow."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print(
        "Error: Pillow is required. Install it with "
        "'python3 -m pip install Pillow' or your system package manager.",
        file=sys.stderr,
    )
    raise SystemExit(1)


HOTSPOTS: dict[str, tuple[int, int]] = {}
ARROW_NAME_HINTS = ("arrow", "pointer", "normal", "default")


def parse_hotspot(value: str) -> tuple[str, tuple[int, int]]:
    try:
        filename, coordinates = value.rsplit(":", 1)
        x_text, y_text = coordinates.split(",", 1)
        x, y = int(x_text), int(y_text)
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            "hotspots must have the form filename.png:x,y"
        ) from error
    if not filename or x < 0 or y < 0:
        raise argparse.ArgumentTypeError(
            "hotspot filename and coordinates must be non-negative"
        )
    return filename, (x, y)


def c_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def read_hotspots(path: Path) -> dict[str, tuple[int, int]]:
    if not path.exists():
        return {}

    hotspots: dict[str, tuple[int, int]] = {}
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        fields = line.split()
        if not fields or fields[0].startswith("#"):
            continue
        if len(fields) != 3:
            raise ValueError(f"invalid hotspot entry at {path}:{line_number}")
        try:
            x, y = int(fields[1]), int(fields[2])
        except ValueError as error:
            raise ValueError(
                f"invalid hotspot coordinates at {path}:{line_number}"
            ) from error
        if x < 0 or y < 0:
            raise ValueError(f"negative hotspot coordinate at {path}:{line_number}")
        hotspots[fields[0]] = (x, y)
    return hotspots


def choose_default(images: list[dict[str, object]]) -> int:
    for hint in ARROW_NAME_HINTS:
        for index, image in enumerate(images):
            name = Path(str(image["name"])).stem.lower()
            if hint in name:
                return index
    print(
        "Warning: no cursor filename suggests an arrow/default pointer; "
        f"using {images[0]['name']!r}.",
        file=sys.stderr,
    )
    return 0


def read_images(
    input_dir: Path, hotspots: dict[str, tuple[int, int]]
) -> list[dict[str, object]]:
    if not input_dir.is_dir():
        raise ValueError(f"cursor directory does not exist: {input_dir}")

    files = sorted(
        path for path in input_dir.rglob("*")
        if path.is_file() and path.suffix.lower() == ".png"
    )
    unsupported = sorted(
        path for path in input_dir.rglob("*")
        if path.is_file() and path.suffix.lower() != ".png" and
        path.name != "hotspots.txt"
    )
    if unsupported:
        names = ", ".join(
            path.relative_to(input_dir).as_posix() for path in unsupported
        )
        print(
            f"Unsupported cursor format(s), ignored: {names}. "
            "Only PNG files are read.",
            file=sys.stderr,
        )
    if not files:
        raise ValueError(f"no PNG cursor images found in {input_dir}")

    images: list[dict[str, object]] = []
    for path in files:
        name = path.relative_to(input_dir).as_posix()
        with Image.open(path) as source:
            if source.format != "PNG":
                print(f"Warning: {name} is not a PNG image; skipped.", file=sys.stderr)
                continue
            width, height = source.size
            if width <= 0 or height <= 0:
                raise ValueError(f"invalid image dimensions: {path}")
            if width > 65535 or height > 65535:
                raise ValueError(f"image dimensions exceed the supported range: {path}")
            if width > 64 or height > 64:
                print(
                    f"Warning: {name} is {width}x{height}, larger than 64x64.",
                    file=sys.stderr,
                )

            has_alpha = "A" in source.getbands() or "transparency" in source.info
            rgba = source.convert("RGBA")
            alpha = rgba.getchannel("A")
            if not has_alpha:
                print(
                    f"Warning: {name} has no alpha channel; it will be opaque.",
                    file=sys.stderr,
                )
            elif alpha.getextrema() == (255, 255):
                print(
                    f"Warning: {name} has an alpha channel but no transparent pixels.",
                    file=sys.stderr,
                )

            hotspot_x, hotspot_y = hotspots.get(
                name,
                hotspots.get(
                    path.name,
                    hotspots.get(
                        path.stem,
                        HOTSPOTS.get(
                            name, HOTSPOTS.get(path.name, HOTSPOTS.get(path.stem, (0, 0)))
                        ),
                    ),
                ),
            )
            if hotspot_x >= width or hotspot_y >= height:
                raise ValueError(
                    f"hotspot ({hotspot_x},{hotspot_y}) is outside {name}"
                )

            rgba_bytes = rgba.tobytes()
            argb = [
                (rgba_bytes[offset + 3] << 24)
                | (rgba_bytes[offset] << 16)
                | (rgba_bytes[offset + 1] << 8)
                | rgba_bytes[offset + 2]
                for offset in range(0, len(rgba_bytes), 4)
            ]
            images.append(
                {
                    "name": name,
                    "width": width,
                    "height": height,
                    "hotspot_x": hotspot_x,
                    "hotspot_y": hotspot_y,
                    "pixels": argb,
                }
            )

    if not images:
        raise ValueError(f"no readable PNG cursor images found in {input_dir}")
    return images


def write_header(path: Path, images: list[dict[str, object]]) -> None:
    max_width = max(int(image["width"]) for image in images)
    max_height = max(int(image["height"]) for image in images)
    lines = [
        "#ifndef REA_CURSOR_DATA_H",
        "#define REA_CURSOR_DATA_H",
        "",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        "struct cursor_image {",
        "    const char *name;",
        "    uint16_t width;",
        "    uint16_t height;",
        "    uint16_t hotspot_x;",
        "    uint16_t hotspot_y;",
        "    const uint32_t *argb;",
        "};",
        "",
        f"#define CURSOR_IMAGE_COUNT {len(images)}u",
        f"#define CURSOR_MAX_WIDTH {max_width}u",
        f"#define CURSOR_MAX_HEIGHT {max_height}u",
        "",
        "const struct cursor_image *cursor_get_images(size_t *count);",
        "const struct cursor_image *cursor_get_default(void);",
        "",
        "#endif",
        "",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="ascii")


def write_source(path: Path, header_path: Path, images: list[dict[str, object]]) -> int:
    default_index = choose_default(images)
    include_name = header_path.name
    lines = [
        f'#include "{include_name}"',
        "",
    ]
    for index, image in enumerate(images):
        lines.append(f"static const uint32_t cursor_pixels_{index}[] = {{")
        pixels = image["pixels"]
        assert isinstance(pixels, list)
        for offset in range(0, len(pixels), 8):
            row = pixels[offset : offset + 8]
            lines.append(
                "    " + ", ".join(f"0x{pixel:08x}u" for pixel in row) + ","
            )
        lines.extend(["};", ""])

    lines.append("static const struct cursor_image cursor_images[] = {")
    for index, image in enumerate(images):
        lines.append(
            "    { "
            f"{c_string(str(image['name']))}, "
            f"{image['width']}u, {image['height']}u, "
            f"{image['hotspot_x']}u, {image['hotspot_y']}u, "
            f"cursor_pixels_{index} "
            "},"
        )
    lines.extend(
        [
            "};",
            "",
            "const struct cursor_image *cursor_get_images(size_t *count)",
            "{",
            "    if (count != 0) {",
            "        *count = CURSOR_IMAGE_COUNT;",
            "    }",
            "    return cursor_images;",
            "}",
            "",
            "const struct cursor_image *cursor_get_default(void)",
            "{",
            f"    return &cursor_images[{default_index}];",
            "}",
            "",
        ]
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="ascii")
    return default_index


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Convert PNG cursor images to immutable C ARGB tables."
    )
    default_input_dir = Path("assets/cursors")
    if not default_input_dir.is_dir() and Path("assets/cursor").is_dir():
        default_input_dir = Path("assets/cursor")
    parser.add_argument("--input-dir", type=Path, default=default_input_dir)
    parser.add_argument("--output-c", type=Path, default=Path("src/cursor_data.c"))
    parser.add_argument("--output-h", type=Path, default=Path("src/cursor_data.h"))
    parser.add_argument(
        "--hotspot",
        action="append",
        type=parse_hotspot,
        default=[],
        metavar="FILE:X,Y",
        help="override a PNG hotspot; may be supplied more than once",
    )
    args = parser.parse_args()

    try:
        hotspots = read_hotspots(args.input_dir / "hotspots.txt")
        hotspots.update(dict(args.hotspot))
        images = read_images(args.input_dir, hotspots)
        write_header(args.output_h, images)
        default_index = write_source(args.output_c, args.output_h, images)
    except (OSError, ValueError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1

    print(f"Generated {len(images)} cursor image(s).")
    print(f"Default arrow cursor: {images[default_index]['name']}")
    print(f"C source: {args.output_c}")
    print(f"C header: {args.output_h}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
