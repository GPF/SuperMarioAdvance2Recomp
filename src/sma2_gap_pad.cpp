// Opt-in insertion-only I-cache layout control (SMA2_B16_GAP). Contains NO code and is never
// executed or referenced: a block of zero bytes in .text, 32-byte aligned and a multiple of 32
// bytes, linked (as a non-LTO object) ahead of the LTO partitions so every following byte of
// .text shifts by exactly SMA2_GAP_BYTES. Kept alive only by a linker GC root.
#ifndef SMA2_GAP_BYTES
#define SMA2_GAP_BYTES 13664
#endif
#define SMA2_GAP_STR2(x) #x
#define SMA2_GAP_STR(x) SMA2_GAP_STR2(x)
__asm__(
    ".section .text.sma2_gap,\"ax\"\n"
    ".balign 32\n"
    ".global _sma2_gap_pad\n"
    "_sma2_gap_pad:\n"
    ".fill " SMA2_GAP_STR(SMA2_GAP_BYTES) ",1,0\n"
    ".previous\n");
