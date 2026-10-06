/* Host-side stand-in for src/unifont_data.s: the same generated blob as a C array with C
 * linkage (the firmware build gets it from an assembler .incbin). */
#include <stdint.h>

extern "C" const uint8_t fm_font_blob[] = {
#include "unifont_c.inc"
};
