# SPDX-License-Identifier: BSD-2-Clause-Patent
# CSU_PM firmware for CIX Sky1 (Xtensa LX7 "pm_core_v1"), work in progress.
#   make [O=outdir]            $(O)/csupm.elf and $(O)/csupm.xbin (bootloader1 image "pm")
#   make check [O=outdir]      check that the image and a test file use only
#                              instructions the core has
# Needs the build container: Espressif's xtensa-esp-elf GCC and the overlay
# sources under /opt/xtensa (docker/Dockerfile).

O		?= build
XTENSA_DIR	?= /opt/xtensa
CROSS_COMPILE	?= xtensa-esp-elf-
CORE		:= $(O)/pm_core_v1.so
ROOT		:= $(abspath ../..)
XT		:= XTENSA_GNU_CONFIG=$(abspath $(CORE))
CC		:= $(XT) $(CROSS_COMPILE)gcc
OBJDUMP		:= $(CROSS_COMPILE)objdump

CFLAGS	:= -mlongcalls -ffp-contract=off -g -std=gnu11 -ffreestanding -fno-builtin \
	   -ffunction-sections -fdata-sections -Wall -Wextra -Werror
FW_CFLAGS := $(CFLAGS) -Os -Iinclude -Wvla
LDFLAGS	:= -nostdlib -T pm.ld -Wl,--gc-sections -Wl,-Map=$(O)/csupm.map

SRCS	:= arch/vectors.S src/main.c lib/string.c
OBJS	:= $(patsubst %,$(O)/%.o,$(SRCS))

all: $(O)/csupm.xbin

$(CORE): core/pm_core_v1.json $(ROOT)/scripts/open/xtensa_core.py
	@mkdir -p $(O)
	@echo "  CORE    $@"
	@python3 $(ROOT)/scripts/open/xtensa_core.py $@ --config core/pm_core_v1.json \
		--overlays $(XTENSA_DIR)/overlays --dynconfig $(XTENSA_DIR)/dynconfig

$(O)/%.c.o: %.c $(CORE) $(wildcard include/*.h)
	@mkdir -p $(dir $@)
	@echo "  CC      $<"
	@$(CC) $(FW_CFLAGS) -c -o $@ $<

$(O)/%.S.o: %.S $(CORE)
	@mkdir -p $(dir $@)
	@echo "  AS      $<"
	@$(CC) $(FW_CFLAGS) -c -o $@ $<

$(O)/csupm.elf: $(OBJS) pm.ld
	@echo "  LD      $@"
	@$(CC) $(FW_CFLAGS) $(LDFLAGS) -o $@ $(OBJS) -lgcc

$(O)/csupm.xbin: $(O)/csupm.elf $(ROOT)/scripts/open/xbin.py
	@echo "  XBIN    $@"
	@python3 $(ROOT)/scripts/open/xbin.py $< $@

CHECK_OBJS := $(foreach o,O0 O2 Os,$(O)/test/isa-$(o).o)

$(O)/test/isa-%.o: test/isa.c $(CORE)
	@mkdir -p $(dir $@)
	@echo "  CC      $< (-$*)"
	@$(CC) $(CFLAGS) -$* -c -o $@ $<

check: $(CHECK_OBJS) $(O)/csupm.elf
	@$(XT) OBJDUMP=$(OBJDUMP) python3 $(ROOT)/scripts/open/xtensa_isa.py check $^

clean:
	rm -rf $(O)

.PHONY: all check clean
