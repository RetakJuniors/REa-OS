#!/usr/bin/env python3
"""Generate a small, static C glyph table from a TrueType font using Pillow."""

from __future__ import annotations

import argparse
import sys
import warnings
from pathlib import Path


RANGES = ((0x0020, 0x007E), (0x00A0, 0x00FF), (0x0100, 0x017F))
DEFAULT_FONT_CANDIDATES = (
    Path("fonts/Inter-Regular.ttf"),
    Path("fonts/Inter_18pt-Regular.ttf"),
)


def codepoints() -> list[int]:
    return [
        codepoint
        for first, last in RANGES
        for codepoint in range(first, last + 1)
    ]


def c_bytes(data: bytes, columns: int = 16) -> str:
    if not data:
        return "    0x00"
    lines = []
    for offset in range(0, len(data), columns):
        chunk = data[offset : offset + columns]
        lines.append("    " + ", ".join(f"0x{value:02x}" for value in chunk))
    return ",\n".join(lines)


def resolve_default_font() -> Path:
    for candidate in DEFAULT_FONT_CANDIDATES:
        if candidate.is_file():
            return candidate
    return DEFAULT_FONT_CANDIDATES[0]


def render_font(font_path: Path, size: int):
    try:
        from PIL import Image, ImageDraw, ImageFont
    except ImportError as error:
        raise RuntimeError(
            "Pillow is required. Install it with `python3 -m pip install Pillow` "
            "or your OS package manager (for example `sudo apt install python3-pil`)."
        ) from error

    try:
        font = ImageFont.truetype(str(font_path), size=size)
    except OSError as error:
        raise RuntimeError(f"Cannot open TrueType font: {font_path}") from error

    ascent, descent = font.getmetrics()
    line_height = ascent + descent
    missing_bbox = font.getbbox("\U0010ffff", anchor="ls")
    missing_mask = font.getmask("\U0010ffff", mode="L")
    missing_signature = (missing_bbox, missing_mask.size, bytes(missing_mask))

    glyphs = []
    missing = []
    for codepoint in codepoints():
        character = chr(codepoint)
        bbox = font.getbbox(character, anchor="ls")
        mask = font.getmask(character, mode="L")
        if (bbox, mask.size, bytes(mask)) == missing_signature:
            missing.append(codepoint)
            character = "?"
            bbox = font.getbbox(character, anchor="ls")

        left, top, right, bottom = bbox
        width = max(0, right - left)
        height = max(0, bottom - top)
        image = Image.new("L", (width, height), 0)
        if width and height:
            ImageDraw.Draw(image).text(
                (-left, -top), character, font=font, fill=255, anchor="ls"
            )

        advance = max(0, int(round(font.getlength(character))))
        glyphs.append(
            {
                "codepoint": codepoint,
                "width": width,
                "height": height,
                "bearing_x": left,
                "bearing_y": -top,
                "advance": advance,
                "alpha": image.tobytes(),
            }
        )

    return ascent, descent, line_height, glyphs, missing


def header_text() -> str:
    return """#ifndef REA_FONT_H
#define REA_FONT_H

#include <stdint.h>

struct glyph {
    uint32_t codepoint;
    uint16_t width;
    uint16_t height;
    int16_t bearing_x;
    int16_t bearing_y;
    uint16_t advance;
    const uint8_t *alpha;
};

const struct glyph *font_get_glyph(uint32_t codepoint);
uint16_t font_ascent(void);
uint16_t font_descent(void);
uint16_t font_line_height(void);

#endif
"""


def source_text(ascent: int, descent: int, line_height: int, glyphs: list[dict]) -> str:
    bitmap = b"".join(glyph["alpha"] for glyph in glyphs)
    offsets = []
    offset = 0
    for glyph in glyphs:
        offsets.append(offset)
        offset += len(glyph["alpha"])

    rows = []
    for glyph, bitmap_offset in zip(glyphs, offsets):
        rows.append(
            "    { "
            f"0x{glyph['codepoint']:04x}, {glyph['width']}, {glyph['height']}, "
            f"{glyph['bearing_x']}, {glyph['bearing_y']}, {glyph['advance']}, "
            f"glyph_alpha + {bitmap_offset} "
            "}"
        )

    glyph_rows = ",\n".join(rows)
    return f"""#include "font.h"

#define GENERATED_FONT_ASCENT {ascent}
#define GENERATED_FONT_DESCENT {descent}
#define GENERATED_FONT_LINE_HEIGHT {line_height}
#define GENERATED_GLYPH_COUNT {len(glyphs)}

static const uint8_t glyph_alpha[] = {{
{c_bytes(bitmap)}
}};

static const struct glyph glyphs[GENERATED_GLYPH_COUNT] = {{
{glyph_rows}
}};

static const struct glyph *find_glyph(uint32_t codepoint)
{{
    uint32_t low = 0;
    uint32_t high = GENERATED_GLYPH_COUNT;

    while (low < high) {{
        uint32_t middle = low + (high - low) / 2;
        if (glyphs[middle].codepoint < codepoint) {{
            low = middle + 1;
        }} else {{
            high = middle;
        }}
    }}

    if (low < GENERATED_GLYPH_COUNT &&
        glyphs[low].codepoint == codepoint) {{
        return &glyphs[low];
    }}
    return 0;
}}

const struct glyph *font_get_glyph(uint32_t codepoint)
{{
    const struct glyph *glyph = find_glyph(codepoint);
    return glyph != 0 ? glyph : find_glyph('?');
}}

uint16_t font_ascent(void)
{{
    return GENERATED_FONT_ASCENT;
}}

uint16_t font_descent(void)
{{
    return GENERATED_FONT_DESCENT;
}}

uint16_t font_line_height(void)
{{
    return GENERATED_FONT_LINE_HEIGHT;
}}
"""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--font",
        type=Path,
        default=resolve_default_font(),
        help="Input TrueType font (default: an Inter Regular file under fonts/)",
    )
    parser.add_argument("--size", type=int, default=16, help="Font size in pixels")
    parser.add_argument("--c-output", type=Path, default=Path("src/font.c"))
    parser.add_argument("--header-output", type=Path, default=Path("src/font.h"))
    arguments = parser.parse_args()

    if arguments.size < 1 or arguments.size > 256:
        parser.error("--size must be between 1 and 256")
    if not arguments.font.is_file():
        parser.error(
            f"font file not found: {arguments.font} "
            "(use --font fonts/Inter_18pt-Regular.ttf if that is its filename)"
        )

    try:
        ascent, descent, line_height, glyphs, missing = render_font(
            arguments.font, arguments.size
        )
    except RuntimeError as error:
        print(f"ttf2c.py: error: {error}", file=sys.stderr)
        return 1

    for codepoint in missing:
        warnings.warn(
            f"font has no glyph for U+{codepoint:04X}; '?' glyph will be used",
            RuntimeWarning,
            stacklevel=0,
        )

    arguments.c_output.parent.mkdir(parents=True, exist_ok=True)
    arguments.header_output.parent.mkdir(parents=True, exist_ok=True)
    arguments.c_output.write_text(
        source_text(ascent, descent, line_height, glyphs), encoding="utf-8"
    )
    arguments.header_output.write_text(header_text(), encoding="utf-8")
    print(
        f"Generated {arguments.c_output} and {arguments.header_output}: "
        f"{len(glyphs)} glyphs, {len(missing)} missing, "
        f"ascent={ascent}, descent={descent}, line_height={line_height}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
