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
LDFLAGS	:= -nostdlib -T pm.ld -Wl,--gc-sections -Wl,-Map=$(O)/csupm.map

# SCP-firmware (v2.11.0, submodule): the framework, the modules in
# SCP_MODULES (upstream ones from SCP-firmware/module, ours from
# scp/module) and their configurations in scp/config
SCP		:= SCP-firmware
SCP_GEN		:= $(O)/scp/gen
SCP_MODULES	:= xt_timer timer cix_mbox transport scmi rcsu power_domain scmi_power_domain \
		   scmi_system_power soc_sensor sensor scmi_sensor \
		   resource_perms clkpll clock scmi_clock
SCP_FWK		:= fwk_arch fwk_core fwk_delayed_resp fwk_dlist fwk_id fwk_interrupt \
		   fwk_io fwk_log fwk_mm fwk_module fwk_notification fwk_ring fwk_slist \
		   fwk_status fwk_string fwk_time assert
SCP_DEFS	:= -DBUILD_HAS_SUB_SYSTEM_MODE -DBUILD_HAS_NOTIFICATION -DBUILD_HAS_SCMI_NOTIFICATIONS \
		   -DBUILD_HAS_BASE_PROTOCOL -DFWK_LOG_LEVEL=FWK_LOG_LEVEL_CRIT \
		   '-DBUILD_VERSION_DESCRIBE_STRING="SCP-firmware v2.11.0"' \
		   -DBUILD_VERSION_MAJOR=2 -DBUILD_VERSION_MINOR=11 -DBUILD_VERSION_PATCH=0 \
		   $(foreach m,$(SCP_MODULES),-DBUILD_HAS_MOD_$(shell echo $(m) | tr a-z A-Z)=1)
mod_dir		= $(if $(wildcard scp/module/$(1)),scp/module/$(1),$(SCP)/module/$(1))
# modules whose headers others include though they are not built (yet)
SCP_HEADERS	:=
SCP_INC		:= -I$(SCP_GEN) -Iscp/include -I$(SCP)/framework/include \
		   $(foreach m,$(SCP_MODULES) $(SCP_HEADERS),-I$(call mod_dir,$(m))/include)
SCP_SRCS	:= $(patsubst %,$(SCP)/framework/src/%.c,$(SCP_FWK)) \
		   $(foreach m,$(SCP_MODULES),$(wildcard $(call mod_dir,$(m))/src/*.c)) \
		   $(wildcard scp/src/*.c) $(wildcard $(patsubst %,scp/config/config_%.c,$(SCP_MODULES)))

FW_CFLAGS := $(CFLAGS) -Os -Iinclude -Isrc -Wvla $(SCP_DEFS) $(SCP_INC)
# upstream code builds without -Wextra -Werror
SCP_CFLAGS := $(filter-out -Wextra -Werror,$(CFLAGS)) -Os -Iinclude -Isrc $(SCP_DEFS) $(SCP_INC)

# upstream modules kept with a change (scp/module/*/README) build as upstream's
$(O)/scp/module/scmi_clock/%: FW_CFLAGS = $(SCP_CFLAGS)

SRCS	:= arch/vectors.S src/main.c src/lcru.c src/uart.c src/i2c.c src/pmic.c src/soc.c \
	   src/pmu.c src/mbox.c src/power.c src/pvt.c src/plat.c $(wildcard src/clk/*.c) lib/string.c lib/malloc.c lib/printf.c lib/libgcc.c $(SCP_SRCS)
OBJS	:= $(patsubst %,$(O)/%.o,$(SRCS)) $(SCP_GEN)/fwk_module_list.c.o

all: $(O)/csupm.xbin

$(CORE): core/pm_core_v1.json $(ROOT)/scripts/open/xtensa_core.py
	@mkdir -p $(O)
	@echo "  CORE    $@"
	@python3 $(ROOT)/scripts/open/xtensa_core.py $@ --config core/pm_core_v1.json \
		--overlays $(XTENSA_DIR)/overlays --dynconfig $(XTENSA_DIR)/dynconfig

$(SCP_GEN)/fwk_module_idx.h $(SCP_GEN)/fwk_module_list.c: scp/modules.py Makefile
	@mkdir -p $(SCP_GEN)
	@python3 scp/modules.py $(SCP_GEN) $(SCP_MODULES)

$(O)/%.c.o: %.c $(CORE) $(wildcard include/*.h scp/include/*.h src/clk/*.h scp/module/*/include/*.h scp/module/*/include/*/*.h) $(SCP_GEN)/fwk_module_idx.h Makefile
	@mkdir -p $(dir $@)
	@echo "  CC      $<"
	@$(CC) $(FW_CFLAGS) -c -o $@ $<

$(O)/$(SCP)/%.c.o: $(SCP)/%.c $(CORE) $(SCP_GEN)/fwk_module_idx.h Makefile
	@mkdir -p $(dir $@)
	@echo "  CC      $<"
	@$(CC) $(SCP_CFLAGS) -c -o $@ $<

$(SCP_GEN)/fwk_module_list.c.o: $(SCP_GEN)/fwk_module_list.c $(CORE)
	@echo "  CC      $<"
	@$(CC) $(SCP_CFLAGS) -c -o $@ $<

$(O)/%.S.o: %.S $(CORE) $(wildcard include/*.h) Makefile
	@mkdir -p $(dir $@)
	@echo "  AS      $<"
	@$(CC) $(FW_CFLAGS) -c -o $@ $<

$(O)/csupm.elf: $(OBJS) pm.ld
	@echo "  LD      $@"
	@$(CC) $(FW_CFLAGS) $(LDFLAGS) -o $@ $(OBJS)

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
