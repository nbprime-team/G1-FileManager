#!/usr/bin/env python3
"""Render strings with the embedded Unifont blob as terminal ASCII art.

Verifies build/font-<tier>/unifont.bin without a calculator: same page-table walk and same
bitmap interpretation as fm_font_lookup()/draw_glyph() in src/fm.cpp.

Usage:  tools/preview_render.py --tier f "项目 Copy ← ─ 1.5K"
        tools/preview_render.py --width 320 "..."      # emulate row truncation
"""
import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


class Font:
    def __init__(self, path):
        self.b = open(path, "rb").read()
        assert self.b[:4] == b"FU01", "bad blob magic"
        self.page_count, self.bitmap_bytes, self.hdr = struct.unpack_from("<III", self.b, 4)
        self.bitmap = 16
        # offsets: bitmap, then dir_block_off[], then data_block_off[], then the directory
        self.pagenos = self.bitmap + self.bitmap_bytes
        self.dirblocks = self.pagenos + self.page_count * 3
        self.datablocks = self.dirblocks + self.page_count * 2
        self.dirdata = self.datablocks + self.page_count * 4
        self.pages = []
        for i in range(self.page_count):
            o = self.pagenos + i * 3
            self.pages.append((self.b[o] << 16) | (self.b[o + 1] << 8) | self.b[o + 2])

    def lookup(self, cp):
        page = cp >> 8
        if not (self.b[self.bitmap + (page >> 3)] & (1 << (page & 7))):
            return None
        lo, hi = 0, self.page_count - 1
        while lo <= hi:
            mid = (lo + hi) // 2
            if page < self.pages[mid]:
                hi = mid - 1
            elif page > self.pages[mid]:
                lo = mid + 1
            else:
                dir_off = struct.unpack_from("<H", self.b, self.dirblocks + mid * 2)[0]
                data_off = struct.unpack_from("<I", self.b, self.datablocks + mid * 4)[0]
                slot = self.b[self.dirdata + dir_off + (cp & 0xFF)]
                if slot == 0xFF:
                    return None
                o = self.hdr + data_off + slot * 32
                return [struct.unpack_from(">H", self.b, o + r * 2)[0] for r in range(16)]
        return None

    def width(self, rows):
        return 16 if any(r & 0xFF00 for r in rows) else 8

    def text_width(self, s):
        w = 0
        for ch in s:
            rows = self.lookup(ord(ch))
            w += self.width(rows) if rows else 8
        return w

    def render(self, s, max_w=None):
        canvas = [["."] * 0 for _ in range(16)]
        total = 0
        for ch in s:
            rows = self.lookup(ord(ch))
            if rows is None:
                rows = [0x3C00, 0x4200, 0x4200, 0x4200, 0x4200, 0x4200, 0x3C00] + [0] * 9
            w = self.width(rows)
            if max_w is not None and total + w > max_w:
                break
            if len(canvas[0]) < total:
                pass
            total += w
            for r in range(16):
                row = canvas[r]
                while len(row) < total:
                    row.append(".")
                bits = rows[r]
                for c in range(w):
                    if (bits >> (15 - c)) & 1:
                        row[total - w + c] = "#"
        for r in range(16):
            while len(canvas[r]) < total:
                canvas[r].append(".")
        return canvas


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tier", default="f")
    ap.add_argument("--width", type=int, default=None, help="truncate to this pixel width")
    ap.add_argument("strings", nargs="+")
    args = ap.parse_args()
    path = os.path.join(ROOT, "build", "font-%s" % args.tier, "unifont.bin")
    if not os.path.exists(path):
        print("missing %s (run: make FONT_TIER=%s font)" % (path, args.tier), file=sys.stderr)
        return 1
    f = Font(path)
    print("blob %s: %d glyph pages, %d bytes" % (path, f.page_count, len(f.b)))
    for s in args.strings:
        print("=== %r  (%d px wide)" % (s, f.text_width(s)))
        for row in f.render(s, args.width):
            print("   " + "".join(row).replace(".", " "))
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
