/*
 * _wfcopy.s -- Besta syscall 0x10276 (wide-char recursive file copy).
 *
 * The SDK ships a stub for this syscall inside libsyscalls.a but never declares it in
 * its headers, so fm.cpp declares `extern "C" int _wfcopy(const UTF16 *, const UTF16 *)`
 * and this object provides the entry point.  The sequence is the one used by every other
 * stub in the SDK's syscalls.s (push {r0} / push {lr} / svc N) or the OS will not find
 * the syscall number.
 */
    .cpu arm7tdmi
    .section .text
    .arm
    .global _wfcopy
    .type   _wfcopy, %function
_wfcopy:
    push    {r0}
    push    {lr}
    svc     0x10276
    .size   _wfcopy, . - _wfcopy
