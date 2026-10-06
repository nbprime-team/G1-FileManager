/*
 * fm_min.cpp -- minimal bring-up build (make min).
 *
 * Answers one question only: does a native external app start at all on this firmware, and
 * does file logging work?  It appends one line to C:\APPS\fm.log and then keeps the screen
 * alive.  If fm.log stays empty with this build, the problem is not in the file manager --
 * it is in loading/starting an external app or in the log path itself.
 */
#include <stdint.h>
#include "muteki.h"
#include "besta_fixes.h"

extern "C" unsigned long SetSystemVariable(unsigned short, unsigned short, unsigned long);

static UTF16 *u16(const char *s, int slot) {
    static UTF16 bufs[2][64];
    UTF16 *out = bufs[slot ? 1 : 0];
    int o = 0;
    if (!s) s = "";
    for (int i = 0; s[i] && o < 62; ++i) out[o++] = (UTF16)(unsigned char)s[i];
    out[o] = 0;
    return out;
}

static void log_line(const char *msg, const char *path) {
    file_descriptor_t *fd = __wfopen(u16(path, 0), u16("ab", 1));
    if (!fd) return;
    int n = 0;
    while (msg[n]) ++n;
    _fwrite(msg, 1, (size_t)n, fd);
    _fclose(fd);
}

int main() {
    log_line("fm_min: main entered\n", "C:\\APPS\\fm.log");
    SetSystemVariable(85, 2, 1);
    log_line("fm_min: SetSystemVariable ok\n", "C:\\APPS\\fm.log");
    log_line("fm_min: fallback path works\n", "C:\\fm_min.log");
    /* visible heartbeat so a working app is obvious even without the log */
    for (;;) {
        rgbSetColor(0x1E3A8A);
        FillRect(0, 0, 319, 119, 0);
        rgbSetColor(0xFFFFFF);
        FillRect(0, 120, 319, 239, 0);
        OSSleep(600);
        rgbSetColor(0xFFFFFF);
        FillRect(0, 0, 319, 119, 0);
        rgbSetColor(0x1E3A8A);
        FillRect(0, 120, 319, 239, 0);
        OSSleep(600);
    }
    return 0;
}
