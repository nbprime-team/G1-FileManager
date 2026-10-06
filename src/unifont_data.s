/*
 * unifont_data.s -- embeds the generated GNU Unifont blob into .rodata.
 *
 * The blob (magic 'FU01') contains a sparse page table plus the glyph payload; its layout
 * is documented at the top of tools/gen_font.py.  The path comes from
 * build/font-<tier>/unifont_paths.inc, which the Makefile adds with -I so the same source
 * serves every font tier (see FONT_TIER in the Makefile).
 */
    .section .rodata
    .align 4
    .global fm_font_blob
fm_font_blob:
    .include "unifont_paths.inc"
