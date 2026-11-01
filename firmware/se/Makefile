# SPDX-License-Identifier: BSD-2-Clause-Patent
# Open SE firmware for CIX Sky1 (replaces bootloader1 image "se").
#   make [CIX_ELF=path/to/cix/se_fw.elf | DDR_ISLAND=path/to/ddr_island.S] [O=outdir]
#        [CROSS_COMPILE=arm-none-eabi-] [MBEDTLS_DIR=../../mbedtls] [SE_FW_VERSION=beta2.0-<id>]
# The DDR driver is ported to ddr/*.c a function at a time.  What is not
# ported yet comes from CIX's debug build: with CIX_ELF, the DDR island
# (scripts/open/se_ddr_island.py) is generated from it without every
# function and variable the ddr/*.c objects define ($(O)/ddr/island.list:
# what is left).  DDR_ISLAND links a pre-generated island as it is.

CROSS_COMPILE	?= arm-none-eabi-
MBEDTLS_DIR	?= ../../mbedtls
O		?= build
CC		:= $(CROSS_COMPILE)gcc
OBJCOPY		:= $(CROSS_COMPILE)objcopy

CFLAGS	:= -mcpu=cortex-m33 -mthumb -mfloat-abi=soft -Os -g -std=gnu11 \
	   -ffreestanding -fno-builtin -ffunction-sections -fdata-sections \
	   -fno-strict-aliasing -Wall -Wextra -Werror -Wno-unused-parameter \
	   -Wno-missing-field-initializers -Iinclude -I$(MBEDTLS_DIR)/include -Ilibrary/crypto \
	   -DMBEDTLS_CONFIG_FILE='"mbedtls_config.h"'
LDFLAGS	:= -nostdlib -nostartfiles -Wl,--gc-sections -Wl,--no-warn-rwx-segments \
	   -T se.ld -Wl,-Map=$(O)/se_fw.map

SRCS	:= $(sort $(wildcard arch/*.c driver/*.c hal/*.c hal/*/*.c framework/*.c \
	   service/*.c platform/sky1/*.c library/libc/*.c library/crypto/*.c))
MBEDTLS_SRCS := sha256 sha512 md hkdf rsa rsa_internal bignum oid asn1parse asn1write \
	   platform platform_util constant_time ecp ecp_curves
DDR_SRCS := $(sort $(wildcard ddr/*.c))
DDR_OBJS := $(DDR_SRCS:%.c=$(O)/%.o)
OBJS	:= $(SRCS:%.c=$(O)/%.o) $(DDR_OBJS) $(MBEDTLS_SRCS:%=$(O)/mbedtls/%.o)
PYTHON	?= python3
ISLAND_PY := ../../scripts/open/se_ddr_island.py
ifneq ($(CIX_ELF),)
DDR_ISLAND := $(O)/ddr/ddr_island.S
OBJS	+= $(O)/ddr/ddr_island.o
else ifneq ($(DDR_ISLAND),)
OBJS	+= $(O)/ddr/ddr_island.o
endif
ifneq ($(SE_FW_VERSION),)
CFLAGS	+= -DSE_FW_VERSION='"$(SE_FW_VERSION)"'
endif
HDRS	:= $(wildcard include/*.h ddr/*.h)

all: $(O)/se_fw.bin

$(O)/%.o: %.c $(HDRS)
	@mkdir -p $(dir $@)
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) -c -o $@ $<

# Mbed TLS, built without our warning policy
$(O)/mbedtls/%.o: $(MBEDTLS_DIR)/library/%.c library/crypto/mbedtls_config.h
	@mkdir -p $(dir $@)
	@echo "  CC      mbedtls/$*.c"
	@$(CC) $(filter-out -Werror -Wall -Wextra,$(CFLAGS)) -w -c -o $@ $<

ifneq ($(CIX_ELF),)
# the island depends on which ddr/*.c there are, not only on their objects
DDR_LIST := $(O)/ddr/objs.list
ifneq ($(file < $(DDR_LIST)),$(DDR_OBJS))
$(shell mkdir -p $(O)/ddr)
$(file > $(DDR_LIST),$(DDR_OBJS))
endif
$(O)/ddr/ddr_island.S: $(CIX_ELF) $(DDR_OBJS) $(DDR_LIST) $(ISLAND_PY)
	@mkdir -p $(dir $@)
	@echo "  ISLAND  $@"
	@$(PYTHON) $(ISLAND_PY) $(CIX_ELF) $@ --skip-obj $(DDR_OBJS) \
		--list $(O)/ddr/island.list --report > $(O)/ddr/island.txt
	@head -2 $(O)/ddr/island.txt | tail -1
endif

$(O)/ddr/ddr_island.o: $(DDR_ISLAND)
	@mkdir -p $(dir $@)
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) -c -o $@ $<

ifeq ($(DDR_ISLAND),)
# without CIX's code the ported files must define the whole DDR driver
LINK_FAIL = { echo "error: link failed; without CIX_ELF (or DDR_ISLAND) firmware/se/ddr must define every" \
		"function of CIX's DDR driver: build with CIX_ELF=path/to/cix/se_fw.elf while the port" \
		"is incomplete (the undefined references above are what is missing)" >&2; exit 1; }
else
LINK_FAIL = exit 1
endif

$(O)/se_fw.elf: $(OBJS) se.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) -lgcc || $(LINK_FAIL)

$(O)/se_fw.bin: $(O)/se_fw.elf
	$(OBJCOPY) -O binary $< $@

objs: $(OBJS)

clean:
	rm -rf $(O)

.PHONY: all objs clean
