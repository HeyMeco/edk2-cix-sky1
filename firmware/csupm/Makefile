# SPDX-License-Identifier: BSD-2-Clause-Patent
# CSU_PM firmware for CIX Sky1 (Xtensa LX7 "pm_core_v1"), work in progress.
#   make check [O=outdir]      build the core description and check the toolchain
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

all: check

$(CORE): core/pm_core_v1.json $(ROOT)/scripts/open/xtensa_core.py
	@mkdir -p $(O)
	@echo "  CORE    $@"
	@python3 $(ROOT)/scripts/open/xtensa_core.py $@ --config core/pm_core_v1.json \
		--overlays $(XTENSA_DIR)/overlays --dynconfig $(XTENSA_DIR)/dynconfig

CHECK_OBJS := $(foreach o,O0 O2 Os,$(O)/test/isa-$(o).o)

$(O)/test/isa-%.o: test/isa.c $(CORE)
	@mkdir -p $(dir $@)
	@echo "  CC      $< (-$*)"
	@$(CC) $(CFLAGS) -$* -c -o $@ $<

check: $(CHECK_OBJS)
	@$(XT) OBJDUMP=$(OBJDUMP) python3 $(ROOT)/scripts/open/xtensa_isa.py check $^

clean:
	rm -rf $(O)

.PHONY: all check clean
