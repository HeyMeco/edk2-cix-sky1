# SPDX-License-Identifier: BSD-2-Clause-Patent
# Open SE firmware for CIX Sky1 (replaces bootloader1 image "se").
#   make DDR_ISLAND=path/to/ddr_island.S [O=outdir] [CROSS_COMPILE=arm-none-eabi-]
#        [MBEDTLS_DIR=../../mbedtls] [SE_FW_VERSION=beta2.0-<id>]
# The DDR island (CIX's DDR driver) comes from scripts/open/se_ddr_island.py.

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
OBJS	:= $(SRCS:%.c=$(O)/%.o) $(MBEDTLS_SRCS:%=$(O)/mbedtls/%.o)
ifeq ($(DDR_ISLAND),)
$(error DDR_ISLAND is not set: generate it with scripts/open/se_ddr_island.py)
endif
OBJS	+= $(O)/ddr/ddr_island.o
ifneq ($(SE_FW_VERSION),)
CFLAGS	+= -DSE_FW_VERSION='"$(SE_FW_VERSION)"'
endif
HDRS	:= $(wildcard include/*.h)

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

$(O)/ddr/ddr_island.o: $(DDR_ISLAND)
	@mkdir -p $(dir $@)
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) -c -o $@ $<

$(O)/se_fw.elf: $(OBJS) se.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) -lgcc

$(O)/se_fw.bin: $(O)/se_fw.elf
	$(OBJCOPY) -O binary $< $@

objs: $(OBJS)

clean:
	rm -rf $(O)

.PHONY: all objs clean
