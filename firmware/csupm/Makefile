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
		   resource_perms clkpll clock dvfs psu pmic sky1soc_perf locked_perf \
		   scmi_clock scmi_perf pm_svc dpm
SCP_FWK		:= fwk_arch fwk_core fwk_delayed_resp fwk_dlist fwk_id fwk_interrupt \
		   fwk_io fwk_log fwk_mm fwk_module fwk_notification fwk_ring fwk_slist \
		   fwk_status fwk_string fwk_time assert
# (BUILD_HAS_SCMI_SENSOR_EVENTS: the sensors' trip points, SENSOR_TRIP_POINT_
# NOTIFY and _CONFIG, which CIX's firmware answers; without it scmi_sensor
# also calls a null handler for either message sent without a payload)
SCP_DEFS	:= -DBUILD_HAS_SUB_SYSTEM_MODE -DBUILD_HAS_NOTIFICATION -DBUILD_HAS_SCMI_NOTIFICATIONS \
		   -DBUILD_HAS_SCMI_SENSOR_EVENTS \
		   -DBUILD_HAS_BASE_PROTOCOL -DBUILD_HAS_SCMI_PERF_FAST_CHANNELS \
		   -DBUILD_HAS_SCMI_PERF_PLUGIN_HANDLER -DFWK_LOG_LEVEL=FWK_LOG_LEVEL_CRIT \
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

# FreeRTOS (V11.1.0, submodule): the kernel, our port and configuration
# (rtos/), the tasks (src/rtos.c)
RTOS		:= FreeRTOS-Kernel
RTOS_INC	:= -Irtos -Irtos/port -I$(RTOS)/include
RTOS_SRCS	:= $(RTOS)/tasks.c $(RTOS)/queue.c $(RTOS)/list.c

# CSUPM_THERMAL_IRQ=1: the PVT warning and PROCHOT aggregator sources on
# (src/board/board.c says why it is off by default)
CSUPM_THERMAL_IRQ ?= 0

FW_CFLAGS := $(CFLAGS) -Os -Iinclude -Isrc -Wvla $(SCP_DEFS) $(SCP_INC) $(RTOS_INC) \
	     -DCSUPM_THERMAL_IRQ=$(CSUPM_THERMAL_IRQ)
# upstream code builds without -Wextra -Werror
SCP_CFLAGS := $(filter-out -Wextra -Werror,$(CFLAGS)) -Os -Iinclude -Isrc $(SCP_DEFS) $(SCP_INC) $(RTOS_INC)

# upstream modules kept with a change (scp/module/*/README) build as upstream's
$(O)/scp/module/scmi_clock/%: FW_CFLAGS = $(SCP_CFLAGS)
$(O)/scp/module/dvfs/%: FW_CFLAGS = $(SCP_CFLAGS)
$(O)/scp/module/psu/%: FW_CFLAGS = $(SCP_CFLAGS)
$(O)/scp/module/scmi_perf/%: FW_CFLAGS = $(SCP_CFLAGS)
$(O)/scp/module/transport/%: FW_CFLAGS = $(SCP_CFLAGS)
$(O)/scp/module/scmi_system_power/%: FW_CFLAGS = $(SCP_CFLAGS)
$(O)/scp/module/scmi_power_domain/%: FW_CFLAGS = $(SCP_CFLAGS)

SRCS	:= arch/vectors.S src/main.c src/lcru.c src/uart.c src/i2c.c src/pmic.c src/soc.c \
	   src/mbox.c src/power.c src/pvt.c src/plat.c $(wildcard src/clk/*.c) $(wildcard src/dpm/*.c) \
	   $(wildcard src/perf/*.c) \
	   $(wildcard src/rtt/*.c) $(wildcard src/log/*.c) $(wildcard src/pcc/*.c) \
	   $(wildcard src/hsm/*.c) $(wildcard src/msg/*.c) \
	   $(wildcard src/board/*.c) $(wildcard src/fan/*.c) $(wildcard src/mclp/*.c) $(wildcard src/s3script/*.c) \
	   src/rtos.c rtos/port/port.c rtos/port/portasm.S \
	   lib/string.c lib/malloc.c lib/printf.c lib/libgcc.c $(SCP_SRCS) $(RTOS_SRCS)
OBJS	:= $(patsubst %,$(O)/%.o,$(SRCS)) $(SCP_GEN)/fwk_module_list.c.o

all: $(O)/csupm.xbin

# The version the AP reads (src/log/md_fwver.c): ours in CIX's layout, the
# commit and its date from git; $(O)/fwver.h changes only when they do
CSUPM_VERSION	?= open
FWVER_GIT	:= $(shell git -c safe.directory='*' log -1 --abbrev=12 --format='%h %cd' \
		     --date=format:'%y%m%d %H%M%S' 2>/dev/null)
FWVER_HASH	:= $(or $(word 1,$(FWVER_GIT)),unknown)
FWVER_DATE	:= $(or $(word 2,$(FWVER_GIT)),000000)
FWVER_TIME	:= $(or $(word 3,$(FWVER_GIT)),000000)

$(O)/fwver.h: FORCE
	@mkdir -p $(O)
	@printf '#define %s "%s"\n' CSUPM_VERSION '$(CSUPM_VERSION)' CSUPM_GIT_HASH $(FWVER_HASH) \
		CSUPM_GIT_DATE $(FWVER_DATE) CSUPM_GIT_TIME $(FWVER_TIME) > $@.tmp
	@cmp -s $@.tmp $@ && rm -f $@.tmp || mv $@.tmp $@

$(O)/src/log/md_fwver.c.o: $(O)/fwver.h
$(O)/src/log/md_fwver.c.o: FW_CFLAGS += -I$(O)

$(CORE): core/pm_core_v1.json $(ROOT)/scripts/open/xtensa_core.py
	@mkdir -p $(O)
	@echo "  CORE    $@"
	@python3 $(ROOT)/scripts/open/xtensa_core.py $@ --config core/pm_core_v1.json \
		--overlays $(XTENSA_DIR)/overlays --dynconfig $(XTENSA_DIR)/dynconfig

$(SCP_GEN)/fwk_module_idx.h $(SCP_GEN)/fwk_module_list.c: scp/modules.py Makefile
	@mkdir -p $(SCP_GEN)
	@python3 scp/modules.py $(SCP_GEN) $(SCP_MODULES)

$(O)/%.c.o: %.c $(CORE) $(wildcard include/*.h rtos/*.h rtos/port/*.h scp/include/*.h src/clk/*.h src/dpm/*.h src/rtt/*.h src/log/*.h src/pcc/*.h src/hsm/*.h src/msg/*.h src/mclp/*.h src/s3script/*.h src/board/*.h src/fan/*.h src/perf/*.h scp/module/*/include/*.h scp/module/*/include/*/*.h) $(SCP_GEN)/fwk_module_idx.h Makefile
	@mkdir -p $(dir $@)
	@echo "  CC      $<"
	@$(CC) $(FW_CFLAGS) -c -o $@ $<

$(O)/$(SCP)/%.c.o: $(SCP)/%.c $(CORE) $(SCP_GEN)/fwk_module_idx.h Makefile
	@mkdir -p $(dir $@)
	@echo "  CC      $<"
	@$(CC) $(SCP_CFLAGS) -c -o $@ $<

$(O)/$(RTOS)/%.c.o: $(RTOS)/%.c $(CORE) $(wildcard rtos/*.h rtos/port/*.h) Makefile
	@mkdir -p $(dir $@)
	@echo "  CC      $<"
	@$(CC) $(SCP_CFLAGS) -c -o $@ $<

$(SCP_GEN)/fwk_module_list.c.o: $(SCP_GEN)/fwk_module_list.c $(CORE)
	@echo "  CC      $<"
	@$(CC) $(SCP_CFLAGS) -c -o $@ $<

$(O)/%.S.o: %.S $(CORE) $(wildcard include/*.h rtos/port/*.h) Makefile
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

FORCE:

.PHONY: all check clean FORCE
