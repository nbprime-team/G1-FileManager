#!/usr/bin/env python3
"""Build the fm.elf UI font from GNU Unifont's official hexadecimal bitmap source.

Unifont glyphs are 16 pixels tall; ASCII/Latin glyphs are 8 pixels wide (one byte per row)
and CJK/wide glyphs are 16 pixels wide (two bytes per row, big endian).  Every selected
glyph is stored as 32 bytes (16 rows x uint16, MSB first) -- the same encoding Unifont's
.hex lines use for 16-wide glyphs -- so the renderer needs a single code path.

The lookup structure is a *sparse page table* rather than one entry per codepoint run: the
app window is only 1 MB (0x30900000..0x31000000), and a flat run table for a full CJK set
costs ~170 KB while this page table costs ~35 KB.  Layout of the generated blob:

    0  magic 'FU01'
    4  u32 page_count
    8  u32 bitmap_bytes
   12  u32 header_size          (offset of the glyph payload)
   16  bitmap[bitmap_bytes]     bit (pg & 7) of byte (pg >> 3) -> page present
       u8  page_no[page_count][3]       page number, big endian
       u16 dir_block_off[page_count]    byte offset inside the glyph directory area
       u32 data_block_off[page_count]   byte offset inside the glyph payload
       u8 dir[]                         per page: slot inside the page block for the
                                        codepoint low byte, or 0xFF when absent
       payload                          page_count blocks, each n_glyphs * 32 bytes

A glyph slot is the global glyph index, so page directory entries stay under 65536.

Outputs (all under --outdir): unifont.bin (blob), unifont_font.h (constants),
unifont_paths.inc (the .incbin fragment used by src/unifont_data.s).
"""
import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

DEFAULT_HEX = "/usr/share/unifont/unifont.hex"
MAGIC = b"FU01"
GLYPH_BYTES = 32

# Built-in coverage: ASCII/Latin/symbols plus the full CJK Unified Ideographs block.
DEFAULT_COVERAGE = [
    (0x0020, 0x007E), (0x00A0, 0x024F), (0x0250, 0x02AF), (0x02B0, 0x02FF),
    (0x0300, 0x036F), (0x0370, 0x03FF), (0x0400, 0x052F), (0x2000, 0x206F),
    (0x2070, 0x209F), (0x20A0, 0x20CF), (0x2100, 0x214F), (0x2150, 0x218F),
    (0x2190, 0x21FF), (0x2200, 0x22FF), (0x2300, 0x23FF), (0x2460, 0x24FF),
    (0x2500, 0x257F), (0x2580, 0x259F), (0x25A0, 0x25FF), (0x2600, 0x26FF),
    (0x2700, 0x27BF), (0x27C0, 0x27FF), (0x2E80, 0x2EFF), (0x2F00, 0x2FDF),
    (0x3000, 0x303F), (0x3040, 0x309F), (0x30A0, 0x30FF), (0x3100, 0x312F),
    (0x31C0, 0x31EF), (0x3200, 0x32FF), (0x3400, 0x4DBF), (0x4E00, 0x9FFF),
    (0xF900, 0xFAFF), (0xFE30, 0xFE4F), (0xFF00, 0xFFEF),
]


def parse_ranges(path):
    out = []
    if not path:
        return out
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            for tok in line.replace(",", " ").split():
                if "-" in tok:
                    a, b = tok.split("-", 1)
                    out.append((int(a, 16), int(b, 16)))
                else:
                    cp = int(tok, 16)
                    out.append((cp, cp))
    return out


def parse_extra(path):
    if not path:
        return []
    return [(ord(c), ord(c)) for c in open(path, encoding="utf-8").read() if ord(c) > 0x7F]


def normalize(ranges):
    out = []
    for a, b in sorted(ranges):
        if a > b:
            a, b = b, a
        if out and a <= out[-1][1] + 1:
            out[-1][1] = max(out[-1][1], b)
        else:
            out.append([a, b])
    return [(a, b) for a, b in out]


def canonical(hexdata):
    """32 bytes = 16 rows of uint16 in little endian memory order.

    Unifont writes each row big endian (pixel bits first) while the glyph table is read as
    uint16 on a little endian ARM, so each byte pair is swapped: memory becomes
    [low byte, high byte].  An 8 pixel wide glyph (16 hex bytes) has the row value in the
    LOW byte, which is also how the renderer tells narrow glyphs from wide ones."""
    raw = bytes.fromhex(hexdata)
    out = bytearray(GLYPH_BYTES)
    if len(raw) == 16:                      # narrow: row value in bytes [0]
        for r in range(16):
            out[r * 2] = raw[r]
        return bytes(out)
    if len(raw) == 32:                      # wide: swap the two bytes of every row
        for r in range(16):
            out[r * 2] = raw[r * 2 + 1]
            out[r * 2 + 1] = raw[r * 2]
        return bytes(out)
    raise ValueError("unexpected glyph length %d" % len(raw))


def build(hex_path, ranges, outdir):
    covered = set()
    for a, b in ranges:
        covered.update(range(a, b + 1))

    glyphs = {}
    with open(hex_path, encoding="ascii", errors="ignore") as f:
        for line in f:
            if ":" not in line:
                continue
            left, hexdata = line.split(":", 1)
            try:
                cp = int(left, 16)
            except ValueError:
                continue
            if cp not in covered:
                continue
            try:
                glyphs[cp] = canonical(hexdata.strip())
            except ValueError:
                continue

    cps = sorted(glyphs)
    total = len(cps)
    if not cps:
        raise SystemExit("no glyphs selected -- check --coverage/--hex")

    by_page = {}
    for slot, cp in enumerate(cps):
        by_page.setdefault(cp >> 8, []).append((cp & 0xFF, slot))
    pages = sorted(by_page)
    page_count = len(pages)

    bitmap = bytearray((pages[-1] >> 3) + 1)
    for pg in pages:
        bitmap[pg >> 3] |= 1 << (pg & 7)

    dir_blocks = bytearray()
    dir_off, data_off = [], []
    data_cursor = 0
    for pg in pages:
        entries = by_page[pg]
        dir_off.append(len(dir_blocks))
        data_off.append(data_cursor)
        row = bytearray(b"\xff" * (entries[-1][0] + 1))
        for k, (lo, _slot) in enumerate(entries):
            row[lo] = k          # slot inside this page (<= 256 glyphs per page)
        dir_blocks += row
        data_cursor += len(entries) * GLYPH_BYTES

    payload = bytearray()
    for pg in pages:
        for _lo, slot in by_page[pg]:
            payload += glyphs[cps[slot]]

    os.makedirs(outdir, exist_ok=True)
    bin_path = os.path.join(outdir, "unifont.bin")
    with open(bin_path, "wb") as f:
        f.write(MAGIC)
        f.write(struct.pack("<III", page_count, len(bitmap),
                            16 + len(bitmap) + 9 * page_count + len(dir_blocks)))
        f.write(bytes(bitmap))
        for pg in pages:
            f.write(bytes(((pg >> 16) & 0xFF, (pg >> 8) & 0xFF, pg & 0xFF)))
        for off in dir_off:
            f.write(struct.pack("<H", off))
        for off in data_off:
            f.write(struct.pack("<I", off))
        f.write(bytes(dir_blocks))
        f.write(bytes(payload))

    header_size = 16 + len(bitmap) + 9 * page_count + len(dir_blocks)
    with open(os.path.join(outdir, "unifont_paths.inc"), "w", encoding="utf-8") as f:
        f.write('    .incbin "%s"\n' % os.path.relpath(bin_path, ROOT))

    with open(os.path.join(outdir, "unifont_font.h"), "w", encoding="utf-8") as f:
        f.write("// Generated by tools/gen_font.py from GNU Unifont -- DO NOT EDIT.\n")
        f.write("#pragma once\n#include <stdint.h>\n\n")
        f.write("#define FM_FONT_GLYPH_H 16\n")
        f.write("#define FM_FONT_GLYPH_BYTES %d\n" % GLYPH_BYTES)
        f.write("#define FM_FONT_GLYPH_COUNT %d\n" % total)
        f.write("#define FM_FONT_PAGE_COUNT %d\n" % page_count)
        f.write("#define FM_FONT_BITMAP_BYTES %d\n" % len(bitmap))
        f.write("#define FM_FONT_SIZE %d\n" % (header_size + len(payload)))
        f.write("#define FM_FONT_HDR_BITMAP 16\n")
        f.write("#define FM_FONT_HDR_PAGES (FM_FONT_HDR_BITMAP + FM_FONT_BITMAP_BYTES)\n")
        f.write("#define FM_FONT_HDR_DIRBLOCKS (FM_FONT_HDR_PAGES + 3 * FM_FONT_PAGE_COUNT)\n")
        f.write("#define FM_FONT_HDR_DATABLOCKS %d\n" %
                (16 + len(bitmap) + 5 * page_count))
        f.write("#define FM_FONT_HDR_DIR %d\n" % (16 + len(bitmap) + 9 * page_count))
        f.write("#define FM_FONT_PAYLOAD_OFF %d\n" % header_size)
        f.write("\n/* the blob lives in src/unifont_data.s, so it has C linkage */\n")
        f.write('#ifdef __cplusplus\nextern "C" {\n#endif\n')
        f.write("extern const uint8_t fm_font_blob[];\n")
        f.write('#ifdef __cplusplus\n}\n#endif\n')

    print("glyphs : %6d   payload %8.1f KB" % (total, len(payload) / 1024.0))
    print("index  : bitmap %5.1f KB + pages %7.1f KB" %
          (len(bitmap) / 1024.0, (header_size - 16 - len(bitmap)) / 1024.0))
    print("total  : %8.1f KB -> %s" % ((header_size + len(payload)) / 1024.0, bin_path))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hex", default=DEFAULT_HEX)
    ap.add_argument("--coverage", default=None, help="range file, one range per line (hex)")
    ap.add_argument("--coverage-only", action="store_true",
                    help="use only --coverage/--extra, ignore the built-in coverage")
    ap.add_argument("--extra", default=None, help="file of extra characters to include")
    ap.add_argument("--outdir", default=os.path.join(ROOT, "build", "font"))
    args = ap.parse_args()

    if not os.path.exists(args.hex):
        print("Unifont source %s not found; install it with: apt-get install unifont"
              % args.hex, file=sys.stderr)
        return 1
    base = [] if args.coverage_only else list(DEFAULT_COVERAGE)
    build(args.hex, normalize(base + parse_ranges(args.coverage) + parse_extra(args.extra)),
          args.outdir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
