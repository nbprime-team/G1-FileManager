/* Host test harness for the fm.cpp font layer.
 *
 * Includes src/fm.cpp with -Dmain=fm_main so the firmware entry point does not collide
 * with the host main() below, then exercises the Unifont accessors and the text renderer
 * against a decoded framebuffer.
 *
 * Build/run with tools/run_host_test.sh -- this file is never part of fm.elf.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* one character buffer wide enough for the whole screen */
static int fb_w = 320, fb_h = 240;
static unsigned char *g_rgb; /* 3 bytes per pixel */

static inline void host_set(int x, int y, int r, int g, int b) {
    if (x < 0 || y < 0 || x >= fb_w || y >= fb_h) return;
    unsigned char *p = g_rgb + (y * fb_w + x) * 3;
    p[0] = (unsigned char)r;
    p[1] = (unsigned char)g;
    p[2] = (unsigned char)b;
}

static int g_cur = 0;

#define main fm_main
#include "../src/fm.cpp"
#undef main

/* SDK shim implementations (need the types declared by the shim headers above). */
#include "shim_impl.inc"

/* ---- test: render sample text and dump it as ASCII art ---- */
static void dump_text(const char *s, int top) {
    memset(g_rgb, 0, (size_t)fb_w * fb_h * 3);
    draw_text(0, top, s, 0xFFFFFF);
    int w = text_width(s);
    printf("=== %s  (%d px wide)\n", s, w);
    for (int y = 0; y < top + 17 && y < fb_h; ++y) {
        char line[400];
        int n = 0;
        for (int x = 0; x < w && x < 200; ++x) line[n++] = g_rgb[(y * fb_w + x) * 3] ? '#' : '.';
        line[n] = 0;
        printf("   %s\n", line);
    }
    printf("\n");
}

static int failures = 0;

static void check(int cond, const char *what) {
    printf("%-58s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) ++failures;
}

int main(void) {
    g_rgb = (unsigned char *)calloc((size_t)fb_w * fb_h * 3, 1);
    if (!g_rgb) return 2;

    check(UF_COUNT > 90, "UI glyph table loaded (ASCII + extras)");
    check(GB_HAN_COUNT > 7000, "GB2312 glyph table loaded");

    /* every GB2312 byte pair either maps to a character we can draw or is marked unused */
    {
        int mapped = 0, drawn = 0;
        for (int hi = 0xA1; hi <= 0xF7; ++hi)
            for (int lo = 0xA1; lo <= 0xFE; ++lo) {
                unsigned cp = gb2312_map[(hi - 0xA1) * 94 + (lo - 0xA1)];
                if (cp == 0xFFFD) continue;
                ++mapped;
                if (glyph_of(cp) || gb_glyph_of(cp)) ++drawn;
            }
        printf("%-58s %d mapped, %d drawable\n", "GB2312 pairs", mapped, drawn);
        check(mapped > 7000, "GB2312 map is populated");
        check(drawn == mapped, "every mapped GB2312 character has a glyph");
    }

    /* GB2312 decoding of a known string: 中 = 0xD6D0, 文 = 0xCEC4 */
    {
        const unsigned char zhong[] = { 0xD6, 0xD0 };
        const unsigned char wen[] = { 0xCE, 0xC4 };
        int used = 0;
        unsigned a1 = gb2312_decode(zhong, &used);
        unsigned b1 = gb2312_decode(wen, &used);
        printf("%-58s U+%04X U+%04X\n", "GB2312 decode of 中文", a1, b1);
        check(a1 == 0x4E2D && b1 == 0x6587, "GB2312 decode matches Unicode");
        check(gb_glyph_of(a1) != 0 && gb_glyph_of(b1) != 0, "decoded glyphs are present");
    }

    /* UTF-8 detection drives the viewer's decoder choice */
    {
        const unsigned char utf8_cjk[] = { 0xE4, 0xB8, 0xAD };
        const unsigned char gbk_cjk[] = { 0xD6, 0xD0 };
        check(is_utf8(utf8_cjk, 3), "valid UTF-8 detected");
        check(!is_utf8(gbk_cjk, 2), "GB2312 rejected by the UTF-8 check");
    }

    /* advances: ASCII 6 px, CJK 12 px */
    check(text_width("ABC") == 24, "text_width(\"ABC\") == 24 (8 px per ASCII cell)");
    check(text_width("\xE9\xA1\xB9") == 16, "text_width(CJK) == 16 (full width cell)");
    check(text_width("A" "\xE9\xA1\xB9" "B") == 32, "text_width(A + CJK + B) == 32");

    /* fit_text keeps within budget and appends the suffix */
    {
        char out[128];
        fit_text("averyveryverylongfilename.txt", 80, "~", out, sizeof(out));
        check(text_width(out) <= 80, "fit_text respects the pixel budget");
        check(strlen(out) > 0 && out[strlen(out) - 1] == '~', "fit_text appends suffix");
        fit_text("ab", 100, "~", out, sizeof(out));
        check(strcmp(out, "ab") == 0, "fit_text keeps text that already fits");
    }

    /* render a mixed line and confirm pixels landed on the baseline rows */
    memset(g_rgb, 0, (size_t)fb_w * fb_h * 3);
    draw_text(0, 0, "A\xE9\xA1\xB9", 0xFFFFFF);
    int ink_top = 99, ink_bot = -1;
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 24; ++x)
            if (g_rgb[(y * fb_w + x) * 3]) {
                if (y < ink_top) ink_top = y;
                if (y > ink_bot) ink_bot = y;
            }
    check(ink_top >= 0 && ink_bot <= 15, "draw_text stays inside the 16px cell");

    dump_text("Copy 1.5K", 0);
    dump_text("\xE9\xA1\xB9\xE7\x9B\xAE" " 1.5K", 0);
    dump_text("café Ünïcödé", 0);

    printf("\n%s (%d failure%s)\n", failures ? "HOST TEST FAILED" : "HOST TEST PASSED", failures,
           failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
