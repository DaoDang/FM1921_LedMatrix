#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
make_vn_font.py - generate P4Panel32 bitmap fonts (ASCII + Vietnamese) from a TrueType font.

Output: one C++ header per size (e.g. DejaVuVN_12.h) that can be passed to
P4Panel32::setFont(&DejaVuVN_12).  Only the glyphs in the character set are stored
(~250 glyphs, a few KB per size).

Requirements:   pip install freetype-py            (preview also needs: pip install pillow)
Font:           DejaVu Sans is found automatically if matplotlib is installed
                (pip install matplotlib) or in the usual system font folders;
                otherwise download it from https://dejavu-fonts.github.io and pass --ttf.

Examples:
  python make_vn_font.py                                  # sizes 9 12 16 -> ../../src
  python make_vn_font.py --sizes 10 14 --out ../../src    # other sizes
  python make_vn_font.py --ttf DejaVuSans-Bold.ttf --name DejaVuBoldVN
  python make_vn_font.py --sizes 12 --preview preview.png --text "Xin chào Việt Nam"

"size" is the em size in pixels given to FreeType (like a font size in px).  The real
cell height of the generated font also has to hold the stacked Vietnamese marks and the
descenders, so it is larger: the script prints ascent/height for every size, and the
same numbers are available at run time (P4Panel32::fontHeight()).
"""
import argparse
import os
import sys

try:
    import freetype
except ImportError:
    sys.exit("freetype-py is missing:  pip install freetype-py")

# ---------------------------------------------------------------- character set
VN_LOWER = ("àáảãạ" "ăằắẳẵặ" "âầấẩẫậ" "èéẻẽẹ" "êềếểễệ" "ìíỉĩị"
            "òóỏõọ" "ôồốổỗộ" "ơờớởỡợ" "ùúủũụ" "ưừứửữự" "ỳýỷỹỵ" "đ")
VN_UPPER = VN_LOWER.upper()
assert len(VN_LOWER) == 67 and len(VN_UPPER) == 67
EXTRAS = "°±×÷·–—‘’“”…•₫€£©"


def charset(extra=""):
    chars = [chr(c) for c in range(32, 127)]
    chars += list(VN_LOWER + VN_UPPER + EXTRAS + extra)
    seen, out = set(), []
    for ch in chars:
        if ch not in seen:
            seen.add(ch)
            out.append(ch)
    return sorted(out, key=ord)


# ---------------------------------------------------------------- font lookup
def find_ttf(explicit):
    if explicit:
        if not os.path.isfile(explicit):
            sys.exit("TTF not found: %s" % explicit)
        return explicit
    cands = []
    try:
        import matplotlib
        cands.append(os.path.join(os.path.dirname(matplotlib.__file__), "mpl-data", "fonts", "ttf", "DejaVuSans.ttf"))
    except ImportError:
        pass
    cands += [
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/Library/Fonts/DejaVuSans.ttf",
        "C:\\Windows\\Fonts\\DejaVuSans.ttf",
        os.path.expanduser("~/.fonts/DejaVuSans.ttf"),
    ]
    for c in cands:
        if os.path.isfile(c):
            return c
    sys.exit("DejaVuSans.ttf not found. Download it from https://dejavu-fonts.github.io "
             "(or: pip install matplotlib) and pass --ttf <file>.")


# ---------------------------------------------------------------- rasterise
def render(face, size, chars, mode, threshold, autohint):
    """Return list of dicts (code, w, h, dx, dy, adv, rows[list of list of 0/1])."""
    face.set_pixel_sizes(0, size)
    if mode == "mono":
        flags = freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_MONO
    else:
        flags = freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_LIGHT
    if autohint:
        flags |= freetype.FT_LOAD_FORCE_AUTOHINT
    glyphs = []
    for ch in chars:
        if face.get_char_index(ord(ch)) == 0:
            print("  warning: U+%04X %r not in font, skipped" % (ord(ch), ch))
            continue
        face.load_char(ch, flags)
        slot = face.glyph
        bm = slot.bitmap
        w, h, pitch = bm.width, bm.rows, bm.pitch
        buf = bm.buffer
        rows = []
        for r in range(h):
            row = []
            for c in range(w):
                if bm.pixel_mode == freetype.FT_PIXEL_MODE_MONO:
                    bit = (buf[r * pitch + (c >> 3)] >> (7 - (c & 7))) & 1
                else:
                    bit = 1 if buf[r * pitch + c] >= threshold else 0
                row.append(bit)
            rows.append(row)
        adv = int(round(slot.advance.x / 64.0))
        dx, dy = slot.bitmap_left, -slot.bitmap_top
        if not (-128 <= dx <= 127 and -128 <= dy <= 127 and 0 <= adv <= 255 and w <= 255 and h <= 255):
            sys.exit("glyph U+%04X does not fit the 8-bit metrics; use a smaller --sizes" % ord(ch))
        glyphs.append(dict(code=ord(ch), w=w, h=h, dx=dx, dy=dy, adv=adv, rows=rows))
    return glyphs


def metrics(glyphs):
    ascent = max(max(0, -g["dy"]) for g in glyphs)
    descent = max(max(0, g["dy"] + g["h"]) for g in glyphs)
    return ascent, descent


# ---------------------------------------------------------------- pack + write
def pack(glyphs):
    data, table = bytearray(), []
    for g in glyphs:
        off = len(data)
        bits = [b for row in g["rows"] for b in row]
        for i in range(0, len(bits), 8):
            chunk = bits[i:i + 8]
            v = 0
            for j, b in enumerate(chunk):
                v |= b << (7 - j)
            data.append(v)
        table.append((off, g))
    if len(data) > 65535:
        sys.exit("bitmap too large for 16-bit offsets (%d bytes)" % len(data))
    return bytes(data), table


def write_header(path, symbol, family, size, mode, glyphs, ascent, descent):
    data, table = pack(glyphs)
    guard = symbol.upper() + "_H"
    L = []
    L.append("// %s - generated by extras/fontgen/make_vn_font.py, do not edit by hand." % symbol)
    L.append("// Source: %s, em size %d px, %s rendering, %d glyphs (ASCII + Vietnamese)." % (family, size, mode, len(glyphs)))
    L.append("// Cell: ascent %d px above the baseline + descent %d px = height %d px; %d bytes of bitmap data."
             % (ascent, descent, ascent + descent, len(data)))
    L.append("// DejaVu fonts: see NOTICE (Bitstream Vera licence, DejaVu changes are public domain).")
    L.append("#ifndef %s" % guard)
    L.append("#define %s" % guard)
    L.append('#include "P4Font.h"')
    L.append("")
    L.append("static const uint8_t %s_bitmap[] = {" % symbol)
    for i in range(0, len(data), 16):
        L.append("  " + ", ".join("0x%02X" % b for b in data[i:i + 16]) + ",")
    L.append("};")
    L.append("")
    L.append("// offset, w, h, dx, dy (from baseline, negative = above), advance")
    L.append("static const P4Glyph %s_glyphs[] = {" % symbol)
    for off, g in table:
        ch = chr(g["code"])
        label = ch if ch.isprintable() and ch not in "\\" else "U+%04X" % g["code"]
        L.append("  {%5d, %2d, %2d, %3d, %3d, %2d},   // U+%04X %s"
                 % (off, g["w"], g["h"], g["dx"], g["dy"], g["adv"], g["code"], label))
    L.append("};")
    L.append("")
    L.append("static const uint16_t %s_codes[] = {" % symbol)
    codes = [g["code"] for g in glyphs]
    for i in range(0, len(codes), 12):
        L.append("  " + ", ".join("0x%04X" % c for c in codes[i:i + 12]) + ",")
    L.append("};")
    L.append("")
    L.append("static const P4Font %s = {" % symbol)
    L.append("  %s_bitmap, %s_glyphs, %s_codes," % (symbol, symbol, symbol))
    L.append("  %d,    // glyph count" % len(glyphs))
    L.append("  %d,     // ascent: top of the cell to the baseline" % ascent)
    L.append("  %d      // height: line cell height (ascent + descent)" % (ascent + descent))
    L.append("};")
    L.append("")
    L.append("#endif")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(L) + "\n")
    return len(data)


# ---------------------------------------------------------------- preview / ascii
def layout(glyphs, ascent, height, text):
    by = {g["code"]: g for g in glyphs}
    width = sum(by.get(ord(c), by[ord("?")])["adv"] for c in text) + 2
    grid = [[0] * width for _ in range(height)]
    x = 0
    for c in text:
        g = by.get(ord(c), by[ord("?")])
        for r, row in enumerate(g["rows"]):
            for k, b in enumerate(row):
                yy, xx = ascent + g["dy"] + r, x + g["dx"] + k
                if b and 0 <= yy < height and 0 <= xx < width:
                    grid[yy][xx] = 1
        x += g["adv"]
    return grid


def ascii_art(grid):
    return "\n".join("".join("#" if v else "." for v in row) for row in grid)


def write_preview(path, blocks, scale=8):
    try:
        from PIL import Image
    except ImportError:
        print("  (preview skipped: pip install pillow)")
        return
    gap = 2
    W = max(len(b[0]) for b in blocks)
    H = sum(len(b) + gap for b in blocks)
    img = Image.new("RGB", (W * scale, H * scale), (0, 0, 0))
    px = img.load()
    y0 = 0
    for grid in blocks:
        for r, row in enumerate(grid):
            for c, v in enumerate(row):
                if v:
                    for yy in range(scale - 1):
                        for xx in range(scale - 1):
                            px[c * scale + xx, (y0 + r) * scale + yy] = (255, 190, 40)
        y0 += len(grid) + gap
    img.save(path)


# ---------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser(description="Generate P4Panel32 Vietnamese bitmap fonts from a TTF.")
    ap.add_argument("--ttf", help="TrueType font (default: DejaVuSans.ttf, found automatically)")
    ap.add_argument("--sizes", type=int, nargs="+", default=[9, 12, 16], help="em sizes in px (default 9 12 16)")
    ap.add_argument("--name", default="DejaVuVN", help="C++ symbol prefix; files are <name>_<size>.h")
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "src"),
                    help="output folder (default: ../../src)")
    ap.add_argument("--mode", choices=["mono", "gray"], default="mono",
                    help="mono = FreeType hinted 1-bit rendering (default); gray = light-hinted anti-aliased, then thresholded")
    ap.add_argument("--threshold", type=int, default=110, help="gray mode threshold 1..255 (default 110)")
    ap.add_argument("--autohint", action="store_true", help="force the FreeType auto-hinter instead of the font's own hints")
    ap.add_argument("--extra", default="", help="extra characters to include (e.g. '←→')")
    ap.add_argument("--preview", help="write a magnified PNG preview of --text for every size")
    ap.add_argument("--text", default="Xin chào Việt Nam! ẤẦẨẪẬ ệữờ 0123", help="sample text for --preview / --ascii")
    ap.add_argument("--ascii", action="store_true", help="print the sample text as ASCII art")
    a = ap.parse_args()

    ttf = find_ttf(a.ttf)
    face = freetype.Face(ttf)
    family = "%s %s" % (face.family_name.decode(), face.style_name.decode())
    chars = charset(a.extra)
    os.makedirs(a.out, exist_ok=True)
    print("Font: %s (%s)  glyphs wanted: %d" % (family, ttf, len(chars)))

    blocks = []
    for size in a.sizes:
        glyphs = render(face, size, chars, a.mode, a.threshold, a.autohint)
        ascent, descent = metrics(glyphs)
        symbol = "%s_%d" % (a.name, size)
        path = os.path.join(a.out, symbol + ".h")
        nbytes = write_header(path, symbol, family, size, a.mode, glyphs, ascent, descent)
        print("  %-14s em %2d px -> ascent %2d, descent %d, height %2d px, %d glyphs, %d bytes  -> %s"
              % (symbol, size, ascent, descent, ascent + descent, len(glyphs), nbytes, os.path.normpath(path)))
        grid = layout(glyphs, ascent, ascent + descent, a.text)
        if a.ascii:
            print(ascii_art(grid))
            print()
        blocks.append(grid)
    if a.preview:
        write_preview(a.preview, blocks)
        print("preview:", a.preview)


if __name__ == "__main__":
    main()
