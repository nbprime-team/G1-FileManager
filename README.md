upsilon version created by deepseek flash.

# HP Prime G1 native file manager 
#### G1 SDK rewrite

A C++ rewrite of the existing GUI file manager (`FileManager.hpappdir/fm.py`, the Python
app in the workspace) on top of the G1 SDK: Upsilon external app, `arm-none-eabi-gcc`,
Project Muteki syscalls.  Layout, colours, key bindings and behaviour follow the Python
original; the user interface is drawn with **GNU Unifont** instead of the firmware font.

```
g1fm/
├── Makefile                 build fm.elf / armfir.elf, font generation, host tests
├── src/
│   ├── fm.cpp               the whole application (filesystem + UI + input)
│   ├── wfcopy.s             _wfcopy (0x10276) syscall stub, missing from the SDK headers
│   └── unifont_data.s       .incbin of the generated Unifont blob
├── tools/
│   ├── gen_font.py          Unifont .hex -> page-table blob + header
│   ├── coverage-fit.py      pick the Unifont ranges that fit the 1 MB app window
│   ├── preview_render.py    render strings as terminal ASCII art (font sanity check)
│   └── run_host_test.sh     build/run the host font tests
├── host/                    SDK shim + unit tests so fm.cpp can be built on a PC
└── build/font-<tier>/       generated font data (rebuilt by `make font`)
```

## Size limit (important)

The firmware's external app loader only accepts an ELF that lives inside
**0x30900000..0x31000000 (1 MiB)** -- `readme.txt`: "The app.elf must be compiled at a free
location inside the memory area end of Upsilon firmware 0x30900000 to 0x31000000".
A larger app makes the calculator reboot the moment it is started (exactly what the first
1.43 MiB build did).  `make` now fails the build if the size crosses the limit.

The UI font is what dominates the size, so it is generated to a byte budget:
`tools/coverage-fit.py` picks the Unifont ranges that fit and `tools/gen_font.py` packs them
into a sparse page table (32 bytes per glyph + ~28 KB of index for 27k glyphs, instead of
the ~170 KB a flat run table would need).

| `FONT_TIER` | glyphs | font blob | app size | Chinese |
|---|---|---|---|---|
| `a` | 95 | 3 KB | 30 KB | no |
| `c` | ~3 000 | 94 KB | 123 KB | no |
| **`f` (default)** | **27 385** | **884 KB** | **932 KB** | **full CJK Unified Ideographs U+4E00-U+9FFF + 1 178 Extension-A glyphs** |

```sh
make FONT_TIER=c                       # small build, useful for isolating loading issues
make FONT_BUDGET_KB=1000               # let the default tier use more of the window
python3 tools/preview_render.py --tier f "项目 ← ─ 1.5K"   # ASCII-art preview of the font
```

## Build

```sh
apt-get install gcc-arm-none-eabi unifont      # toolchain + Unifont .hex source
make                                           # -> fm.elf  (linked at 0x30900000)
make check                                     # host-side font unit tests (no calculator)
make firmware                                  # -> armfir.elf (boot firmware, 0x30600000)
make install APPDSK_DIR=/mnt/appdisk           # copy to <appdisk>/apps/fm.elf
```

`make` needs the SDK next to this directory, i.e. `../g1sdk/HP/g1sdk/` from the extracted
`g1sdk.zip`.  The Unifont source defaults to `/usr/share/unifont/unifont.hex`; override it
with `make UNIFONT_HEX=/path/to/unifont.hex` (or point it at `unifont_jp.hex` for the
Japanese variant).  `tools/gen_font.py` accepts `--coverage FILE` (extra ranges) and
`--extra FILE` (extra single characters) to widen the embedded subset.

Install to the calculator: copy `fm.elf` into the `apps` directory of the Prime's C: drive
(Upsilon: Vars key while connected to a PC), then start it from the External app.

## What it does (parity with the Python file manager)

| Feature | Notes |
|---|---|
| Directory listing | `_wfindfirst`/`_wfindnext` with the same multi-mask merge the Python version uses (0x37, 0xFF, 0x00), directories first, case-insensitive name sort |
| Long file names | LFN (UTF-16) preferred, DOS 8.3 name as fallback; UTF-8 with CJK renders through Unifont |
| Navigation | up/down select (hold to repeat), Enter/right opens, left/u goes up, q/esc quits with confirmation, g path entry (`C:\`, `..`, `C:`), r refresh, touch taps open a row |
| File viewer | 2 KB chunks, text or hex dump (offset / 8 bytes / ASCII), left/right for blocks, up/down for lines, esc returns |
| Info page | name, full path, size (human + bytes), FAT timestamp, attribute flags (D/R/H/S/A), type |
| Delete | recursive for directories, with the "删除中，请稍候..." dialog |
| Copy | **c** enters copy mode, browse to the target directory, **y** confirms (files via `_wfcopy`, directories recursive), **esc** cancels |
| Rename / mkdir | **y** / **m**, with a modal text input dialog |
| Colours | identical palette to fm.py (header `#1E3A8A`, selection `#3B82F6`, footer `#E5E7EB`, ...) |
| Layout | header bar (path + item count / copy banner), list rows with size column, two line footer with the same hint strings |

Differences from the Python version, deliberately:

* Text is drawn with Unifont 16 px bitmaps instead of the firmware's `TEXTSIZE`/`textout_p`
  calls.  Row height is 16 px, so 11 list rows are visible (the Python version showed 11
  with a 16 px row and a 30 px footer, so the information density is unchanged).
* Text input is a drawn modal dialog (the Python version used PPL `input()`, which opens the
  firmware's own prompt).
* Touch events are consumed by a background thread (`GetEvent` blocks), which is what the
  SDK's own event samples do; the main loop polls the resulting state.
* The current directory is tracked by the app (validated once at startup through
  `_wgetcurdir`) instead of being re-read before every refresh.

## UI font: one embedded bitmap table, no firmware text API

Every glyph -- ASCII **and** CJK -- is a 12x12 bitmap in a 12x13 cell, rasterized at build
time from the Prime's own system font (`PrimeSans.ttf`) by `tools/gen_cjk.py` and compiled
into the binary.  Text is drawn exclusively with `rgbSetColor` + `FillRect`, the path the
bring-up probe verified on this firmware.

```
glyph count  161   (printable ASCII + the CJK/graphic set in tools/cjk_chars.txt)
ink data     2.8 KB
whole app    34 KB stripped   (was 934 KB with the Unifont blob)
```

* Advances are uniform: one 12 px cell per character, so `text_width()` counts characters.
* `objdump` on the final ELF shows **zero** calls to `SetFontType`, `GetFontType`,
  `GetFontHeight`, `GetCharWidth`, `WriteString`, `WriteChar` or `WriteAlignString`.
* Two glyphs are hand-drawn in the generator because the vector font renders them badly at
  12 px: the left arrow `U+2190` and the underscore.
* Add characters to `tools/cjk_chars.txt` and run `make font`; each glyph costs 24 bytes plus
  a small index entry.  Missing codepoints draw a placeholder box.

Why not the 900 KB Unifont blob: it was unstable on the device (the boot log always stopped
inside the first glyph lookup), while the same drawing calls with a small in-image table work.
Small tables also keep `.rodata` under 3 KB, so the 1 MB app window is a non-issue.

## Keys: the device keycode table only

`primetcc/rt/hp_input.h` documents the hardware-verified scan codes, and `primeLua/README.md`
records the trap this app originally fell into: the PPL GETKEY numbering used by the Python
original overlaps the device table, so **the right arrow's device code `0x04` is GETKEY's
`esc`** and the left arrow's `0x02` is GETKEY's up.  Matching both tables at once therefore
breaks navigation (pressing right quits, left goes up).

fm.elf now uses only the device codes:

| key | code | key | code |
|---|---|---|---|
| esc | `0x01` | backspace | `0x0C` |
| left | `0x02` | enter | `0x0D` |
| up | `0x03` | space | `0x20` |
| right | `0x04` | ON | `0x83` |
| down | `0x05` | symb/plot/num/view/cas/menu | `0x91/0xB2/0xB3/0xB4/0xB5/0x93` |

Letter keys are the alpha layer of the keypad (the firmware reports the key, e.g. `0x51` for
7/Q, not the letter), so the letter shortcuts match **both** the alpha key and the plain ASCII
code; the raw code of every press is written to `fm.log` (`key code 0xNN`) so the remaining
letters can be pinned down from hardware in one run.

## Not verified on hardware

Built and (font/pure logic) unit tested on the host; the syscall calls themselves need a
real G1 to confirm.  The points to check first on the device:

1. Key table: press esc, arrows, Enter; if the arrows do not navigate, rebuild with
   `-DFM_KEYMAP_GETKEY`. Every list action also has a letter alias, so the app stays usable.
2. `_wfindfirst` attribute masks: exactly the masks the Python version used; if entries go
   missing, add another mask to `Fs::list_dir()`.
3. `_wgetcurdir(NULL, buf)` argument order (the SDK documents `(void *unk, UTF16 *buf)`,
   the Python version passed a disk id first).  If the initial path looks wrong, the app
   falls back to `"C:\\"` and path entry works regardless.
4. `_wfcopy` (0x10276) -- an assembly stub, because the SDK ships the syscall in
   `libsyscalls.a` but never declares it in a header.
5. `SetFontType` is deliberately never called (the readme warns it crashes on the G1); the
   app draws its own bitmap font, so no font syscall is involved.

## Licensing

* GNU Unifont is dual licensed (GPLv2+ with the font embedding exception, and SIL OFL 1.1);
  check the terms of the Unifont distribution before redistributing a binary that embeds
  its glyphs.
* `src/fm.cpp` is a port of the workspace's `fm.py`; carry over whatever licence that file
  has.  `src/besta_fixes.cc`, `src/syscalls.s` and the headers come from the G1 SDK /
  Project Muteki.
