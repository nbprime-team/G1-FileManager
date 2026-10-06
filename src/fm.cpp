/*
 * fm.cpp -- native (Upsilon external app) rewrite of the HP Prime FileManager.hpappdir.
 *
 * This is a port of the Python file manager (FileManager.hpappdir/fm.py) to C++ on top of
 * the G1 SDK (Project Muteki syscalls + besta_fixes).  Layout, colours, key bindings and
 * behaviour follow the Python original; the only intended visual change is the text size:
 * everything is drawn with an embedded 5x7 ASCII font / 12px CJK font ("small font")
 * instead of the firmware's default text size.
 *
 * Build:  make            -> obj/armfir.elf + fm.elf (external app for a:\programs\misc)
 *         make firmware   -> armfir.elf built at 0x30600000
 *
 * Keys (HP Prime physical keycodes as reported by Besta's ui_event primitive -- see KMAP_*
 * below and the -DFM_KEYMAP_GETKEY switch if a different table is needed).
 */
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "muteki.h"
#include "besta_fixes.h"

extern "C" {
/* Declared by the SDK but missing from libsyscalls.a's header set. */
extern int _wfcopy(const UTF16 *src, const UTF16 *dst);
/* Backlight control, used by the SDK's own hello/event samples only as declarations. */
extern void LCDOff(void);
extern void LCDOn(void);
/* Flush a file opened with __wfopen (declared in muteki/file.h, repeated here so the host
 * shim does not need the whole header). */
extern int __fflush(file_descriptor_t *stream);
/* SetSystemVariable(85, 2, 1) -- SYSVAR_SYSPWCTRL / SET / TRUE -- is called by every SDK
 * sample to keep the app in the foreground; no header declares it. */
extern unsigned long SetSystemVariable(unsigned short type, unsigned short mode, unsigned long value);
}

/* ============================================================================
 * 0. Syscall stubs that the SDK headers do not declare
 * ============================================================================ */
/* see src/wfcopy.s: _wfcopy 0x10276 */


/* Tiny UTF-8 -> UTF-16 helper for the logger (the full converter lives in section 3).
 * `slot` selects one of three static buffers. */

static UTF16 *utf16_of_static(const char *s, int slot) {
    static UTF16 bufs[2][64];
    UTF16 *out = bufs[slot ? 1 : 0];
    int o = 0;
    if (!s) s = "";
    for (int i = 0; s[i] && o < 62; ++i) out[o++] = (UTF16)(unsigned char)s[i];
    out[o] = 0;
    return out;
}

/* ============================================================================
 * 0b. File log
 *
 * The app reboots before anything can be read off the screen on some firmware builds, so
 * every startup stage is appended to a log file instead:  C:\APPS\fm.log
 * (with fm.log and A:\APPS\fm.log as fallbacks).  Both use only SDK file calls:
 * __wfopen / _fwrite / _fclose.  Only the first ~700 bytes are kept and no log call may
 * itself be able to fault (every descriptor is checked before it is used).
 * ============================================================================ */
#ifdef FM_NO_LOG
#define LOG(msg)
#else
#define LOG(msg) log_line(msg)
#endif

static file_descriptor_t *g_log_fd = 0;
static int g_log_state = 0; /* 0 = untried, 1 = open, -1 = failed */

/* On-device probe result: __wfopen("C:\\APPS\\fm.log", "wb") works, append mode does not, and
 * a second writable open in the same session fails.  So the file is opened ONCE and the
 * handle is kept open for the whole session; every line is written immediately (plus a
 * best-effort flush) so the card holds the trace even if the firmware reboots underneath. */
static void log_line(const char *msg) {
    static char buf[1024];
    static int used = 0;
    if (g_log_state == 0) {
        g_log_state = -1;
        _wmkdir(utf16_of_static("C:\\APPS", 1));
        g_log_fd = __wfopen(utf16_of_static("C:\\APPS\\fm.log", 0),
                            utf16_of_static("wb", 1));
        if (!g_log_fd)
            g_log_fd = __wfopen(utf16_of_static("C:\\fm.log", 0), utf16_of_static("wb", 1));
        if (g_log_fd) g_log_state = 1;
    }
    if (g_log_state != 1 || !g_log_fd) return;
    int n = 0;
    for (; msg[n] && used < (int)sizeof(buf) - 1; ++n) buf[used++] = msg[n];
    if (n) {
        _fwrite(buf, 1, (size_t)used, g_log_fd);
        __fflush(g_log_fd);
        used = 0;
    }
}

/* ============================================================================
 * 1. Keycodes
 *
 * fm.py polls hpprime.keyboard() whose bit numbers are PPL GETKEY codes, while a native app
 * receives Besta's raw scancode (prime_keydown = ptr[8] >> 16).  See the KEYMAP table below:
 * both numbering schemes are accepted at the same time, so the app cannot end up bound to
 * the wrong table.  Letter keys report the lowercase ASCII value of the letter in both
 * schemes, which is what the shortcuts and the text input use.
 * ============================================================================ */
/* Authoritative HP Prime G1 device keycodes, verified against the hardware scan matrix
 * (primetcc/rt/hp_input.h).  The PPL GETKEY numbering the Python original used is a
 * DIFFERENT table and mixing the two is the documented trap: the right arrow's device code
 * 0x04 equals GETKEY's esc, and the left arrow's 0x02 equals GETKEY's up.  So navigation
 * only ever matches the device codes below -- unambiguous by construction. */
#define DK_ESC 0x01
#define DK_LEFT 0x02
#define DK_UP 0x03
#define DK_RIGHT 0x04
#define DK_DOWN 0x05
#define DK_BACKSPACE 0x0C
#define DK_ENTER 0x0D
#define DK_SPACE 0x20
#define DK_ON 0x83
#define DK_F1 0x91 /* symb */
#define DK_F2 0xB2 /* plot */
#define DK_F3 0xB3 /* num  */
#define DK_F4 0xB4 /* view */
#define DK_F5 0xB5 /* cas  */
#define DK_F6 0x93 /* menu */
#define DK_HELP 0x95 /* help key (also 'h') */

/* Letter shortcuts match the ASCII code of the letter only.  Guessing the alpha-layer
 * device code is what made 'y' and 's' both trigger rename: the keypad reports the KEY
 * number (7/Q=0x51, 8=0x52, 9=0x53, ...), so several letters can land on codes that other
 * code paths also use.  Navigation and every critical action work from the device-keycode
 * table above, and confirmations accept enter/esc, so nothing depends on the letters. */
static inline bool k_is_letter(unsigned k, char lower) {
    return k == (unsigned)(unsigned char)lower || k == (unsigned)(unsigned char)(lower - 32);
}

static inline bool k_is_up(unsigned k) { return k == DK_UP; }
static inline bool k_is_down(unsigned k) { return k == DK_DOWN; }
static inline bool k_is_enter(unsigned k) { return k == DK_ENTER; }
static inline bool k_is_esc(unsigned k) { return k == DK_ESC; }
static inline bool k_is_left(unsigned k) { return k == DK_LEFT; }
static inline bool k_is_right(unsigned k) { return k == DK_RIGHT; }
static inline bool k_is_bs(unsigned k) { return k == DK_BACKSPACE; }


/* Letter/navigation aliases: same physical keys in both tables, reported as the lowercase
 * ASCII code of the letter (a=14 ... y=42, space=49). */
/* text input range */
#define KL_FIRST_PRINT 0x20
#define KL_LAST_PRINT 0x7E

/* Printable ASCII range accepted by the text input dialog. */
#define KL_FIRST_PRINT 0x20
#define KL_LAST_PRINT 0x7E

/* ============================================================================
 * 2. Geometry / colours
 * ============================================================================ */
#define SCREEN_W 320
#define SCREEN_H 240

/* UI font: GNU Unifont, embedded as a compact subset (build/font-uf/font_uf.h).
 *
 * 163 glyphs: ASCII 8x16 (Unifont ships a half-width ASCII face) plus the interface's
 * CJK/graphic glyphs at 16x16.  Total ink data is under 4 KB, so -- unlike the 900 KB blob
 * that was unstable on this device -- the tables are small static data in the image.
 * Text is drawn only with rgbSetColor + FillRect; no firmware font/text API is called.
 */
#ifdef FM_LANG_EN
#include "ui_en.h"
#else
#include "ui_zh.h"
#endif

#include "font_uf.h"
#include "font_gb.h" /* GB2312: 7445 glyphs + the byte pair -> Unicode map */

#define TEXT_CELL_H (UF_CELL_H + 1) /* 16 px glyph box + 1 px leading */
#define HALF_ADV 8                  /* 8x16 ASCII advance */
#define FULL_ADV 16                 /* 16x16 CJK/graphic advance */
#define CJK_ADV FULL_ADV
#define SIZE_COL_W (7 * HALF_ADV) /* widest human_size() output is "1023.9M" */

/* 24-bit RGB, exactly the Python original's palette. */
#define COL_BG 0xFFFFFF
#define COL_HEAD 0x1E3A8A
#define COL_HEAD_TX 0xFFFFFF
#define COL_SEL 0x3B82F6
#define COL_SEL_TX 0xFFFFFF
#define COL_TX 0x111111
#define COL_DIR 0x1D4ED8
#define COL_SZ 0x6B7280
#define COL_FOOTBG 0xE5E7EB
#define COL_DLGB 0x374151
#define COL_DLG 0xF3F4F6

#define VIEW_CHUNK 2048
/* Viewer line pitch: one 16 px glyph row plus 2 px so descenders and the next line never
 * touch (10 px was too tight and made the text look overlapped). */
#define VIEW_LINE_H 18

/* GB2312 (and the GBK subset that shares its layout) decoding: a lead byte 0xA1..0xF7
 * followed by a trail byte 0xA1..0xFE maps through gb2312_map.  Chinese source files on the
 * calculator are usually stored this way, not as UTF-8. */
static unsigned int gb2312_decode(const unsigned char *p, int *used) {
    unsigned char hi = p[0];
    if (hi < 0xA1 || hi > 0xF7 || !p[1]) {
        *used = 1;
        return 0xFFFD;
    }
    unsigned char lo = p[1];
    if (lo < 0xA1 || lo > 0xFE) {
        *used = 1;
        return 0xFFFD;
    }
    *used = 2;
    unsigned int cp = gb2312_map[(hi - 0xA1) * 94 + (lo - 0xA1)];
    return cp;
}

/* Is this byte string valid UTF-8?  If not, the viewer decodes it as GB2312. */
static bool is_utf8(const unsigned char *s, int n) {
    int i = 0;
    while (i < n) {
        unsigned char c = s[i];
        int extra;
        if (c < 0x80) {
            ++i;
            continue;
        } else if ((c & 0xE0) == 0xC0) {
            extra = 1;
        } else if ((c & 0xF0) == 0xE0) {
            extra = 2;
        } else if ((c & 0xF8) == 0xF0) {
            extra = 3;
        } else {
            return false;
        }
        if (i + extra >= n) return true; /* truncated tail: acceptable */
        for (int k = 1; k <= extra; ++k)
            if ((s[i + k] & 0xC0) != 0x80) return false;
        i += extra + 1;
    }
    return true;
}

/* ============================================================================
 * 2b. Boot trace / halt
 *
 * The header shows the startup step while the app starts, so if a syscall misbehaves on a
 * particular firmware the last visible number says where it stopped.  Build with
 * -UFM_BOOT_TRACE to drop the console trace and -UFM_KEY_DEBUG to drop the key readout.
 * ============================================================================ */
static volatile int g_step = 0;
static volatile unsigned g_last_key = 0;

/* A form feed first: printf without \f can crash after the calculator has been switched
 * off and on once (documented Besta OS quirk). */
#ifdef FM_BOOT_TRACE
#define BOOT_SAY(msg) Printf("\f" msg)
#else
#define BOOT_SAY(msg)
#endif

static void boot_step(int n) { g_step = n; }


/* ============================================================================
 * 3. Small helpers
 * ============================================================================ */
static int slen(const char *s) {
    int n = 0;
    if (!s) return 0;
    while (s[n]) ++n;
    return n;
}

static void scpy(char *dst, const char *src, int cap) {
    int i = 0;
    if (cap <= 0) return;
    if (src) {
        for (; src[i] && i < cap - 1; ++i) dst[i] = src[i];
    }
    dst[i] = 0;
}

static void scat(char *dst, const char *src, int cap) {
    int n = slen(dst);
    scpy(dst + n, src, cap - n);
}

static char upper(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

/* UTF-8 -> UTF-16LE conversion, in place into a caller supplied buffer.
 * Returns the number of UTF-16 code units written (excluding the terminator). */
static int utf16_of(const char *s, UTF16 *out, int cap_units) {
    int o = 0;
    if (!s) {
        if (cap_units > 0) out[0] = 0;
        return 0;
    }
    for (int i = 0; s[i] && o < cap_units - 1;) {
        unsigned char c = (unsigned char)s[i];
        unsigned int cp;
        int extra;
        if (c < 0x80) {
            cp = c;
            extra = 0;
        } else if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1F;
            extra = 1;
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0F;
            extra = 2;
        } else if ((c & 0xF8) == 0xF0) {
            cp = c & 0x07;
            extra = 3;
        } else {
            cp = '?';
            extra = 0;
        }
        i++;
        for (int k = 0; k < extra && s[i]; ++k, ++i)
            cp = (cp << 6) | ((unsigned char)s[i] & 0x3F);
        if (cp > 0xFFFF) cp = '?'; /* no surrogate pairs in this UI */
        out[o++] = (UTF16)cp;
    }
    out[o] = 0;
    return o;
}

/* Decode one UTF-8 sequence; *used receives the byte count consumed. */
static unsigned int utf8_next(const char *s, int *used) {
    unsigned char c = (unsigned char)s[0];
    unsigned int cp;
    int extra;
    if (c < 0x80) {
        *used = 1;
        return c;
    } else if ((c & 0xE0) == 0xC0) {
        cp = c & 0x1F;
        extra = 1;
    } else if ((c & 0xF0) == 0xE0) {
        cp = c & 0x0F;
        extra = 2;
    } else if ((c & 0xF8) == 0xF0) {
        cp = c & 0x07;
        extra = 3;
    } else {
        *used = 1;
        return '?';
    }
    for (int k = 0; k < extra; ++k) {
        if (!s[1 + k]) {
            *used = 1;
            return '?';
        }
        cp = (cp << 6) | ((unsigned char)s[1 + k] & 0x3F);
    }
    *used = extra + 1;
    return cp;
}

/* Human readable size, same formatting rules as the Python version. */
static void human_size(unsigned int n, char *out, int cap) {
    if (n < 1024) {
        char b[24];
        int i = 0;
        unsigned int v = n;
        b[i++] = 'B';
        if (!v) b[i++] = '0';
        while (v) {
            b[i++] = (char)('0' + v % 10);
            v /= 10;
        }
        b[i] = 0;
        for (int j = 0; j < i / 2; ++j) {
            char t = b[j];
            b[j] = b[i - 1 - j];
            b[i - 1 - j] = t;
        }
        scpy(out, b, cap);
        return;
    }
    char num[16];
    unsigned int whole, frac;
    char unit;
    if (n < 1024u * 1024u) {
        whole = n / 1024u;
        frac = ((n % 1024u) * 10u) / 1024u;
        unit = 'K';
    } else {
        whole = n / (1024u * 1024u);
        frac = ((n % (1024u * 1024u)) * 10u) / (1024u * 1024u);
        unit = 'M';
    }
    int i = 0;
    char tmp[12];
    int t = 0;
    if (!whole) tmp[t++] = '0';
    while (whole) {
        tmp[t++] = (char)('0' + whole % 10);
        whole /= 10;
    }
    while (t) num[i++] = tmp[--t];
    num[i++] = '.';
    num[i++] = (char)('0' + frac);
    num[i++] = unit;
    num[i] = 0;
    scpy(out, num, cap);
}

static void u2hex(unsigned int v, int digits, char *out) {
    const char *h = "0123456789ABCDEF";
    for (int i = digits - 1; i >= 0; --i) {
        out[i] = h[v & 0xF];
        v >>= 4;
    }
}

static void u2dec(unsigned int v, char *out) {
    char tmp[12];
    int t = 0, i = 0;
    if (!v) tmp[t++] = '0';
    while (v) {
        tmp[t++] = (char)('0' + v % 10);
        v /= 10;
    }
    while (t) out[i++] = tmp[--t];
    out[i] = 0;
}

/* Log one filesystem syscall result (op, return value, path). */
static void log_fs(const char *op, int r, const char *path) {
#ifdef FM_KEY_DEBUG
    char m[80];
    scpy(m, "  fs ", sizeof(m));
    scat(m, op, sizeof(m));
    scat(m, " r=", sizeof(m));
    char num[16];
    u2dec((unsigned)(r < 0 ? (unsigned)(-r) : (unsigned)r), num);
    if (r < 0) scat(m, "-", sizeof(m));
    scat(m, num, sizeof(m));
    scat(m, " ", sizeof(m));
    scat(m, path, sizeof(m));
    scat(m, "\n", sizeof(m));
    LOG(m);
#else
    (void)op;
    (void)r;
    (void)path;
#endif
}


/* ============================================================================
 * 4. Filesystem layer (port of HPFileSystem in fm.py)
 * ============================================================================ */
#define MAX_NAME 128
#define MAX_PATH 200
#define MAX_ENTRIES 192

struct Entry {
    char name[MAX_NAME];
    unsigned int size;
    unsigned int mtime;
    unsigned char attr;
    bool is_dir;
};

class Fs {
public:
    Entry items[MAX_ENTRIES];
    int count;
    char cwd[MAX_PATH];

    Fs() {
        count = 0;
        cwd[0] = 0;
    }

    /* ---- path helpers ---- */
    static void join(const char *dir, const char *name, char *out, int cap) {
        int n = slen(dir);
        if (n && (dir[n - 1] == '\\' || dir[n - 1] == '/')) {
            scpy(out, dir, cap);
        } else {
            scpy(out, dir, cap);
            scat(out, "\\", cap);
        }
        scat(out, name, cap);
    }

    void path_of(const char *name, char *out, int cap) const { join(cwd, name, out, cap); }

    /* Resolve ".." / "." inside an absolute path, keeping a trailing pattern if any. */
    static void normalize(const char *in, char *out, int cap) {
        char tmp[MAX_PATH];
        int ti = 0;
        tmp[0] = 0;
        /* keep the drive part verbatim */
        int i = 0;
        if (in[1] == ':') {
            tmp[ti++] = in[0];
            tmp[ti++] = ':';
            i = 2;
            if (in[2] == '\\' || in[2] == '/') {
                tmp[ti++] = '\\';
                i = 3;
            }
        } else if (in[0] == '\\') {
            tmp[ti++] = '\\';
            i = 1;
        }
        /* The root separator must exist before the first segment is appended, otherwise
         * "C:" + "SUBDIR" comes out as "C:SUBDIR" and every _wchdir() fails: this was why
         * the arrow keys could not enter or leave a directory. */
        if (ti && tmp[ti - 1] != '\\') {
            if (!(ti == 2 && tmp[1] == ':')) tmp[ti++] = '\\';
            else tmp[ti++] = '\\';
        }
        for (; in[i] && ti < MAX_PATH - 2;) {
            while (in[i] == '\\' || in[i] == '/') ++i;
            if (!in[i]) break;
            char seg[64];
            int si = 0;
            while (in[i] && in[i] != '\\' && in[i] != '/' && si < 63) seg[si++] = in[i++];
            seg[si] = 0;
            if (!strcmp(seg, ".")) continue;
            if (!strcmp(seg, "..")) {
                /* pop one segment */
                int j = ti;
                while (j > 0 && tmp[j - 1] != '\\') --j;
                if (j > 0) ti = j - 1;
                else ti = 0;
                if (ti == 0) {
                    tmp[ti++] = '\\';
                }
                continue;
            }
            if (ti && tmp[ti - 1] != '\\') tmp[ti++] = '\\';
            for (int k = 0; seg[k] && ti < MAX_PATH - 2; ++k) tmp[ti++] = seg[k];
        }
        tmp[ti] = 0;
        if (!ti) {
            scpy(out, "\\", cap);
            return;
        }
        /* "C:" alone is not a usable path: keep the root separator */
        if (ti == 2 && tmp[1] == ':') {
            tmp[ti++] = '\\';
            tmp[ti] = 0;
        }
        scpy(out, tmp, cap);
    }

    /* UTF-16 scratch used for the syscalls; single-threaded UI so one buffer is enough. */
    UTF16 *uw(const char *s) {
        utf16_of(s, utf16_buf, 300);
        return utf16_buf;
    }

    /* Pick a usable starting directory by probing candidate roots with the directory
     * listing syscall.  The firmware's own cwd (see _wgetcurdir) is deliberately not used:
     * it never followed our _wchdir() and it corrupted the caller's buffer. */
    void get_cwd() {
        static const char *candidates[] = { "C:\\", "A:\\", "D:\\" };
        for (int i = 0; i < 3; ++i) {
            char probe[MAX_PATH];
            join(candidates[i], "*.*", probe, MAX_PATH);
            if (probe_ok(probe)) {
                scpy(cwd, candidates[i], MAX_PATH);
                return;
            }
        }
        scpy(cwd, "C:\\", MAX_PATH);
    }

    /* Does a find pattern match at least one entry?  Uses the 8.3 (ASCII) syscall, which
     * is the variant used by the SDK's own KhiCAS file browser. */
    bool probe_ok(const char *ascii_pattern) {
        find_context_t ctx;
        if (_afindfirst((char *)ascii_pattern, &ctx, 0) != 0) return false;
        _findclose(&ctx);
        return true;
    }

    bool chdir(const char *target) {
        char abs[MAX_PATH];
        resolve(target, abs, MAX_PATH);
        short r = _wchdir((UTF16 *)uw(abs));
#ifdef FM_KEY_DEBUG
        {
            char m[64];
            scpy(m, "  chdir call r=", sizeof(m));
            char num[16];
            u2hex((unsigned)r, 2, num);
            scat(m, num, sizeof(m));
            scat(m, " -> ", sizeof(m));
            scat(m, abs, sizeof(m));
            scat(m, "\n", sizeof(m));
            LOG(m);
        }
#endif
        if (r == 0) {
            scpy(cwd, abs, MAX_PATH);
            return true;
        }
        LOG("  chdir: wide failed, trying 8.3\n");
        /* fall back to the DOS 8.3 form (upper case, no long names) */
        char dos[MAX_PATH];
        int o = 0;
        for (int i = 0; abs[i] && o < MAX_PATH - 1; ++i)
            dos[o++] = (abs[i] >= 'a' && abs[i] <= 'z') ? (char)(abs[i] - 32) : abs[i];
        dos[o] = 0;
        if (_achdir(dos) == 0) {
            scpy(cwd, abs, MAX_PATH);
            return true;
        }
        return false;
    }

    /* Resolve a user supplied path relative to cwd. */
    void resolve(const char *target, char *out, int cap) const {
        char tmp[MAX_PATH];
        if (target[1] == ':') {
            scpy(tmp, target, MAX_PATH);
        } else if (target[0] == '\\' || target[0] == '/') {
            char drive[4] = "C:";
            if (cwd[1] == ':') {
                drive[0] = cwd[0];
            }
            scpy(tmp, drive, MAX_PATH);
            scat(tmp, target, MAX_PATH);
        } else {
            join(cwd, target, tmp, MAX_PATH);
        }
        normalize(tmp, out, cap);
    }

    /* ---- directory listing ---- */
    void clear() { count = 0; }

    int find_entry(const char *name) const {
        for (int i = 0; i < count; ++i)
            if (!strcmp(items[i].name, name)) return i;
        return -1;
    }

    void add_entry(const char *name, unsigned int size, unsigned int mtime, unsigned char attr) {
        if (!name[0]) return;
        if (!strcmp(name, ".") || !strcmp(name, "..")) return;
        if (find_entry(name) >= 0) return;
        if (count >= MAX_ENTRIES) return;
        Entry &e = items[count++];
        scpy(e.name, name, MAX_NAME);
        e.size = size;
        e.mtime = mtime;
        e.attr = attr;
        e.is_dir = (attr & 0x10) != 0;
    }

    void sort_entries() {
        /* directories first, then case-insensitive by name (same key as the Python version) */
        for (int i = 1; i < count; ++i) {
            Entry key = items[i];
            int j = i - 1;
            while (j >= 0) {
                const Entry &a = items[j];
                bool a_before = (a.is_dir == key.is_dir)
                                    ? (strcasecmp(a.name, key.name) <= 0)
                                    : (a.is_dir && !key.is_dir);
                if (a_before) break;
                items[j + 1] = items[j];
                --j;
            }
            items[j + 1] = key;
        }
    }

    static int strcasecmp(const char *a, const char *b) {
        for (;;) {
            char ca = lower(*a), cb = lower(*b);
            if (ca != cb) return (int)(unsigned char)ca - (int)(unsigned char)cb;
            if (!ca) return 0;
            ++a;
            ++b;
        }
    }

    /* One _wfindfirst/_wfindnext pass with an attribute mask. */
    /* Copy the entry name out of the find context.
     *
     * Both the ASCII and the LFN find syscalls fill the same context.  The LFN field is
     * UTF-16 (the Python version preferred it, so extended characters survive), while the
     * 8.3 field is a plain char string.  Which one carries the name depends on the
     * firmware, so the encoding is detected instead of assumed: a UTF-16 string has a NUL
     * high byte after every character. */
    void entry_name(const find_context_t *ctx, char *out, int cap) {
        out[0] = 0;
        if (!ctx->filename_lfn && !ctx->filename) return;

        if (ctx->filename_lfn) {
            const unsigned char *raw = (const unsigned char *)ctx->filename_lfn;
            int wide = 1;
            for (int i = 0; i < 32; ++i) {
                if (raw[i * 2 + 0] == 0) break; /* end of a UTF-16 string */
                if (raw[i * 2 + 1] != 0) {
                    wide = 0; /* high bytes set -> it is a plain char string */
                    break;
                }
            }
            if (wide) {
                int o = 0;
                for (int i = 0; ctx->filename_lfn[i] && o < cap - 1; ++i) {
                    unsigned int c = ctx->filename_lfn[i];
                    out[o++] = (c < 0x80) ? (char)c : '?';
                }
                out[o] = 0;
                if (o) return;
            }
        }
        if (ctx->filename) scpy(out, ctx->filename, cap);
    }

    /* One pass with the ASCII find syscall, attribute mask 0 (match everything). */
    int scan_ascii(const char *pattern) {
        find_context_t ctx;
        char pat[MAX_PATH + 8];
        scpy(pat, pattern, sizeof(pat));
        if (_afindfirst(pat, &ctx, 0) != 0) return 0;
        int added = 0;
        for (;;) {
            char name[MAX_NAME];
            entry_name(&ctx, name, MAX_NAME);
            int before = count;
            if (name[0]) {
                add_entry(name, (unsigned int)ctx.size, ctx.mtime, ctx.attrib);
#ifdef FM_KEY_DEBUG
                {
                    char m[80];
                    scpy(m, "  entry ", sizeof(m));
                    char num[16];
                    u2hex((unsigned)ctx.mtime, 8, num);
                    scat(m, num, sizeof(m));
                    scat(m, " sz=", sizeof(m));
                    u2dec((unsigned)ctx.size, num);
                    scat(m, num, sizeof(m));
                    scat(m, " a=", sizeof(m));
                    u2hex(ctx.attrib, 2, num);
                    scat(m, num, sizeof(m));
                    scat(m, " ", sizeof(m));
                    scat(m, name, sizeof(m));
                    scat(m, "\n", sizeof(m));
                    LOG(m);
                }
#endif
            }
            if (count != before) ++added;
            if (_afindnext(&ctx) != 0) break;
        }
        _findclose(&ctx);
        return added;
    }

    /* Same pass through the LFN (wide) find syscall.  Only used when the ASCII variant
     * reports nothing, so a firmware quirk in one of them cannot hide the directory. */
    int scan_wide(const char *pattern) {
        find_context_t ctx;
        UTF16 pat[MAX_PATH + 8];
        utf16_of(pattern, pat, MAX_PATH + 8);
        if (_wfindfirst(pat, &ctx, 0) != 0) return 0;
        int added = 0;
        for (;;) {
            char name[MAX_NAME];
            entry_name(&ctx, name, MAX_NAME);
            int before = count;
            if (name[0]) {
                add_entry(name, (unsigned int)ctx.size, ctx.mtime, ctx.attrib);
#ifdef FM_KEY_DEBUG
                {
                    char m[80];
                    scpy(m, "  entry ", sizeof(m));
                    char num[16];
                    u2hex((unsigned)ctx.mtime, 8, num);
                    scat(m, num, sizeof(m));
                    scat(m, " sz=", sizeof(m));
                    u2dec((unsigned)ctx.size, num);
                    scat(m, num, sizeof(m));
                    scat(m, " a=", sizeof(m));
                    u2hex(ctx.attrib, 2, num);
                    scat(m, num, sizeof(m));
                    scat(m, " ", sizeof(m));
                    scat(m, name, sizeof(m));
                    scat(m, "\n", sizeof(m));
                    LOG(m);
                }
#endif
            }
            if (count != before) ++added;
            if (_wfindnext(&ctx) != 0) break;
        }
        _findclose(&ctx);
        return added;
    }

    /* Directory listing.  The Python version merged three attribute masks because a single
     * mask could miss entries; mask 0 ("everything") is what the SDK's own file browser
     * uses, so it is tried first here and the wide variants only act as a fallback. */
    void list_dir(const char *path_opt) {
        clear();
        char pattern[MAX_PATH];
        if (path_opt && path_opt[0]) {
            scpy(pattern, path_opt, MAX_PATH);
        } else {
            scpy(pattern, cwd, MAX_PATH);
        }
        int n = slen(pattern);
        if (n && (pattern[n - 1] == '\\' || pattern[n - 1] == '/')) {
            scat(pattern, "*.*", MAX_PATH);
        } else if (n && pattern[n - 1] != '*') {
            scat(pattern, "\\*.*", MAX_PATH);
        }
        scan_ascii(pattern);
#ifdef FM_LIST_EXTRA_MASKS
        /* Belt and braces for firmwares that filter on the attribute mask. */
        if (!count) {
            static const int masks[3] = { 0x37, 0xFF, 0x00 };
            for (int i = 0; i < 3 && !count; ++i) {
                find_context_t ctx;
                UTF16 pat[MAX_PATH + 8];
                utf16_of(pattern, pat, MAX_PATH + 8);
                if (_wfindfirst(pat, &ctx, masks[i]) != 0) continue;
                for (;;) {
                    char name[MAX_NAME];
                    entry_name(&ctx, name, MAX_NAME);
                    if (name[0]) {
                add_entry(name, (unsigned int)ctx.size, ctx.mtime, ctx.attrib);
#ifdef FM_KEY_DEBUG
                {
                    char m[80];
                    scpy(m, "  entry ", sizeof(m));
                    char num[16];
                    u2hex((unsigned)ctx.mtime, 8, num);
                    scat(m, num, sizeof(m));
                    scat(m, " sz=", sizeof(m));
                    u2dec((unsigned)ctx.size, num);
                    scat(m, num, sizeof(m));
                    scat(m, " a=", sizeof(m));
                    u2hex(ctx.attrib, 2, num);
                    scat(m, num, sizeof(m));
                    scat(m, " ", sizeof(m));
                    scat(m, name, sizeof(m));
                    scat(m, "\n", sizeof(m));
                    LOG(m);
                }
#endif
            }
                    if (_wfindnext(&ctx) != 0) break;
                }
                _findclose(&ctx);
            }
        }
#endif
        if (!count) scan_wide(pattern);
        sort_entries();
    }

    int attr_of(const char *full) { return (int)(unsigned short)_wfgetattr((UTF16 *)uw(full)); }

    bool is_dir(const char *full) {
        int a = attr_of(full);
        if (a == 0 || a == -1 || a == 0xFFFF) return false;
        return (a & 0x10) != 0;
    }

    /* DOS 8.3 rendering of a path (upper case), used as a fallback for the wide syscalls:
     * several of the _w* calls only accept names the FAT layer can shorten. */
    static void to_dos(const char *in, char *out, int cap) {
        int o = 0;
        for (int i = 0; in[i] && o < cap - 1; ++i) {
            char c = in[i];
            out[o++] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
        }
        out[o] = 0;
    }

    bool mkdir(const char *full) {
        int r = _wmkdir((UTF16 *)uw(full));
        log_fs("mkdir-w", r, full);
        if (r == 0) return true;
        char dos[MAX_PATH];
        to_dos(full, dos, MAX_PATH);
        r = _amkdir(dos);
        log_fs("mkdir-83", r, dos);
        return r == 0;
    }

    bool remove_file(const char *full) {
        /* the SDK documents 0 == ok for the wide filesystem calls, so accept both 0 and a
         * non-zero "true" here and log the value so the correct one is visible in fm.log */
        bool r = __wremove((UTF16 *)uw(full));
        log_fs("rmfile-w", r ? 0 : -1, full);
        if (r) return true;
        char dos[MAX_PATH];
        to_dos(full, dos, MAX_PATH);
        bool r2 = _aremove(dos);
        log_fs("rmfile-83", r2 ? 0 : -1, dos);
        if (r2) return true;
        char withslash[MAX_PATH];
        scpy(withslash, full, MAX_PATH);
        bool r3 = __wremove((UTF16 *)uw(withslash));
        log_fs("rmfile-ws", r3 ? 0 : -1, withslash);
        return r3;
    }

    /* Removing a directory is the one filesystem call that behaves differently across
     * firmware builds, so every variant is tried and logged: the wide call, the same call
     * with a trailing backslash, the DOS 8.3 call, and finally _aremove (which the Python
     * FileManager effectively used for directories too). */
    bool remove_dir(const char *full) {
        int r = _wrmdir((UTF16 *)uw(full));
        log_fs("rmdir-w", r, full);
        if (r == 0) return true;

        char withslash[MAX_PATH];
        scpy(withslash, full, MAX_PATH);
        int n = slen(withslash);
        if (n + 1 < MAX_PATH && n && withslash[n - 1] != '\\') {
            withslash[n] = '\\';
            withslash[n + 1] = 0;
        }
        r = _wrmdir((UTF16 *)uw(withslash));
        log_fs("rmdir-ws", r, withslash);
        if (r == 0) return true;

        char dos[MAX_PATH];
        to_dos(full, dos, MAX_PATH);
        r = _armdir(dos);
        log_fs("rmdir-83", r, dos);
        if (r == 0) return true;

        bool rb = _aremove(dos);
        log_fs("rmdir-83rm", rb ? 0 : -1, dos);
        return rb;
    }

    bool rename(const char *a, const char *b) {
        UTF16 ua[300], ub[300];
        utf16_of(a, ua, 300);
        utf16_of(b, ub, 300);
        int r = _wrename(ua, ub);
        log_fs("rename-w", r, b);
        if (r == 0) return true;
        char da[MAX_PATH], db[MAX_PATH];
        to_dos(a, da, MAX_PATH);
        to_dos(b, db, MAX_PATH);
        int r2 = _arename(da, db);
        log_fs("rename-83", r2, db);
        return r2 == 0;
    }

    /* Manual copy through the (verified) fopen/read/write calls: some firmware builds
     * refuse _wfcopy for an existing destination or for names it cannot shorten. */
    bool copy_manual(const char *a, const char *b) {
        static unsigned char buf[2048];
        UTF16 um[8];
        utf16_of("rb", um, 8);
        file_descriptor_t *in = __wfopen((UTF16 *)uw(a), um);
        if (!in) {
            log_fs("copy-in-open", -1, a);
            return false;
        }
        UTF16 umw[8];
        utf16_of("wb", umw, 8);
        file_descriptor_t *out = __wfopen((UTF16 *)uw(b), umw);
        if (!out) {
            _fclose(in);
            log_fs("copy-out-open", -1, b);
            return false;
        }
        size_t total = 0;
        for (;;) {
            size_t got = _fread(buf, 1, sizeof(buf), in);
            if (got == 0) break;
            size_t put = _fwrite(buf, 1, got, out);
            if (put != got) {
                log_fs("copy-write", -1, b);
                _fclose(in);
                _fclose(out);
                return false;
            }
            total += got;
        }
        _fclose(in);
        _fclose(out);
        log_fs("copy-manual ok bytes", (int)total, b);
        return true;
    }

    bool copy_file(const char *a, const char *b) {
        UTF16 ua[300], ub[300];
        utf16_of(a, ua, 300);
        utf16_of(b, ub, 300);
        int r = _wfcopy(ua, ub);
        log_fs("wfcopy", r, b);
        if (r == 0) return true;
        log_fs("wfcopy failed, manual", r, a);
        return copy_manual(a, b);
    }

    /* Snapshot buffers for the recursive helpers.  Only the NAME of each entry is needed,
     * and only for the current level, so two sets of 8 levels cost 2*8*192*128 = 384 KB of
     * BSS -- a full Entry array per level would have been 2.2 MB, which is why the depth
     * used to be limited to two levels and deleting a folder with subfolders failed. */
    static char scratch_names[2][8][MAX_ENTRIES][MAX_NAME];
    static unsigned char scratch_flags[2][8][MAX_ENTRIES];

    /* recursive delete (Python: delete/delete_dir) */
    bool remove_recursive(const char *full, int depth) {
        if (depth >= 8) return false; /* scratch is eight levels deep */
        if (!is_dir(full)) return remove_file(full);
        /* snapshot the listing: the recursion below re-uses the shared entry array */
        char (*names)[MAX_NAME] = scratch_names[depth & 1][depth & 7];
        unsigned char *flags = scratch_flags[depth & 1][depth & 7];
        int saved = count;
        list_dir(full);
        int n = count > MAX_ENTRIES ? MAX_ENTRIES : count;
        for (int i = 0; i < n; ++i) {
            scpy(names[i], items[i].name, MAX_NAME);
            flags[i] = items[i].is_dir ? 1 : 0;
        }
        count = saved; /* nothing to restore: `items` is only appended to by list_dir */
        for (int i = 0; i < n; ++i) {
            char child[MAX_PATH];
            join(full, names[i], child, MAX_PATH);
            if (flags[i]) {
                if (!remove_recursive(child, depth + 1)) return false;
            } else if (!remove_file(child)) {
                return false;
            }
        }
        return remove_dir(full);
    }

    /* recursive copy (Python: copy/copy_dir -- W_COPY does not recurse) */
    bool copy_recursive(const char *src, const char *dst, int depth) {
        if (depth >= 8) return false; /* scratch is eight levels deep */
        if (!is_dir(src)) return copy_file(src, dst);
        if (!mkdir(dst) && !is_dir(dst)) return false;
        char (*names)[MAX_NAME] = scratch_names[depth & 1][depth & 7];
        unsigned char *flags = scratch_flags[depth & 1][depth & 7];
        int saved = count;
        list_dir(src);
        int n = count > MAX_ENTRIES ? MAX_ENTRIES : count;
        for (int i = 0; i < n; ++i) {
            scpy(names[i], items[i].name, MAX_NAME);
            flags[i] = items[i].is_dir ? 1 : 0;
        }
        count = saved;
        for (int i = 0; i < n; ++i) {
            char sp[MAX_PATH], dp[MAX_PATH];
            join(src, names[i], sp, MAX_PATH);
            join(dst, names[i], dp, MAX_PATH);
            if (flags[i]) {
                if (!copy_recursive(sp, dp, depth + 1)) return false;
            } else if (!copy_file(sp, dp)) {
                return false;
            }
        }
        return true;
    }

    /* read a chunk of a file (Python: read_file_chunk) */
    int read_chunk(const char *full, unsigned int offset, unsigned int size, unsigned char *buf) {
        UTF16 um[8];
        utf16_of("rb", um, 8);
        file_descriptor_t *f = __wfopen((UTF16 *)uw(full), um);
        if (!f) return 0;
        int got = 0;
        if (__fseek(f, (long)offset, 0) == 0) {
            size_t r = _fread(buf, 1, size, f);
            got = (int)r;
        } else {
            /* some firmwares do not support fseek on this handle: re-open and skip */
            unsigned int left = offset;
            while (left) {
                unsigned int step = left > 512 ? 512 : left;
                size_t r = _fread(buf, 1, step, f);
                if (r != step) {
                    _fclose(f);
                    return 0;
                }
                left -= step;
            }
            size_t r = _fread(buf, 1, size, f);
            got = (int)r;
        }
        _fclose(f);
        return got;
    }

private:
    UTF16 utf16_buf[300];
};

/* Definitions for Fs's static recursion scratch. */
char Fs::scratch_names[2][8][MAX_ENTRIES][MAX_NAME];
unsigned char Fs::scratch_flags[2][8][MAX_ENTRIES];

/* Key auto repeat timing in ms (a plain "up/down" tap still moves exactly one row). */
#define FM_REPEAT_DELAY 400
#define FM_REPEAT_PERIOD 90

/* ============================================================================
 * 5. Input: event thread + polling facade (KeyReader / mouse in fm.py)
 * ============================================================================ */
struct Input {
    volatile unsigned keycode;
    volatile int tap_x, tap_y;
    volatile bool tap_pending;

    unsigned prev_key;
    int held;
    /* auto repeat state (see wait_key) */
    unsigned repeat_key;
    int repeat_held_ms;
    int repeat_next_ms;

    Input() {
        keycode = 0;
        tap_x = tap_y = -1;
        tap_pending = false;
        prev_key = 0;
        held = 0;
        repeat_key = 0;
        repeat_held_ms = 0;
        repeat_next_ms = 0;
    }

    thread_t *thread;

    static int entry(void *self) {
        Input *in = (Input *)self;
        ClearAllEvents();
        ui_event_t e;
        for (;;) {
            if (GetEvent(&e)) {
                unsigned *ptr = (unsigned *)&e;
                unsigned type = ptr[7];
#ifdef FM_KEY_DEBUG
                {
                    char m[40];
                    scpy(m, "ev type=", sizeof(m));
                    char hx[8];
                    u2hex(type, 6, hx);
                    scat(m, hx, sizeof(m));
                    if (type == 0x10) {
                        scat(m, " key=0x", sizeof(m));
                        u2hex(ptr[8] >> 16, 2, hx);
                        scat(m, hx, sizeof(m));
                    }
                    scat(m, "\n", sizeof(m));
                    LOG(m);
                }
#endif
                if (type == 0x10) {
                    in->keycode = ptr[8] >> 16;
                } else if (type == 0x100000) {
                    in->keycode = 0;
                } else if (type == 0x1) {
                    in->tap_x = (int)(ptr[8] >> 16);
                    in->tap_y = (int)ptr[9];
                    in->tap_pending = true;
                } else if (type == 0x8) {
                    unsigned x = ptr[8] >> 16;
                    unsigned y = ptr[9];
                    if (x < SCREEN_W && y < SCREEN_H) {
                        in->tap_x = (int)x;
                        in->tap_y = (int)y;
                        in->tap_pending = true;
                    }
                }
            }
        }
        return 0;
    }

    bool threaded;

    /* Start the event thread.  If the firmware refuses to create it the main loop polls
     * GetEvent() itself (see Browser::pump), so a failed thread is not fatal. */
    void start() {
        thread = 0;
        threaded = false;
        prev_key = 0;
        OSSleep(10);
        thread = OSCreateThread(entry, this, 0, 0);
        threaded = (thread != 0);
        LOG(threaded ? "event thread created\n" : "event thread failed, polling\n");
    }

    /* Poll directly (only used when the event thread could not be created). */
    void poll_events() {
        static ui_event_t e;
        if (threaded) return;
        ClearAllEvents();
        if (!GetEvent(&e)) return;
        unsigned *ptr = (unsigned *)&e;
        unsigned type = ptr[7];
        if (type == 0x10 || type == 0x100000)
            keycode = (type == 0x10) ? (ptr[8] >> 16) : 0;
        else if (type == 0x1 || type == 0x8) {
            unsigned x = ptr[8] >> 16, y = ptr[9];
            if (x < SCREEN_W && y < SCREEN_H) {
                tap_x = (int)x;
                tap_y = (int)y;
                tap_pending = true;
            }
        }
    }

    /* Wait for the next key transition (or timeout).  Returns the keycode or 0. */
    /* Non blocking poll: returns a key when it is newly pressed, or again while it is
     * held (auto repeat after FM_REPEAT_DELAY, then every FM_REPEAT_PERIOD), so holding
     * up/down scrolls continuously instead of moving one row. */
    unsigned wait_key(int timeout_ms) {
        for (int t = 0; t < timeout_ms; t += 8) {
            poll_events();
            unsigned k = keycode;
            if (k) {
                if (k != prev_key) {
                    /* fresh press: act immediately, then wait out the repeat delay */
                    prev_key = k;
                    repeat_key = k;
                    repeat_held_ms = 0;
                    repeat_next_ms = FM_REPEAT_DELAY;
                    return k;
                }
                repeat_held_ms += 8;
                if (repeat_held_ms >= repeat_next_ms) {
                    repeat_next_ms += FM_REPEAT_PERIOD;
                    return k;
                }
            } else {
                prev_key = 0;
                repeat_key = 0;
                repeat_held_ms = 0;
                repeat_next_ms = 0;
            }
            if (tap_pending) return 0;
            OSSleep(8);
        }
        return 0;
    }

    /* Wait until every key is released (the firmware only reports transitions, so a
     * modal dialog must not re-trigger on the key that opened it). */
    void wait_release() {
        for (int i = 0; i < 250 && keycode; ++i) OSSleep(4);
        prev_key = 0;
        repeat_key = 0;
        repeat_held_ms = 0;
        repeat_next_ms = 0;
    }

    bool take_tap(int *x, int *y) {
        if (!tap_pending) return false;
        *x = tap_x;
        *y = tap_y;
        tap_pending = false;
        return true;
    }
};

/* ============================================================================
 * 6. Text rendering: embedded bitmaps, no firmware font calls
 * ============================================================================ */
static void fill_rect(int x, int y, int w, int h, int color) {
    if (w <= 0 || h <= 0) return;
    int x1 = x + w - 1, y1 = y + h - 1;
    if (x >= SCREEN_W || y >= SCREEN_H || x1 < 0 || y1 < 0) return; /* fully off panel */
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 >= SCREEN_W) x1 = SCREEN_W - 1;
    if (y1 >= SCREEN_H) y1 = SCREEN_H - 1;
    rgbSetColor(color);
    FillRect((short)x, (short)y, (short)x1, (short)y1, 0);
}

/* Glyph lookup in the GB2312 table (7445 sorted entries). */
static const gb_glyph_t *gb_glyph_of(unsigned int cp) {
    int lo = 0, hi = GB_HAN_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        unsigned int t = gb2312_han[mid].cp;
        if (cp < t) {
            hi = mid - 1;
        } else if (cp > t) {
            lo = mid + 1;
        } else {
            return &gb2312_han[mid];
        }
    }
    return 0;
}

/* Glyph lookup: binary search over the sorted Unifont subset. */
static const uf_glyph_t *glyph_of(unsigned int cp) {
    int lo = 0, hi = UF_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        unsigned int t = uf_table[mid].cp;
        if (cp < t) {
            hi = mid - 1;
        } else if (cp > t) {
            lo = mid + 1;
        } else {
            return &uf_table[mid];
        }
    }
    return 0;
}

/* Draw one Unifont glyph (rows are MSB first, `w` is 8 or 16 px). */
static void draw_glyph_at(int x, int top, const uf_glyph_t *g, int color) {
    if (!g) return;
    rgbSetColor(color);
    for (int r = 0; r < g->n; ++r) {
        unsigned int bits = g->rows[r];
        int y = top + g->off + r;
        if (!bits || y < 0 || y >= SCREEN_H) continue;
        int c = 0;
        while (c < g->w) {
            if (!((bits >> (15 - c)) & 1)) {
                ++c;
                continue;
            }
            int c2 = c;
            while (c2 + 1 < g->w && ((bits >> (15 - (c2 + 1))) & 1)) ++c2;
            int x0 = x + c, x1 = x + c2;
            if (x1 >= 0 && x0 < SCREEN_W) {
                if (x0 < 0) x0 = 0;
                if (x1 >= SCREEN_W) x1 = SCREEN_W - 1;
                FillRect((short)x0, (short)y, (short)x1, (short)y, 0);
            }
            c = c2 + 1;
        }
    }
}

/* Placeholder for codepoints outside the subset. */
static void draw_missing(int x, int top, int adv, int color) {
    fill_rect(x + 2, top + 3, adv - 4, 1, color);
    fill_rect(x + 2, top + 13, adv - 4, 1, color);
    fill_rect(x + 2, top + 3, 1, 11, color);
    fill_rect(x + adv - 3, top + 3, 1, 11, color);
}

/* Advance: 8 px for the half-width ASCII face, 16 px for wide glyphs. */
static int char_adv(const char *s, int *used) {
    unsigned int cp = utf8_next(s, used);
    const uf_glyph_t *g = glyph_of(cp);
    if (g) return g->w;
    if (gb_glyph_of(cp)) return FULL_ADV;
    return HALF_ADV;
}

static int text_width(const char *s) {
    int w = 0, used;
    for (int i = 0; s[i];) {
        w += char_adv(s + i, &used);
        i += used;
    }
    return w;
}

/* Draw one line of UTF-8 text; `top` is the top of the 17 px cell. */
static int draw_text(int x, int top, const char *s, int color) {
    int cx = x;
    int used;
    for (int i = 0; s[i];) {
        unsigned int cp = utf8_next(s + i, &used);
        i += used;
        if (cp == '\n' || cp == '\r') continue;
        const uf_glyph_t *g = glyph_of(cp);
        if (g) {
            draw_glyph_at(cx, top, g, color);
            cx += g->w;
        } else {
            const gb_glyph_t *gb = gb_glyph_of(cp);
            if (gb) {
                rgbSetColor(color);
                for (int r = 0; r < gb->n; ++r) {
                    unsigned int bits = gb->rows[r];
                    int y = top + gb->off + r;
                    if (!bits || y < 0 || y >= SCREEN_H) continue;
                    int c = 0;
                    while (c < 16) {
                        if (!((bits >> (15 - c)) & 1)) {
                            ++c;
                            continue;
                        }
                        int c2 = c;
                        while (c2 + 1 < 16 && ((bits >> (15 - (c2 + 1))) & 1)) ++c2;
                        int x0 = cx + c, x1 = cx + c2;
                        if (x1 >= 0 && x0 < SCREEN_W) {
                            if (x0 < 0) x0 = 0;
                            if (x1 >= SCREEN_W) x1 = SCREEN_W - 1;
                            FillRect((short)x0, (short)y, (short)x1, (short)y, 0);
                        }
                        c = c2 + 1;
                    }
                }
                cx += FULL_ADV;
            } else {
                draw_missing(cx, top, HALF_ADV, color);
                cx += HALF_ADV;
            }
        }
    }
    return cx - x;
}

/* Truncate `s` to max_w pixels, appending `suffix` (port of fit_text). */
static void fit_text(const char *s, int max_w, const char *suffix, char *out, int cap) {
    out[0] = 0;
    if (max_w <= 0) return;
    if (text_width(s) <= max_w) {
        scpy(out, s, cap);
        return;
    }
    int budget = max_w - text_width(suffix);
    if (budget < HALF_ADV) {
        scpy(out, suffix, cap);
        return;
    }
    int w = 0, i = 0, best = -1;
    while (s[i]) {
        int used;
        int adv = char_adv(s + i, &used);
        if (w + adv > budget) break;
        w += adv;
        i += used;
        if (s[i]) best = i; /* never end on a complete string (it already fits) */
    }
    if (best <= 0) {
        scpy(out, suffix, cap);
        return;
    }
    int m = best < cap - 4 ? best : cap - 4;
    for (int k = 0; k < m; ++k) out[k] = s[k];
    out[m] = 0;
    scat(out, suffix, cap);
}

/* ============================================================================
 * 7. Dialogs / text input
 * ============================================================================ */
struct Ui;

/* ============================================================================
 * 8. The file browser
 * ============================================================================ */
enum Mode { MODE_LIST, MODE_VIEW, MODE_INFO, MODE_INPUT, MODE_MSG, MODE_HELP };

struct Browser {
    Fs fs;
    Input in;

    Mode mode;
    Mode back_mode;

    int sel;
    int scroll_top;

    /* layout (mirrors fm.py: derived from the text height) */
    int text_h;
    int row_h;
    int header_h;
    int footer_h;
    int list_top;
    int list_bottom;
    int nrows;
    int foot_line_h;
    int foot_lines;

    /* viewer state */
    char vname[MAX_NAME];
    unsigned int vsize;
    unsigned int voff;
    int vtop;
    char vlines[64][88];
    int nvlines;

    /* info page */
    int info_idx;

    /* help page (any key returns to the listing) */
    bool help_mode;

    /* copy mode */
    bool copying;
    char copy_src[MAX_PATH];
    char copy_src_name[MAX_NAME];
    bool copy_src_is_dir;

    /* message dialog */
    char msg_title[96];
    char msg_hint[96];
    int msg_timeout_ms;


    /* text input dialog */
    char in_prompt[64];
    char in_text[MAX_NAME];
    int in_len;
    bool in_ok;

    Browser() {
        mode = MODE_LIST;
        back_mode = MODE_LIST;
        sel = 0;
        scroll_top = 0;
        voff = 0;
        vtop = 0;
        nvlines = 0;
        info_idx = -1;
        copying = false;
        help_mode = false;
        msg_timeout_ms = 0;
        in_len = 0;
        in_ok = false;
        vsize = 0;
        copy_src[0] = 0;
        copy_src_name[0] = 0;
        copy_src_is_dir = false;
        text_h = TEXT_CELL_H;
        row_h = text_h;
        foot_line_h = text_h;
        foot_lines = 0;
        header_h = text_h + 2;
        footer_h = 0; /* no permanent footer */
        list_top = header_h;
        list_bottom = SCREEN_H - 2;
        nrows = (list_bottom - list_top) / row_h;
    }

    /* ---------------- drawing primitives ---------------- */
    void clear_screen(int color) { fill_rect(0, 0, SCREEN_W, SCREEN_H, color); }

    void txt(int x, int y, const char *s, int color) { draw_text(x, y, s, color); }

    void txt_fit(int x, int y, const char *s, int max_w, int color) {
        char buf[MAX_PATH * 2];
        fit_text(s, max_w, "~", buf, sizeof(buf));
        txt(x, y, buf, color);
    }

    void txt_right(int right_x, int y, const char *s, int color) {
        int w = text_width(s);
        txt(right_x - w, y, s, color);
    }

    /* ---------------- list view ---------------- */
    void clamp_sel() {
        int n = fs.count;
        if (n == 0) {
            sel = 0;
            scroll_top = 0;
            return;
        }
        if (sel < 0) sel = 0;
        if (sel >= n) sel = n - 1;
        if (sel < scroll_top) scroll_top = sel;
        else if (sel >= scroll_top + nrows) scroll_top = sel - nrows + 1;
    }

    void draw_list() {
        clear_screen(COL_BG);
        fill_rect(0, 0, SCREEN_W, header_h, COL_HEAD);
        if (copying) {
            char buf[MAX_PATH * 2 + 32];
            scpy(buf, UI_COPY_BANNER_PREFIX, sizeof(buf));
            scat(buf, copy_src_name, sizeof(buf));
            scat(buf, UI_COPY_BANNER_MID, sizeof(buf));
            scat(buf, fs.cwd, sizeof(buf));
            txt_fit(2, 1, buf, SCREEN_W - 6, COL_HEAD_TX);
        } else {
            txt_fit(2, 1, fs.cwd, SCREEN_W - 6 - 40, COL_HEAD_TX);
            char cnt[32];
            u2dec((unsigned int)fs.count, cnt);
            scat(cnt, UI_ITEMS_SUFFIX, sizeof(cnt));
            txt_right(SCREEN_W - 2, 1, cnt, COL_HEAD_TX);
        }

        if (fs.count == 0) {
            txt(4, list_top, UI_EMPTY_DIR, COL_SZ);
        } else {
            for (int r = 0; r < nrows; ++r) {
                int i = scroll_top + r;
                if (i >= fs.count) break;
                Entry &it = fs.items[i];
                int y = list_top + r * row_h;
                bool is_sel = (i == sel);
                if (is_sel) fill_rect(0, y, SCREEN_W, row_h, COL_SEL);
                char disp[MAX_PATH * 2];
                if (it.is_dir) {
                    scpy(disp, it.name, sizeof(disp));
                    scat(disp, "/", sizeof(disp));
                    txt_fit(2, y, disp, SCREEN_W - 4, is_sel ? COL_SEL_TX : COL_DIR);
                } else {
                    txt_fit(2, y, it.name, SCREEN_W - 4 - SIZE_COL_W, is_sel ? COL_SEL_TX : COL_TX);
                    char sz[24];
                    human_size(it.size, sz, sizeof(sz));
                    txt_right(SCREEN_W - 2, y, sz, is_sel ? COL_SEL_TX : COL_SZ);
                }
            }
        }

        /* No permanent footer: press help (or h) for the full key guide.  Only the copy
         * mode still shows a hint line, because it is a modal state nobody can guess. */
        if (copying) {
            fill_rect(0, list_bottom, SCREEN_W, SCREEN_H - list_bottom, COL_FOOTBG);
            txt(2, list_bottom + 1, UI_COPY_MODE_HINT, COL_TX);
        }
    }

    /* Full screen key guide, shown with the help key / h. */
    void draw_help() {
        clear_screen(COL_BG);
        fill_rect(0, 0, SCREEN_W, header_h, COL_HEAD);
        txt(2, 1, UI_HELP_TITLE, COL_HEAD_TX);
        const char *const *lines = UI_HELP_LINES;
        const int nlines = (int)(sizeof(UI_HELP_LINES) / sizeof(UI_HELP_LINES[0]));
        int y = header_h + 1; /* below the title bar, not on top of it */
        for (int i = 0; i < nlines; ++i) {
            if (y + TEXT_CELL_H > SCREEN_H) break;
            txt(2, y, lines[i], COL_TX);
            y += TEXT_CELL_H;
        }
    }

    void draw_dialog(const char *title, const char *hint) {
        int bw = 300, bh = 2 + 2 * text_h + 8;
        int bx = (SCREEN_W - bw) / 2;
        int by = (SCREEN_H - bh) / 2;
        fill_rect(bx, by, bw, bh, COL_DLGB);
        fill_rect(bx + 2, by + 2, bw - 4, bh - 4, COL_DLG);
        txt_fit(bx + 6, by + 3, title, bw - 12, COL_TX);
        txt_fit(bx + 6, by + 3 + text_h, hint, bw - 12, COL_SZ);
    }

    void draw_view() {
        clear_screen(COL_BG);
        fill_rect(0, 0, SCREEN_W, header_h, COL_HEAD);
        char head[128];
        scpy(head, vname, sizeof(head));
        scat(head, "  [", sizeof(head));
        char num[24];
        u2dec(voff, num);
        scat(head, num, sizeof(head));
        scat(head, "/", sizeof(head));
        u2dec(vsize, num);
        scat(head, num, sizeof(head));
        scat(head, "]", sizeof(head));
        txt_fit(2, 1, head, SCREEN_W - 6, COL_HEAD_TX);
        int pitch = VIEW_LINE_H;
        int vrows = (list_bottom - list_top) / pitch;
        for (int r = 0; r < vrows; ++r) {
            int i = vtop + r;
            if (i >= nvlines) break;
            txt_fit(2, list_top + r * pitch, vlines[i], SCREEN_W - 4, COL_TX);
        }
        fill_rect(0, list_bottom, SCREEN_W, SCREEN_H - list_bottom, COL_FOOTBG);
        txt(2, list_bottom + 1, "Up/Dn line   left/right block   esc back", COL_TX);
    }

    void draw_info() {
        clear_screen(COL_BG);
        fill_rect(0, 0, SCREEN_W, header_h, COL_HEAD);
        txt(2, 1, UI_INFO_TITLE, COL_HEAD_TX);
        if (info_idx < 0 || info_idx >= fs.count) {
            txt(4, list_top, UI_INFO_NONE, COL_TX);
        } else {
            Entry &it = fs.items[info_idx];
            char full[MAX_PATH];
            fs.path_of(it.name, full, MAX_PATH);
            char line[MAX_PATH * 2];
            char num[32];
            int y = list_top;

            scpy(line, UI_INFO_NAME, sizeof(line));
            scat(line, it.name, sizeof(line));
            txt_fit(4, y, line, SCREEN_W - 8, COL_TX);
            y += row_h;

            scpy(line, UI_INFO_PATH, sizeof(line));
            scat(line, full, sizeof(line));
            txt_fit(4, y, line, SCREEN_W - 8, COL_TX);
            y += row_h;

            char sz[24];
            human_size(it.size, sz, sizeof(sz));
            scpy(line, UI_INFO_SIZE, sizeof(line));
            scat(line, sz, sizeof(line));
            scat(line, " (", sizeof(line));
            u2dec(it.size, num);
            scat(line, num, sizeof(line));
            scat(line, "B)", sizeof(line));
            txt_fit(4, y, line, SCREEN_W - 8, COL_TX);
            y += row_h;

            scpy(line, UI_INFO_TIME, sizeof(line));
            format_mtime(it.mtime, num);
            scat(line, num, sizeof(line));
            txt_fit(4, y, line, SCREEN_W - 8, COL_TX);
            y += row_h;

            scpy(line, UI_INFO_ATTR, sizeof(line));
            attr_str(it.attr, num);
            scat(line, num, sizeof(line));
            txt_fit(4, y, line, SCREEN_W - 8, COL_TX);
            y += row_h;

            scpy(line, UI_INFO_TYPE, sizeof(line));
            scat(line, it.is_dir ? UI_INFO_DIR : UI_INFO_FILE, sizeof(line));
            txt_fit(4, y, line, SCREEN_W - 8, COL_TX);
        }
        fill_rect(0, list_bottom, SCREEN_W, SCREEN_H - list_bottom, COL_FOOTBG);
        txt(2, list_bottom + 1, "", COL_TX);
    }

    /* Two decimal digits with a leading zero. */
    static void u2dec2p(unsigned int v, char *out) {
        out[0] = (char)('0' + (v / 10) % 10);
        out[1] = (char)('0' + v % 10);
        out[2] = 0;
    }

    /* FAT DATE/DATE packed timestamp (D16 | T16) printed in decimal. */
    static void format_mtime(unsigned int mtime, char *out) {
        unsigned int date = (mtime >> 16) & 0xFFFF;
        unsigned int time = mtime & 0xFFFF;
        unsigned int y = ((date >> 9) & 0x7F) + 1980;
        unsigned int mo = (date >> 5) & 0xF;
        unsigned int d = date & 0x1F;
        unsigned int h = (time >> 11) & 0x1F;
        unsigned int mi = (time >> 5) & 0x3F;
        char b[8];
        u2dec(y, out);          /* decimal year, e.g. 2026 */
        int n = slen(out);
        out[n++] = '-';
        u2dec2p(mo, b);
        out[n++] = b[0];
        out[n++] = b[1];
        out[n++] = '-';
        u2dec2p(d, b);
        out[n++] = b[0];
        out[n++] = b[1];
        out[n++] = ' ';
        u2dec2p(h, b);
        out[n++] = b[0];
        out[n++] = b[1];
        out[n++] = ':';
        u2dec2p(mi, b);
        out[n++] = b[0];
        out[n++] = b[1];
        out[n] = 0;
    }

    static void attr_str(unsigned char attr, char *out) {
        out[0] = (attr & 0x10) ? 'D' : '-';
        out[1] = (attr & 0x01) ? 'R' : '-';
        out[2] = (attr & 0x02) ? 'H' : '-';
        out[3] = (attr & 0x04) ? 'S' : '-';
        out[4] = (attr & 0x20) ? 'A' : '-';
        out[5] = 0;
    }

    void redraw() {
        if (help_mode) {
            draw_help();
            return;
        }
        switch (mode) {
        case MODE_VIEW: draw_view(); break;
        case MODE_INFO: draw_info(); break;
        case MODE_INPUT: draw_input(); break;
        case MODE_MSG: draw_msg(); break;
        case MODE_HELP: draw_help(); break;
        default: draw_list(); break;
        }
    }

    /* ---------------- viewer ---------------- */
    static bool looks_binary(const unsigned char *d, int n) {
        if (n <= 0) return false;
        int bad = 0;
        for (int i = 0; i < n; ++i) {
            unsigned char b = d[i];
            if (b == 0 || (b < 32 && b != 9 && b != 10 && b != 13)) ++bad;
        }
        return bad * 100 > n * 3;
    }

    void add_vline(const char *s) {
        if (nvlines >= 64) return;
        scpy(vlines[nvlines], s, 88);
        ++nvlines;
    }

    /* Eight bytes per row.  The address is printed compactly (low 4 nibbles of the offset)
     * and the ASCII column is kept: 6 + 23 + 8 = 37 cells, i.e. 296 px of the 320 px panel,
     * so nothing is truncated. */
    void hex_dump(const unsigned char *d, int n, unsigned int base) {
        char line[64];
        for (int i = 0; i < n; i += 8) {
            int p = 0;
            u2hex((base + (unsigned int)i) & 0xFFFF, 4, line + p);
            p += 4;
            line[p++] = ' ';
            int cnt = (n - i) > 8 ? 8 : (n - i);
            for (int k = 0; k < 8; ++k) {
                if (k < cnt) {
                    u2hex(d[i + k], 2, line + p);
                    p += 2;
                } else {
                    line[p++] = ' ';
                    line[p++] = ' ';
                }
                if (k < 7) line[p++] = ' ';
            }
            line[p++] = ' ';
            for (int k = 0; k < cnt; ++k) {
                unsigned char b = d[i + k];
                line[p++] = (b >= 32 && b < 127) ? (char)b : '.';
            }
            line[p] = 0;
            add_vline(line);
        }
    }

    /* Split a chunk into display lines.  `gb` selects GB2312 decoding (Chinese source
     * files on the calculator are almost always GB2312/GBK, not UTF-8). */
    void text_dump(const unsigned char *d, int n, bool gb) {
        char line[200];
        int p = 0;
        int i = 0;
        while (i < n) {
            unsigned char b = d[i];
            if (b == '\n') {
                line[p] = 0;
                add_vline(line);
                p = 0;
                ++i;
                if (nvlines >= 64) return;
                continue;
            }
            if (b == '\r') {
                ++i;
                continue;
            }
            if (b < 0x80) {
                if (p < 190) line[p++] = (char)b;
                ++i;
                continue;
            }
            int used = 1;
            unsigned int cp;
            if (gb) {
                cp = gb2312_decode(d + i, &used);
            } else {
                cp = utf8_next((const char *)(d + i), &used);
            }
            if (used < 1) used = 1;
            i += used;
            if (p < 190) {
                /* re-encode the codepoint as UTF-8 so the renderer can decode it again */
                if (cp < 0x800) {
                    line[p++] = (char)(0xC0 | (cp >> 6));
                    if (p < 190) line[p++] = (char)(0x80 | (cp & 0x3F));
                } else {
                    line[p++] = (char)(0xE0 | (cp >> 12));
                    if (p < 190) line[p++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                    if (p < 190) line[p++] = (char)(0x80 | (cp & 0x3F));
                }
            }
        }
        if (p) {
            line[p] = 0;
            add_vline(line);
        }
    }

    void view_load() {
        static unsigned char buf[VIEW_CHUNK + 16];
        nvlines = 0;
        char full[MAX_PATH];
        fs.path_of(vname, full, MAX_PATH);
        int n = fs.read_chunk(full, voff, VIEW_CHUNK, buf);
        if (n <= 0) {
            add_vline(n == 0 ? UI_VIEW_EMPTY : UI_VIEW_FAIL);
            vtop = 0;
            return;
        }
        if (looks_binary(buf, n)) {
            hex_dump(buf, n, voff);
        } else {
            /* drop a partial UTF-8 tail so decoding never fails at chunk borders */
            int cut = n;
            int trailing = 0;
            while (cut > 0 && trailing < 4 && ((buf[cut - 1] & 0xC0) == 0x80)) {
                --cut;
                ++trailing;
            }
            if (cut < n && (trailing == 0 || ((buf[cut - 1] & 0xC0) != 0xC0 && trailing < 3))) {
                /* keep everything: worst case one replacement char */
            }
            /* UTF-8 when the chunk validates, GB2312 otherwise */
            text_dump(buf, n, !is_utf8(buf, n));
        }
        vtop = 0;
    }

    void view_file(int idx) {
        if (idx < 0 || idx >= fs.count) return;
        scpy(vname, fs.items[idx].name, MAX_NAME);
        vsize = fs.items[idx].size;
        voff = 0;
        mode = MODE_VIEW;
        view_load();
    }

    /* ---------------- messages ---------------- */
    void show_msg(const char *title, const char *hint, int timeout_ms) {
        scpy(msg_title, title, sizeof(msg_title));
        scpy(msg_hint, hint, sizeof(msg_hint));
        msg_timeout_ms = timeout_ms;
        back_mode = (mode == MODE_INPUT || mode == MODE_MSG) ? MODE_LIST : mode;
        mode = MODE_MSG;
        draw_msg();
    }

    void draw_msg() { draw_dialog(msg_title, msg_hint); }

    /* Blocking message: draws and waits for a key/tap or timeout (port of flash()). */
    void flash(const char *title, const char *hint, int timeout_ms) {
        show_msg(title, hint, timeout_ms);
        int waited = 0;
        while (waited < timeout_ms) {
            unsigned k = in.wait_key(20);
            if (k_is_esc(k) || k_is_enter(k)) {
                in.wait_release();
                break;
            }
            int x, y;
            if (in.take_tap(&x, &y)) break;
            waited += 20;
        }
        mode = back_mode;
        redraw();
    }

    /* y/n confirmation (port of confirm()). */
    bool confirm(const char *title) {
        show_msg(title, UI_CONFIRM_HINT, 0);
        for (;;) {
            unsigned k = in.wait_key(100);
            if (!k) {
                int x, y;
                in.take_tap(&x, &y);
                continue;
            }
            in.wait_release();
            if (k_is_letter(k, 'y') || k_is_enter(k)) {
                mode = back_mode;
                redraw();
                return true;
            }
            if (k_is_letter(k, 'n') || k_is_esc(k)) {
                mode = back_mode;
                redraw();
                return false;
            }
        }
    }

    /* ---------------- text input ---------------- */
    void draw_input() {
        int bw = 300, bh = 2 + 2 * text_h + 8;
        int bx = (SCREEN_W - bw) / 2;
        int by = (SCREEN_H - bh) / 2;
        fill_rect(bx, by, bw, bh, COL_DLGB);
        fill_rect(bx + 2, by + 2, bw - 4, bh - 4, COL_DLG);
        txt_fit(bx + 6, by + 3, in_prompt, bw - 12, COL_TX);
        /* the editing line with a caret */
        char shown[MAX_NAME + 4];
        scpy(shown, in_text, sizeof(shown));
        /* scroll the visible part so the caret stays visible */
        int w = text_width(shown) + HALF_ADV;
        while (w > bw - 16 && shown[0]) {
            int n = slen(shown);
            for (int i = 0; i < n; ++i) shown[i] = shown[i + 1];
            w = text_width(shown) + HALF_ADV;
        }
        fill_rect(bx + 4, by + 3 + text_h, bw - 8, text_h, COL_DLG);
        txt(bx + 6, by + 3 + text_h, shown, COL_TX);
        int cx = bx + 6 + text_width(shown);
        fill_rect(cx, by + 3 + text_h + 9, 5, 1, COL_TX); /* caret */
    }

    /* Modal one line text input.  Returns true on Enter. */
    bool ask_text(const char *prompt, const char *initial, char *out, int cap) {
        scpy(in_prompt, prompt, sizeof(in_prompt));
        scpy(in_text, initial ? initial : "", sizeof(in_text));
        in_len = slen(in_text);
        back_mode = MODE_LIST;
        mode = MODE_INPUT;
        draw_input();
        for (;;) {
            unsigned k = in.wait_key(100);
            if (!k) continue;
            in.wait_release();
            if (k_is_enter(k)) {
                scpy(out, in_text, cap);
                mode = MODE_LIST;
                redraw();
                return true;
            }
            if (k_is_esc(k) || k == DK_ON) {
                mode = MODE_LIST;
                redraw();
                return false;
            }
            if (k_is_bs(k) || k == 0x8) {
                if (in_len > 0) in_text[--in_len] = 0;
                draw_input();
                continue;
            }
            char ch = 0;
            if (k >= KL_FIRST_PRINT && k <= KL_LAST_PRINT) {
                /* the letter keys report their lowercase ASCII value (a=14 .. y=42) */
                ch = (char)k;
                if (ch >= 'A' && ch <= 'Z') ch = lower(ch);
            } else if (k == DK_SPACE) {
                ch = ' ';
            }
            if (ch && in_len < MAX_NAME - 2) {
                in_text[in_len++] = ch;
                in_text[in_len] = 0;
                draw_input();
            }
        }
    }

    /* ---------------- actions ---------------- */
    void refresh() {
        /* The current directory is tracked by the app (Fs::chdir / Fs::probe_start_path).
         * _wgetcurdir() is not consulted here on purpose: on this firmware it returned
         * "C:\\" after a successful _wchdir() and scribbled over the path buffer. */
        fs.list_dir(fs.cwd);
        if (sel >= fs.count) sel = fs.count ? fs.count - 1 : 0;
        if (sel < 0) sel = 0;
        clamp_sel();
    }

    void go_up() {
        if (fs.chdir("..")) refresh();
    }

    void do_cd(const char *target) {
        char t[MAX_PATH];
        scpy(t, target, MAX_PATH);
        /* strip quotes like the Python version */
        for (int i = 0; t[i]; ++i)
            if (t[i] == '"' || t[i] == '\'') t[i] = ' ';
        if (t[0] && t[1] == ':' && !t[2]) {
            /* "C:" -- switch the active drive through the filesystem itself */
            char rooted[8];
            rooted[0] = upper(t[0]);
            rooted[1] = ':';
            rooted[2] = '\\';
            rooted[3] = 0;
            if (fs.chdir(rooted)) refresh();
            else flash(UI_CANNOT_ENTER, rooted, 1500);
            return;
        }
        if (fs.chdir(t)) refresh();
        else flash(UI_CANNOT_ENTER, t, 1500);
    }

    void open_sel() {
        if (sel >= fs.count) {
            LOG("  open: nothing selected\n");
            return;
        }
        Entry &it = fs.items[sel];
        if (it.is_dir) {
            LOG("  open: dir\n");
            if (fs.chdir(it.name)) {
                refresh();
                LOG("  open: chdir ok\n");
            } else {
                LOG("  open: chdir failed\n");
            }
        } else if (!copying) {
            LOG("  open: file\n");
            view_file(sel);
        }
    }

    void do_delete() {
        if (sel >= fs.count) return;
        char name[MAX_NAME];
        scpy(name, fs.items[sel].name, MAX_NAME);
        bool is_dir = fs.items[sel].is_dir;
        char title[128];
        scpy(title, UI_DELETE_Q_PRE, sizeof(title));
        scat(title, name, sizeof(title));
        scat(title, UI_DELETE_Q_POST, sizeof(title));
        if (!confirm(title)) return;
        char full[MAX_PATH];
        fs.path_of(name, full, MAX_PATH);
        /* paint the "working" dialog before starting, like the Python version */
        show_msg(UI_DELETING, name, 0);
        bool ok = fs.remove_recursive(full, 0);
        mode = MODE_LIST;
        if (ok) refresh();
        else flash(UI_DELETE_FAIL, name, 2000);
    }

    void do_mkdir() {
        char name[MAX_NAME];
        if (!ask_text(UI_MKDIR_PROMPT, "", name, MAX_NAME)) return;
        while (*name && name[slen(name) - 1] == ' ') name[slen(name) - 1] = 0;
        if (!*name) return;
        char full[MAX_PATH];
        fs.path_of(name, full, MAX_PATH);
        if (fs.mkdir(full)) refresh();
        else flash(UI_MKDIR_FAIL, name, 2000);
    }

    void do_rename() {
        if (sel >= fs.count) return;
        char name[MAX_NAME];
        scpy(name, fs.items[sel].name, MAX_NAME);
        char newname[MAX_NAME];
        if (!ask_text(UI_RENAME_PROMPT, name, newname, MAX_NAME)) return;
        if (!*newname || !strcmp(newname, name)) return;
        char a[MAX_PATH], b[MAX_PATH];
        fs.path_of(name, a, MAX_PATH);
        fs.path_of(newname, b, MAX_PATH);
        if (fs.rename(a, b)) refresh();
        else flash(UI_RENAME_FAIL, newname, 2000);
    }

    void do_copy() {
        if (sel >= fs.count) return;
        Entry &it = fs.items[sel];
        scpy(copy_src_name, it.name, MAX_NAME);
        fs.path_of(it.name, copy_src, MAX_PATH);
        copy_src_is_dir = it.is_dir;
        copying = true;
        mode = MODE_LIST;
    }

    void copy_confirm() {
        char dst[MAX_PATH];
        fs.path_of(copy_src_name, dst, MAX_PATH);
        bool is_dir = copy_src_is_dir;
        char src[MAX_PATH];
        scpy(src, copy_src, MAX_PATH);
        char name[MAX_NAME];
        scpy(name, copy_src_name, MAX_NAME);
        copying = false;
        copy_src[0] = 0;
        if (!strcmp(src, dst)) {
            flash(UI_COPY_SAME, name, 2000);
            return;
        }
        show_msg(UI_COPYING, name, 0);
        bool ok = is_dir ? fs.copy_recursive(src, dst, 0) : fs.copy_file(src, dst);
        mode = MODE_LIST;
        if (ok) {
            char t[128];
            scpy(t, UI_COPIED, sizeof(t));
            scat(t, name, sizeof(t));
            flash(t, fs.cwd, 1500);
            refresh();
        } else {
            flash(UI_COPY_FAIL, name, 2000);
        }
    }

    void copy_cancel() {
        copying = false;
        copy_src[0] = 0;
        copy_src_name[0] = 0;
    }

    /* ---------------- key dispatch ---------------- */
    bool on_key_list(unsigned k, bool *quit) {
        if (help_mode) {
            help_mode = false; /* any key closes the help page */
            return true;
        }
        if (copying) {
            if (k_is_up(k)) {
                --sel;
                clamp_sel();
                return true;
            }
            if (k_is_down(k)) {
                ++sel;
                clamp_sel();
                return true;
            }
            if (k_is_enter(k) || k_is_right(k)) {
                if (sel < fs.count && fs.items[sel].is_dir) {
                    if (fs.chdir(fs.items[sel].name)) refresh();
                }
                return true;
            }
            if (k_is_left(k) || k_is_letter(k, 'u')) {
                go_up();
                return true;
            }
            if (k_is_letter(k, 'y')) {
                copy_confirm();
                return true;
            }
            if (k_is_esc(k) || k_is_letter(k, 'q')) {
                copy_cancel();
                return true;
            }
            return false;
        }
        if (k_is_up(k)) {
            LOG("  act: sel-\n");
            --sel;
            clamp_sel();
            return true;
        }
        if (k_is_down(k)) {
            LOG("  act: sel+\n");
            ++sel;
            clamp_sel();
            return true;
        }
        if (k_is_enter(k) || k_is_right(k)) {
            LOG("  act: open\n");
            open_sel();
            return true;
        }
        if (k_is_left(k) || k_is_letter(k, 'u')) {
            LOG("  act: up\n");
            go_up();
            return true;
        }
        if (k_is_esc(k) || k_is_letter(k, 'q')) {
            LOG("  act: quit?\n");
            if (confirm(UI_QUIT_Q)) {
                LOG("  act: quit yes\n");
                *quit = true;
            } else {
                LOG("  act: quit no\n");
            }
            return true;
        }
        if (k_is_letter(k, 'v')) {
            open_sel();
            return true;
        }
        if (k_is_letter(k, 'i')) {
            if (sel < fs.count) {
                info_idx = sel;
                mode = MODE_INFO;
            }
            return true;
        }
        if (k_is_letter(k, 'x')) {
            do_delete();
            return true;
        }
        if (k_is_letter(k, 'c')) {
            do_copy();
            return true;
        }
        if (k_is_letter(k, 'm')) {
            do_mkdir();
            return true;
        }
        if (k_is_letter(k, 'r')) {
            refresh();
            return true;
        }
        if (k_is_letter(k, 'h') || k == DK_HELP) {
            help_mode = true;
            return true;
        }
        if (k_is_letter(k, 'y')) {
            do_rename();
            return true;
        }
        if (k == DK_ON) {
            *quit = true;
            return true;
        }
        return false;
    }

    bool on_key_view(unsigned k, bool *quit) {
        (void)quit;
        if (k_is_up(k)) {
            if (vtop > 0) --vtop;
            return true;
        }
        if (k_is_down(k)) {
            int vrows = (list_bottom - list_top) / VIEW_LINE_H;
            if (vtop + vrows < nvlines) ++vtop;
            return true;
        }
        if (k_is_left(k)) {
            if (voff >= VIEW_CHUNK) {
                voff -= VIEW_CHUNK;
                view_load();
            } else if (voff > 0) {
                voff = 0;
                view_load();
            }
            return true;
        }
        if (k_is_right(k) || k_is_enter(k)) {
            if (voff + VIEW_CHUNK < vsize) {
                voff += VIEW_CHUNK;
                view_load();
            }
            return true;
        }
        if (k_is_esc(k) || k_is_bs(k)) {
            mode = MODE_LIST;
            return true;
        }
        return false;
    }

    bool on_key_info(unsigned k, bool *quit) {
        (void)quit;
        if (k_is_esc(k) || k_is_enter(k) || k_is_left(k)) {
            mode = MODE_LIST;
            return true;
        }
        return false;
    }

    /* ---------------- main loop ---------------- */
    /* Quit like the doom/nofrendo examples do: leave the main loop, end the worker
     * threads, and let main() return.  No infinite loop, no LCD power calls. */
    void shutdown() {
        LOG("shutdown\n");
        if (in.thread) OSTerminateThread(in.thread, 0);
        LOG("shutdown: thread ended\n");
    }

    void run() {
        boot_step(1);
        LOG("picking starting directory\n");
        fs.get_cwd();
        BOOT_SAY("fm: cwd\n");
        boot_step(2);
        LOG("first list_dir\n");
        refresh();
        BOOT_SAY("fm: listed\n");
        boot_step(3);
        mode = MODE_LIST;
        LOG("first redraw\n");
        redraw();
        LOG("first redraw returned\n");
        BOOT_SAY("fm: drawn\n");
        boot_step(4);
        LOG("wait_release\n");
        in.wait_release();
        LOG("creating event thread\n");
        in.start();
        BOOT_SAY("fm: input thread\n");
        boot_step(5);
        for (;;) {
            unsigned k = in.wait_key(60);
            bool acted = false;
            bool quit = false;
            if (k) {
                g_last_key = k;
#ifdef FM_KEY_DEBUG
                {
                    char m[48];
                    scpy(m, "key code 0x", sizeof(m));
                    char hx[4];
                    u2hex(k, 2, hx);
                    scat(m, hx, sizeof(m));
                    scat(m, " km=", sizeof(m));
                    u2hex(DK_ESC, 2, hx);
                    scat(m, hx, sizeof(m));
                    if (k_is_esc(k)) scat(m, " (esc)", sizeof(m));
                    else if (k_is_letter(k, 'y')) scat(m, " (y)", sizeof(m));
                    else if (k_is_letter(k, 'n')) scat(m, " (n)", sizeof(m));
                    else if (k_is_enter(k)) scat(m, " (enter)", sizeof(m));
                    scat(m, "\n", sizeof(m));
                    LOG(m);
                }
#endif
                /* No wait_release() here: the input layer handles repeat and the key
                 * release resets the state, so holding a key scrolls continuously. */
                if (mode == MODE_LIST) acted = on_key_list(k, &quit);
                else if (mode == MODE_VIEW) acted = on_key_view(k, &quit);
                else if (mode == MODE_INFO) acted = on_key_info(k, &quit);
            }
            int x, y;
            if (in.take_tap(&x, &y)) {
                if (mode == MODE_LIST && !copying && y >= list_top && y < list_bottom) {
                    int r = (y - list_top) / row_h;
                    int i = scroll_top + r;
                    if (i >= 0 && i < fs.count) {
                        sel = i;
                        clamp_sel();
                        open_sel();
                        acted = true;
                    }
                } else if (mode == MODE_VIEW || mode == MODE_INFO) {
                    mode = MODE_LIST;
                    acted = true;
                }
            }
            if (quit) {
                shutdown();
                return;
            }
            if (acted) redraw();
        }
    }
};


/* ============================================================================
 * 9. Entry point
 * ============================================================================ */
static Browser g_browser;


int main() {
    /* Keep the app alive: the SDK's own samples all disable the idle power-off.
     * 85 == SYSVAR_SYSPWCTRL, 2 == SET, 1 == TRUE. */
    LOG("fm.elf boot: main entered\n");
    SetSystemVariable(85, 2, 1);
    LOG("SetSystemVariable ok\n");
    boot_step(0);
    g_browser.run();
    /* Returning from main is how the SDK's doom/nofrendo ports leave the app. */
    LOG("main: returned from run, exiting\n");
    return 0;
}
