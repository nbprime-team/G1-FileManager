/* Host shim for the SDK's besta_fixes.h (only the pieces fm.cpp uses). */
#ifndef BESTA_FIXES_SHIM
#define BESTA_FIXES_SHIM
#define DMB __asm__ __volatile__("" : : : "memory")
#endif
