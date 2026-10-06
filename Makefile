# =============================================================================
# fm.elf -- native HP Prime G1 file manager (Upsilon external app)
#
#   make            build fm.elf        (external app, linked at 0x30900000)
#   make firmware   build armfir.elf    (boot firmware variant, 0x30600000)
#   make clean      remove build output
#   make install    copy fm.elf into $(APPDSK_DIR)/apps
#
# Requires the arm-none-eabi toolchain and the G1 SDK next to this directory
# (../g1sdk == the HP/g1sdk tree unzipped from g1sdk.zip).
# =============================================================================

.DEFAULT_GOAL := all

CROSS    := arm-none-eabi-
CC       := $(CROSS)gcc
CXX      := $(CROSS)g++
AS       := $(CROSS)gcc
LD       := $(CROSS)g++
OBJCOPY  := $(CROSS)objcopy
SIZE     := $(CROSS)size

G1SDK     := ../g1sdk/HP/g1sdk/
BUILD_DIR := obj
# UI font: a compact GNU Unifont subset compiled into the binary (no firmware font API).
#   build/font-uf/font_uf.h  ASCII + non GB2312 UI glyphs (small table)
#   build/font-gb/font_gb.h  full GB2312: 7445 glyphs + the byte pair -> Unicode map
#   src/font5x7.inc            static ASCII 5x7 table, verified on the device
PYTHON    ?= python3
FONT_DIR  := build/font-uf
GB_DIR    := build/font-gb
CJK_CHARS ?= tools/cjk_chars.txt
UNIFONT_HEX ?= /usr/share/unifont/unifont.hex
UNIFONT_SAMPLE ?= /usr/share/unifont/unifont_sample.hex
UNIFONT_JP ?= /usr/share/unifont/unifont_jp.hex
FONT_DATA := $(FONT_DIR)/font_uf.h $(GB_DIR)/font_gb.h
EN_DIR    := build/font-en
EN_FONT   := $(EN_DIR)/font_uf.h $(EN_DIR)/font_gb.h

# --- CPU: Samsung S3C2416 / ARM926EJ-S (ARMv5TEJ, soft float, little endian) ---
CPU_FLAGS := -mcpu=arm926ej-s -marm -mlittle-endian -mfloat-abi=soft -mthumb-interwork -mno-unaligned-access
OPT_FLAGS := -Os -fno-builtin-sprintf -fno-delete-null-pointer-checks
DEFINES   := -DHPG1 -DMUTEKI_HAS_PRIME_UI_EVENT
# bring-up diagnostics: boot trace to C:\APPS\fm.log + step/breadcrumb lines + raw keycode
# readout.  Turn them off for a quiet build with:  make DEFINES_EXTRA="-UFM_DIAG"
ifneq ($(filter -UFM_DIAG,$(DEFINES_EXTRA)),)
else
DEFINES   += -DFM_BOOT_TRACE -DFM_DRAW_TRACE -DFM_KEY_DEBUG
endif
DEFINES   += $(DEFINES_EXTRA)
# -DFM_NO_CJK drops the CJK glyph table (ASCII-only build)
# -DFM_NO_LOG  drops the log file entirely (trace goes to the screen)
INCLUDES  := -I$(G1SDK)/include -I. -I$(FONT_DIR) -I$(GB_DIR) -Isrc -Ilang
COMMON_FLAGS := $(CPU_FLAGS) $(OPT_FLAGS) $(DEFINES) $(INCLUDES) \
    -ffunction-sections -fdata-sections -fshort-wchar -fno-short-enums -Wall \
    -Wno-narrowing -fno-tree-loop-distribute-patterns \
    -fno-isolate-erroneous-paths-dereference -fomit-frame-pointer -pipe
CXXFLAGS := -include $(G1SDK)/include/besta_fixes.h $(COMMON_FLAGS) \
    -std=gnu++11 -fno-exceptions -fno-rtti -fpermissive -Wno-write-strings

ASFLAGS  := $(CPU_FLAGS) -x assembler-with-cpp -I$(FONT_DIR)

# --- linker -----------------------------------------------------------------
LDFLAGS_APP := -L. \
    -T $(G1SDK)/lib/g1app.ld \
    -Wl,--gc-sections \
    -Wl,-Map=$(BUILD_DIR)/fm.map \
    -Wl,--entry=AppMain \
    -nostartfiles \
    -Wl,--no-wchar-size-warning -Wl,--no-enum-size-warning \
    -Wl,--allow-multiple-definition -Wl,-n

LDFLAGS_FW := $(subst g1app.ld,hpg1.ld,$(LDFLAGS_APP))

LDLIBS := --specs=nano.specs -L$(G1SDK)/lib/ -lsyscalls -lgcc

ALL_CXX_SRCS := src/fm.cpp $(G1SDK)/src/besta_fixes.cc
ALL_S_SRCS   := src/wfcopy.s

# English build objects live in obj/en/ so both languages can be built in one tree.
EN_CXX_SRCS := src/fm.cpp $(G1SDK)/src/besta_fixes.cc
EN_OBJS := $(BUILD_DIR)/en/src/fm.o $(BUILD_DIR)/en/$(G1SDK)src/besta_fixes.o \
           $(BUILD_DIR)/en/src/wfcopy.o

# ---------------------------------------------------------------------------
# generic compile rules
# ---------------------------------------------------------------------------
$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.s
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/en/src/fm.o: src/fm.cpp lang/ui_en.h $(EN_FONT)
	@mkdir -p $(dir $@)
	$(CXX) -I$(EN_DIR) $(CXXFLAGS) -DFM_LANG_EN -o $@ -c $<

$(BUILD_DIR)/en/src/wfcopy.o: src/wfcopy.s
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -o $@ -c $<

$(BUILD_DIR)/en/$(G1SDK)src/besta_fixes.o: $(G1SDK)/src/besta_fixes.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ -c $<

CXX_OBJS := $(addprefix $(BUILD_DIR)/,$(addsuffix .o,$(basename $(ALL_CXX_SRCS))))
S_OBJS   := $(addprefix $(BUILD_DIR)/,$(addsuffix .o,$(basename $(ALL_S_SRCS))))
ALL_OBJS := $(CXX_OBJS) $(S_OBJS)

.PHONY: all en firmware min probe clean size install font check

all: fm.elf fmen.elf

fmen.elf: $(EN_OBJS)
	@mkdir -p $(BUILD_DIR)
	$(LD) $(LDFLAGS_APP) -o $@ $^ $(LDLIBS)
	$(SIZE) $@
	$(OBJCOPY) -j ER_RO -j ER_RW -j ER_ZI $@ clean_fmen.elf
	$(CROSS)strip -s clean_fmen.elf -o fmen_stripped.elf
	@sz=$$($(SIZE) $@ | awk 'NR==2{print $$1+$$2}'); echo "app size: $$sz / 1048576 bytes"

# GNU Unifont subset (43k glyphs: ASCII/Latin + all CJK Unified Ideographs + symbols).
font: $(FONT_DATA) $(EN_FONT)

$(EN_FONT): tools/gen_gb2312.py $(CJK_CHARS)
	$(PYTHON) tools/gen_gb2312.py --no-han --outdir $(EN_DIR) --ui-outdir $(EN_DIR)

$(FONT_DATA): tools/gen_gb2312.py $(CJK_CHARS)
	$(PYTHON) tools/gen_gb2312.py --hex $(UNIFONT_JP) --ascii-hex $(UNIFONT_HEX) \
	    --chars $(CJK_CHARS) --outdir $(GB_DIR)

build/font/%.o: build/font/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/src/fm.o: $(FONT_DATA) src/font5x7.inc

fm.elf: $(ALL_OBJS)
	@mkdir -p $(BUILD_DIR)
	$(LD) $(LDFLAGS_APP) -o $@ $^ $(LDLIBS)
	@sz=$$($(SIZE) $@ | awk 'NR==2{print $$1+$$2}'); \
	if [ $$sz -gt 1048576 ]; then \
	  echo "ERROR: fm.elf is $$sz bytes; the app window is 1048576 bytes (0x30900000-0x31000000)."; \
	  echo "       The font table is compiled in; check for an accidentally large table."; exit 1; \
	fi; \
	echo "app size: $$sz / 1048576 bytes ($$((1048576 - sz)) bytes free)"
	$(SIZE) $@
	$(OBJCOPY) -j ER_RO -j ER_RW -j ER_ZI $@ clean_fm.elf
	$(CROSS)strip -s clean_fm.elf -o fm_stripped.elf
	@echo "--- sections ---"

# ---------------------------------------------------------------------------
# host side checks and packaging
# ---------------------------------------------------------------------------
check: font
	sh tools/run_host_test.sh $(FONT_DIR)

text: fm_text.elf

clean:
	rm -rf $(BUILD_DIR) fm.elf fmen.elf fm_probe.elf fm_text.elf armfir.elf \
	       clean_fm.elf clean_fmen.elf clean_armfir.elf fm_stripped.elf fmen_stripped.elf

distclean: clean
	rm -rf build/font-uf build/font-gb build/font-en build/font build/font-cjk build/host

size: fm.elf fmen.elf
	$(SIZE) --format=berkeley fm.elf fmen.elf

# Copy an app onto a mounted App Disk (adapt APPDSK_DIR to your mount point).
APPDSK_DIR ?= /mnt/appdisk
install: fm.elf
	@test -d "$(APPDSK_DIR)" || { echo "APPDSK_DIR=$(APPDSK_DIR) not mounted"; exit 1; }
	mkdir -p $(APPDSK_DIR)/apps
	cp -f fm.elf $(APPDSK_DIR)/apps/fm.elf
	cp -f fmen.elf $(APPDSK_DIR)/apps/fmen.elf
	@echo "copied fm.elf and fmen.elf -> $(APPDSK_DIR)/apps"
