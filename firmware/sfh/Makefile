# SPDX-License-Identifier: BSD-2-Clause-Patent
# Open sensor fusion hub firmware for CIX Sky1 (replaces sfh_fw.bin).
#   make [O=outdir] [CROSS_COMPILE=arm-none-eabi-] [LOG_UART=0x...]

CROSS_COMPILE	?= arm-none-eabi-
O		?= build
CC		:= $(CROSS_COMPILE)gcc
OBJCOPY		:= $(CROSS_COMPILE)objcopy

CFLAGS	:= -march=armv7-m -mthumb -mfloat-abi=soft -Os -g -std=gnu11 \
	   -ffreestanding -fno-builtin -ffunction-sections -fdata-sections \
	   -Wall -Wextra -Werror -Wno-unused-parameter
ifneq ($(LOG_UART),)
CFLAGS	+= -DSFH_LOG_UART=$(LOG_UART)
endif
LDFLAGS	:= -nostdlib -nostartfiles -Wl,--gc-sections -T sfh.ld -Wl,-Map=$(O)/sfh_fw.map

SRCS	:= start.c main.c irq.c timer.c mbox.c log.c
OBJS	:= $(SRCS:%.c=$(O)/%.o)

all: $(O)/sfh_fw.bin

$(O)/%.o: %.c sfh.h | $(O)
	$(CC) $(CFLAGS) -c -o $@ $<

$(O)/sfh_fw.elf: $(OBJS) sfh.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) -lgcc

$(O)/sfh_fw.bin: $(O)/sfh_fw.elf
	$(OBJCOPY) -O binary $< $@

$(O):
	mkdir -p $@

clean:
	rm -rf $(O)

.PHONY: all clean
