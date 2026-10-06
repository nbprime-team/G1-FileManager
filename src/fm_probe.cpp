/*
 * fm_probe.cpp -- bring-up probe (make probe).
 *
 * fm_min.elf proved that a native external app runs on this firmware but that
 * __wfopen()+_fwrite() to C:\APPS\fm.log does not create a file.  This build tries every
 * plausible file-open combination and prints the verdict ON SCREEN (a compact bitmap font
 * is embedded, so nothing depends on the firmware's font API or on file IO).
 *
 * Copy to the calculator's apps directory as fm.elf (or keep the name and select it in
 * Upsilon's external app list) and read the 8 result lines.
 */
#include <stdint.h>
#include "muteki.h"
#include "besta_fixes.h"

extern "C" unsigned long SetSystemVariable(unsigned short, unsigned short, unsigned long);

/* ---------------- tiny 5x7 font (ASCII 0x20..0x7E) ---------------- */
static const unsigned char FONT5X7[95][5] = {
    {0x00,0x00,0x5f,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7f,0x14,0x7f,0x14},
    {0x24,0x2a,0x7f,0x2a,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},
    {0x00,0x05,0x03,0x00,0x00},{0x00,0x1c,0x22,0x41,0x00},{0x00,0x41,0x22,0x1c,0x00},
    {0x14,0x08,0x3e,0x08,0x14},{0x08,0x08,0x3e,0x08,0x08},{0x00,0x50,0x30,0x00,0x00},
    {0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
    {0x3e,0x51,0x49,0x45,0x3e},{0x00,0x42,0x7f,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},
    {0x21,0x41,0x45,0x4b,0x31},{0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
    {0x3c,0x4a,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},{0x36,0x49,0x49,0x49,0x36},
    {0x06,0x49,0x49,0x29,0x1e},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
    {0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},{0x00,0x41,0x22,0x14,0x08},
    {0x02,0x01,0x51,0x09,0x06},{0x32,0x49,0x79,0x41,0x3e},{0x7e,0x11,0x11,0x11,0x7e},
    {0x7f,0x49,0x49,0x49,0x36},{0x3e,0x41,0x41,0x41,0x22},{0x7f,0x41,0x41,0x22,0x1c},
    {0x7f,0x49,0x49,0x49,0x41},{0x7f,0x09,0x09,0x09,0x01},{0x3e,0x41,0x49,0x49,0x7a},
    {0x7f,0x08,0x08,0x08,0x7f},{0x00,0x41,0x7f,0x41,0x00},{0x20,0x40,0x41,0x3f,0x01},
    {0x7f,0x08,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},{0x7f,0x02,0x0c,0x02,0x7f},
    {0x7f,0x04,0x08,0x10,0x7f},{0x3e,0x41,0x41,0x41,0x3e},{0x7f,0x09,0x09,0x09,0x06},
    {0x3e,0x41,0x51,0x21,0x5e},{0x7f,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
    {0x01,0x01,0x7f,0x01,0x01},{0x3f,0x40,0x40,0x40,0x3f},{0x1f,0x20,0x40,0x20,0x1f},
    {0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},
    {0x61,0x51,0x49,0x45,0x43},{0x00,0x7f,0x41,0x41,0x00},{0x02,0x04,0x08,0x10,0x20},
    {0x00,0x41,0x41,0x7f,0x00},{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
    {0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},{0x7f,0x48,0x44,0x44,0x38},
    {0x38,0x44,0x44,0x44,0x20},{0x38,0x44,0x44,0x48,0x7f},{0x38,0x54,0x54,0x54,0x18},
    {0x08,0x7e,0x09,0x01,0x02},{0x0c,0x52,0x52,0x52,0x3e},{0x7f,0x08,0x04,0x04,0x78},
    {0x00,0x44,0x7d,0x40,0x00},{0x20,0x40,0x44,0x3d,0x00},{0x7f,0x10,0x28,0x44,0x00},
    {0x00,0x41,0x7f,0x40,0x00},{0x7c,0x04,0x18,0x04,0x78},{0x7c,0x08,0x04,0x04,0x78},
    {0x38,0x44,0x44,0x44,0x38},{0x7c,0x14,0x14,0x14,0x08},{0x08,0x14,0x14,0x18,0x7c},
    {0x7c,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},{0x04,0x3f,0x44,0x40,0x20},
    {0x3c,0x40,0x40,0x20,0x7c},{0x1c,0x20,0x40,0x20,0x1c},{0x3c,0x40,0x30,0x40,0x3c},
    {0x44,0x28,0x10,0x28,0x44},{0x0c,0x50,0x50,0x50,0x3c},{0x44,0x64,0x54,0x4c,0x44},
    {0x00,0x08,0x36,0x41,0x00},{0x00,0x00,0x7f,0x00,0x00},{0x00,0x41,0x36,0x08,0x00},
    {0x08,0x04,0x08,0x10,0x08},
};

static void px(int x, int y, int color) {
    if (x < 0 || y < 0 || x > 319 || y > 239) return;
    rgbSetColor(color);
    FillRect((short)x, (short)y, (short)x, (short)y, 0);
}

static void fill(int x, int y, int w, int h, int color) {
    if (w <= 0 || h <= 0) return;
    rgbSetColor(color);
    FillRect((short)x, (short)y, (short)(x + w - 1), (short)(y + h - 1), 0);
}

/* Four renderer variants so the legibility question is settled on the device instead of
 * by guessing: bit order within a column byte (bit0 = top row vs bit0 = bottom row) and
 * how a pixel is put on the panel (one FillRect per pixel vs one per row segment). */
#define RENDER_BIT6 0 /* bit 6 = top row */
#define RENDER_BIT0 1 /* bit 0 = top row */
#define RENDER_PER_PX 0
#define RENDER_PER_ROW 1

/* The 5x7 table can be read two ways; draw both so one look settles it. */
static void draw_ascii_ex(int x, int y, const char *s, int color, int mode, int fillmode) {
    rgbSetColor(color);
    for (; *s; ++s, x += 6) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x20 || c > 0x7E) c = '?';
        const unsigned char *g = FONT5X7[c - 0x20];
        for (int row = 0; row < 7; ++row) {
            int bit = (mode == RENDER_BIT6) ? (6 - row) : row;
            if (fillmode == RENDER_PER_PX) {
                for (int col = 0; col < 5; ++col)
                    if (g[col] & (1 << bit))
                        FillRect((short)(x + col), (short)(y + row), (short)(x + col),
                                 (short)(y + row), 0);
            } else {
                int col = 0;
                while (col < 5) {
                    if (!(g[col] & (1 << bit))) {
                        ++col;
                        continue;
                    }
                    int col2 = col;
                    while (col2 + 1 < 5 && (g[col2 + 1] & (1 << bit))) ++col2;
                    FillRect((short)(x + col), (short)(y + row), (short)(x + col2),
                             (short)(y + row), 0);
                    col = col2 + 1;
                }
            }
        }
    }
}

/* Confirmed on hardware: bit 6 of a column byte is the TOP row (variant A/B in the grid
 * below).  bit0 would draw every glyph upside down, which reads as "the letters are shifted
 * by one" because a flipped C looks like a D, e like an f, l like an m, ... */
static void draw_ascii(int x, int y, const char *s, int color) {
    draw_ascii_ex(x, y, s, color, RENDER_BIT6, RENDER_PER_PX);
}

/* ---------------- probe ---------------- */
#define MAXLINES 12
static char g_lines[MAXLINES][40];
static int g_nlines = 0;

static void add(const char *a, const char *b) {
    if (g_nlines >= MAXLINES) return;
    int i = 0;
    for (; a[i] && i < 20; ++i) g_lines[g_nlines][i] = a[i];
    for (int k = 0; b[k] && i < 39; ++k) g_lines[g_nlines][i++] = b[k];
    g_lines[g_nlines][i] = 0;
    ++g_nlines;
}

static UTF16 *u16(const char *s, int slot) {
    static UTF16 bufs[2][80];
    UTF16 *out = bufs[slot ? 1 : 0];
    int o = 0;
    if (!s) s = "";
    for (int i = 0; s[i] && o < 78; ++i) out[o++] = (UTF16)(unsigned char)s[i];
    out[o] = 0;
    return out;
}

/* Try one fopen flavour.  Returns 1 on success, 0 when open failed, 2 when the open
 * worked but the write did not. */
static int try_wfopen(const char *path, const char *mode, int write_char) {
    file_descriptor_t *fd = __wfopen(u16(path, 0), u16(mode, 1));
    if (!fd) return 0;
    char msg[24];
    int n = 0;
    for (const char *p = "probe "; *p; ++p) msg[n++] = *p;
    msg[n++] = write_char;
    msg[n++] = '\n';
    msg[n] = 0;
    size_t wrote = _fwrite(msg, 1, (size_t)n, fd);
    _fclose(fd);
    return wrote == (size_t)n ? 1 : 2;
}

static int try_afopen(const char *path, const char *mode) {
    file_descriptor_t *fd = _afopen(path, mode);
    if (!fd) return 0;
    const char msg[] = "probe 8\n";
    size_t wrote = _fwrite(msg, 1, sizeof(msg) - 1, fd);
    _fclose(fd);
    return wrote == sizeof(msg) - 1 ? 1 : 2;
}

static int try_createfile(const char *path, unsigned access, unsigned on_noentry) {
    devio_descriptor_t *fd = CreateFile(path, access, 0, 0, on_noentry, 0, 0);
    if (!fd || fd == (devio_descriptor_t *)0xffffffff) return 0;
    const char msg[] = "probe 9\n";
    size_t actual = 0;
    bool ok = WriteFile(fd, msg, sizeof(msg) - 1, &actual, 0);
    CloseHandle(fd);
    if (!ok) return 2;
    return actual == sizeof(msg) - 1 ? 1 : 2;
}

static const char *verdict(int r) {
    return r == 1 ? " OK" : (r == 2 ? " NOWRITE" : " NOOPEN");
}

/* Phase 3: verify the exact strategy the boot logger uses.
 *
 * Phase 1/2 on hardware: __wfopen(...,"wb") works once per session; append mode does not;
 * a second writable open later in the same session fails.  So this opens ONE handle, writes
 * several lines through it (flushing between them), closes it once, then reads the file back
 * through a single _afopen(...,"rb") and shows the first line on the panel.  If the read-back
 * text appears, the logger strategy is proven. */
static char g_readback[128];
static int g_readback_len = 0;
static int g_step_log = 0; /* 0 = open failed, 1 = wrote, 2 = close failed, 3 = read failed */

static void phase3(void) {
    file_descriptor_t *fd = __wfopen(u16("C:\\APPS\\fm.log", 0), u16("wb", 1));
    if (!fd) {
        g_step_log = 0;
        return;
    }
    static const char *lines[] = { "--- fm probe 3 ---\n",
                                  "line 2 written through the same handle\n",
                                  "line 3 written through the same handle\n" };
    for (int i = 0; i < 3; ++i) {
        int n = 0;
        while (lines[i][n]) ++n;
        _fwrite(lines[i], 1, (size_t)n, fd);
        __fflush(fd);
    }
    g_step_log = 1;
    if (_fclose(fd) != 0) g_step_log = 2;

    file_descriptor_t *rd = _afopen("C:\\APPS\\fm.log", "rb");
    if (!rd) {
        g_step_log = 3;
        return;
    }
    g_readback_len = (int)_fread(g_readback, 1, sizeof(g_readback) - 1, rd);
    _fclose(rd);
    if (g_readback_len < 0) g_readback_len = 0;
    g_readback[g_readback_len] = 0;
}

volatile unsigned g_last_probe_key = 0;

static void pr_scpy(char *d, const char *s, int cap) {
    int i = 0;
    for (; s[i] && i < cap - 1; ++i) d[i] = s[i];
    d[i] = 0;
}
static void pr_scat(char *d, const char *s, int cap) {
    int n = 0;
    while (d[n]) ++n;
    pr_scpy(d + n, s, cap - n);
}
static void pr_u2hex(unsigned v, int digits, char *out) {
    const char *h = "0123456789ABCDEF";
    for (int i = digits - 1; i >= 0; --i) {
        out[i] = h[v & 0xF];
        v >>= 4;
    }
}

/* poll the event queue so the panel can show which keycode this firmware reports */
static int probe_key_thread(void *) {
    ClearAllEvents();
    ui_event_t e;
    for (;;) {
        if (GetEvent(&e)) {
            unsigned *ptr = (unsigned *)&e;
            if (ptr[7] == 0x10) g_last_probe_key = ptr[8] >> 16;
            else if (ptr[7] == 0x100000) g_last_probe_key = 0;
        }
    }
    return 0;
}

int main() {
    SetSystemVariable(85, 2, 1);
    OSCreateThread(probe_key_thread, 0, 0, 0);

    /* 1..4: wide fopen on the two candidate paths, append then write mode */
    add("1 wfopen C:\\APPS\\fm.log ab", verdict(try_wfopen("C:\\APPS\\fm.log", "ab", '1')));
    add("2 wfopen C:\\APPS\\fm.log wb", verdict(try_wfopen("C:\\APPS\\fm.log", "wb", '2')));
    add("3 wfopen C:\\fm.log ab", verdict(try_wfopen("C:\\fm.log", "ab", '3')));
    add("4 wfopen A:\\fm.log wb", verdict(try_wfopen("A:\\fm.log", "wb", '4')));
    /* 5: wide fopen with an explicit directory creation attempt first */
    _wmkdir(u16("C:\\APPS", 0));
    add("5 mkdir+ wfopen C:\\APPS ab", verdict(try_wfopen("C:\\APPS\\fm.log", "ab", '5')));
    /* 6: DOS 8.3 fopen (the same call besta_fixes uses for _stat) */
    add("6 afopen C:\\fm8.log wb", verdict(try_afopen("C:\\fm8.log", "wb")));
    /* 7/8: CreateFile + WriteFile with two access/disposition guesses */
    add("7 CreateFile acc3 disp3", verdict(try_createfile("C:\\fmc.log", 3, 3)));
    add("8 CreateFile acc0x10 disp2", verdict(try_createfile("C:\\fmc2.log", 0x10, 2)));

    phase3();

    for (;;) {
        fill(0, 0, 320, 240, 0xFFFFFF);
        fill(0, 0, 320, 12, 0x1E3A8A);
        draw_ascii_ex(2, 2, "FONT + KEY TEST", 0xFFFFFF, RENDER_BIT6, RENDER_PER_PX);

        /* rows 1 and 2 use the two bit orders of the same table */
        draw_ascii_ex(2, 16, "V1", 0x6B7280, RENDER_BIT6, RENDER_PER_PX);
        draw_ascii_ex(30, 16, "ABCDEFG abcdefg 0123", 0x111111, RENDER_BIT6, RENDER_PER_PX);
        draw_ascii_ex(2, 29, "V2", 0x6B7280, RENDER_BIT6, RENDER_PER_PX);
        draw_ascii_ex(30, 29, "ABCDEFG abcdefg 0123", 0x111111, RENDER_BIT0, RENDER_PER_PX);

        /* the full printable range, current renderer */
        {
            static const char all[] =
                " !\"#$%&'()*+,-./0123456789:;<=>?"
                "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
                "`abcdefghijklmnopqrstuvwxyz{|}~";
            char line[40];
            int row = 0;
            for (int i = 0; all[i] && row < 3; ++row) {
                int n = 0;
                while (all[i] && n < 32) line[n++] = all[i++];
                line[n] = 0;
                draw_ascii_ex(2, 44 + row * 13, line, 0x111111, RENDER_BIT6, RENDER_PER_PX);
            }
        }

        /* live keycode readout: press a few keys and read the code here */
        {
            char msg[48];
            pr_scpy(msg, "last key = 0x", sizeof(msg));
            char hx[4];
            pr_u2hex(g_last_probe_key, 2, hx);
            pr_scat(msg, hx, sizeof(msg));
            draw_ascii_ex(2, 92, msg, 0x1D4ED8, RENDER_BIT6, RENDER_PER_PX);
        }

        /* write verdicts (from the earlier probe phases) */
        for (int i = 0; i < g_nlines && i < 3; ++i)
            draw_ascii_ex(2, 108 + i * 13, g_lines[i], 0x111111, RENDER_BIT6, RENDER_PER_PX);
        OSSleep(2500);
    }
    return 0;
}
