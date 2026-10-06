/* Host build shim: provides just enough of the G1 SDK for host_test.cpp (see run_host_test.sh).
 * The declarations mirror the real headers in ../g1sdk/HP/g1sdk/include. */
#ifndef HOST_SHIM_MUTEKI_H
#define HOST_SHIM_MUTEKI_H

#include <stdint.h>
#include <stddef.h>

typedef uint16_t UTF16;

typedef struct file_descriptor_s file_descriptor_t;

typedef struct {
    void *unk0;
    void *unk4;
    UTF16 *filename_lfn;
    char *filename;
    char *filename2_alt;
    size_t size;
    unsigned int mtime;
    unsigned int btime;
    unsigned int atime;
    unsigned char attrib_mask;
    unsigned char attrib;
} find_context_t;

struct thread_s;
typedef struct thread_s thread_t;

enum font_type_e { SANS_TINY_CJK_NORMAL = 0 };

/* The Prime event structure is 124 bytes; fm.cpp reads the fields at offsets 28/32/36/40
 * (event type, key/touch payload). */
struct ui_event_prime_s {
    void *recipient;
    int event_type;
    unsigned int w[28];
};
typedef struct ui_event_prime_s ui_event_t;

/* --- drawing --- */
void FillRect(short x0, short y0, short x1, short y1, unsigned int flags);
int rgbSetColor(int color);
int rgbSetBkColor(int color);
void ClearScreen(bool fill_with_fg);
short WriteString(short x, short y, const void *s, unsigned int flags);
short WriteChar(short x, short y, UTF16 c, unsigned int flags);
short SetFontType(short font_type);
short GetFontType(void);
int GetFontHeight(uint8_t font_type);
short GetCharWidth(UTF16 c, uint8_t font_type);
extern "C" void LCDOn(void);
extern "C" void LCDOff(void);
extern "C" unsigned long SetSystemVariable(unsigned short type, unsigned short mode, unsigned long value);

/* --- events / threads --- */
bool GetEvent(ui_event_t *event);
void ClearAllEvents(void);
thread_t *OSCreateThread(int (*func)(void *), void *user_data, size_t stack_size, bool defer_start);
int OSTerminateThread(thread_t *thr, int exit_code);
void OSSleep(short millis);

/* --- filesystem --- */
short _wfindfirst(const UTF16 *fnmatch, find_context_t *ctx, int attrib_mask);
short _afindfirst(const char *fnmatch, find_context_t *ctx, int attrib_mask);
short _afindnext(find_context_t *ctx);
short _wfindnext(find_context_t *ctx);
int _findclose(find_context_t *ctx);
short _wgetcurdir(void *unk, UTF16 *buf);
short _wchdir(const UTF16 *path);
short _achdir(const char *path);
short _wfgetattr(UTF16 *path);
bool __wremove(const UTF16 *pathname);
bool _aremove(const char *pathname);
int _amkdir(char *path);
int _armdir(char *path);
short _arename(const char *old_path, const char *new_path);
short _wrename(const UTF16 *old_path, const UTF16 *new_path);
int _wmkdir(UTF16 *path);
int _wrmdir(UTF16 *path);
extern "C" int _wfcopy(const UTF16 *src, const UTF16 *dst);
file_descriptor_t *__wfopen(const UTF16 *pathname, const UTF16 *mode);
size_t _fread(void *ptr, size_t size, size_t nmemb, file_descriptor_t *stream);
size_t _fwrite(const void *ptr, size_t size, size_t nmemb, file_descriptor_t *stream);
int __fseek(file_descriptor_t *stream, long offset, int whence);
long _ftell(file_descriptor_t *stream);
int _fclose(file_descriptor_t *stream);
extern "C" int __fflush(file_descriptor_t *stream);

/* --- memory --- */
void *malloc(size_t size);
void free(void *ptr);
void *lmalloc(size_t size);
void _lfree(void *ptr);
size_t GetFreeMemory(void);

#endif
