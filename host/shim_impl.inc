/* Host implementations of the SDK calls used by fm.cpp's font layer and input facade.
 * Included by host_test.cpp right before src/fm.cpp. */

typedef struct file_descriptor_s {
    int dummy;
} file_descriptor_t;

/* host log: the tests print it, so log_line() goes to stdout */
file_descriptor_t *__wfopen(const UTF16 *p, const UTF16 *m) {
    (void)p;
    (void)m;
    static file_descriptor_t fd;
    return &fd;
}
size_t _fwrite(const void *p, size_t size, size_t nmemb, file_descriptor_t *f) {
    (void)f;
    fwrite(p, size, nmemb, stdout);
    return nmemb;
}
/* fflush host stub */
int __fflush(file_descriptor_t *f) {
    (void)f;
    return 0;
}
void FillRect(short x0, short y0, short x1, short y1, unsigned int flags) {
    (void)flags;
    if (x1 < x0) {
        short t = x0;
        x0 = x1;
        x1 = t;
    }
    if (y1 < y0) {
        short t = y0;
        y0 = y1;
        y1 = t;
    }
    int r = (g_cur >> 16) & 0xFF, g = (g_cur >> 8) & 0xFF, b = g_cur & 0xFF;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) host_set(x, y, r, g, b);
}

int rgbSetColor(int color) {
    int old = g_cur;
    g_cur = color;
    return old;
}
int rgbSetBkColor(int color) { return color; }
void ClearScreen(bool f) { (void)f; }
short WriteString(short x, short y, const void *s, unsigned int f) {
    (void)x;
    (void)y;
    (void)s;
    (void)f;
    return 0;
}
short WriteChar(short x, short y, UTF16 c, unsigned int f) {
    (void)x;
    (void)y;
    (void)c;
    (void)f;
    return 0;
}
short SetFontType(short t) { return t; }
short GetFontType(void) { return 0; }
int GetFontHeight(uint8_t t) {
    (void)t;
    return 16;
}
short GetCharWidth(UTF16 c, uint8_t t) {
    (void)c;
    (void)t;
    return 8;
}

bool GetEvent(ui_event_t *e) {
    (void)e;
    return false;
}
void ClearAllEvents(void) {}
thread_t *OSCreateThread(int (*func)(void *), void *d, size_t s, bool defer) {
    (void)func;
    (void)d;
    (void)s;
    (void)defer;
    return 0;
}
int OSTerminateThread(thread_t *t, int c) {
    (void)t;
    (void)c;
    return 0;
}
void OSSleep(short ms) { (void)ms; }

short _wfindfirst(const UTF16 *p, find_context_t *c, int m) {
    (void)p;
    (void)c;
    (void)m;
    return -1;
}
short _afindfirst(const char *p, find_context_t *c, int m) {
    (void)p;
    (void)c;
    (void)m;
    return -1;
}
short _afindnext(find_context_t *c) {
    (void)c;
    return -1;
}
void LCDOn(void) {}
void LCDOff(void) {}
unsigned long SetSystemVariable(unsigned short t, unsigned short m, unsigned long v) {
    (void)t;
    (void)m;
    (void)v;
    return 0;
}
short _wfindnext(find_context_t *c) {
    (void)c;
    return -1;
}
int _findclose(find_context_t *c) {
    (void)c;
    return 0;
}
short _wgetcurdir(void *u, UTF16 *b) {
    (void)u;
    b[0] = 'C';
    b[1] = ':';
    b[2] = '\\';
    b[3] = 0;
    return 0;
}
short _wchdir(const UTF16 *p) {
    (void)p;
    return 0;
}
short _achdir(const char *p) {
    (void)p;
    return 0;
}
short _wfgetattr(UTF16 *p) {
    (void)p;
    return 0x10;
}
bool __wremove(const UTF16 *p) {
    (void)p;
    return true;
}
bool _aremove(const char *p) {
    (void)p;
    return true;
}
int _amkdir(char *p) {
    (void)p;
    return 0;
}
int _armdir(char *p) {
    (void)p;
    return 0;
}
short _arename(const char *a, const char *b) {
    (void)a;
    (void)b;
    return 0;
}
short _wrename(const UTF16 *a, const UTF16 *b) {
    (void)a;
    (void)b;
    return 0;
}
int _wmkdir(UTF16 *p) {
    (void)p;
    return 0;
}
int _wrmdir(UTF16 *p) {
    (void)p;
    return 0;
}
int _wfcopy(const UTF16 *a, const UTF16 *b) {
    (void)a;
    (void)b;
    return 0;
}
size_t _fread(void *p, size_t s, size_t n, file_descriptor_t *f) {
    (void)p;
    (void)s;
    (void)n;
    (void)f;
    return 0;
}
int __fseek(file_descriptor_t *f, long o, int w) {
    (void)f;
    (void)o;
    (void)w;
    return -1;
}
long _ftell(file_descriptor_t *f) {
    (void)f;
    return 0;
}
int _fclose(file_descriptor_t *f) {
    (void)f;
    return 0;
}
void *lmalloc(size_t s) { return malloc(s); }
void _lfree(void *p) { free(p); }
size_t GetFreeMemory(void) { return 1 << 20; }
