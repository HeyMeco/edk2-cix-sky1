/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Boot performance records and boot-time configuration
 * (CIX framework/fw_boot_perf.c, fw_boot_time_config.c).
 */
#include "se.h"
#include "btcfg.h"
#include "bootperf.h"
#include "ci700.h"

/* Items the debug monitor can set; "ap.coreid" is stored 1-based (0: default) */
btcfg_item_t mntr_boot_cfg_item_table[5] = {
	{ "ap.coreid" }, { "se.pblbde" }, { "pb.tfabde" }, { "se.memrbp" },
};

static btcfg_item_t *btcfg_find(const char *table, const char *item)
{
	btcfg_item_t *e = (btcfg_item_t *)table;

	for (; strlen(e->name); e++)
		if (strcasecmp(e->name, item) == 0)
			return e;
	return NULL;
}

uint32_t btcfg_set_value(const char *table, const char *item, uint32_t value)
{
	btcfg_item_t *e = btcfg_find(table, item);

	if (e == NULL) {
		SE_INFO("Wrong item!\n");
		return 0x9101;
	}
	if (strcasecmp(item, "ap.coreid") == 0) {
		if (value > 11)
			return 0x9101;
		value++;
	}
	e->value = value;
	SE_INFO("Item set success!\n");
	return 0;
}

uint32_t btcfg_get_value(const char *table, const char *item, uint32_t *value)
{
	btcfg_item_t *e = btcfg_find(table, item);

	if (e == NULL) {
		SE_INFO("Wrong item!\n");
		return 0x9101;
	}
	*value = e->value;
	if (strcasecmp(item, "ap.coreid") != 0)
		SE_INFO("%s:0x%x\n", item, (unsigned)*value);
	else if (*value == 0)
		SE_INFO("%s:default\n", item);
	else
		SE_INFO("%s:%d\n", item, (int)*value - 1);
	return 0;
}

/* Start and end of each boot phase in ms, for the AP to report */
typedef struct {
	uint32_t start_ms, start_hi;
	uint32_t end_ms, end_hi;
	char name[16];
} boot_perf_t;

static boot_perf_t g_boot_perf_base[2];

#define BOOT_PERF_DRAM		0x63e04000u	/* SoC 0x83e04000, through the SYSHUB window */

static const char *const phase_name[] = {
	[BROM_PHASE] = "BOOTROM", [SE_PHASE] = "CSU_SE", [PM_PHASE] = "CSU_PM", [PBL_PHASE] = "PBL",
	[TFA_PHASE] = "TFA", [TEE_PHASE] = "TEE", [BLOADER_PHASE] = "BLOADER", [GRUB_PHASE] = "GRUB",
};

typedef struct { void *priv; } hal_timer_t;
uint32_t hal_timer_init(hal_timer_t *t);
uint32_t hal_timer_get_value(hal_timer_t *t, uint64_t *value);
uint32_t hal_timer_value_to_us(hal_timer_t *t, uint64_t *value, uint32_t *us);

void cix_set_boot_phase(FW_BOOT_PHASE phase, RECORD_POINT point)
{
	boot_perf_t *p = &g_boot_perf_base[phase];
	uint64_t ticks = 0;
	uint32_t us = 0;

	if (phase >= ARRAY_SIZE(g_boot_perf_base))
		return;
	hal_timer_get_value(NULL, &ticks);
	if (hal_timer_value_to_us(NULL, &ticks, &us)) {
		hal_printf_level(LOG_ERR, "ERR %s, %d\n", __func__, __LINE__);
		return;
	}
	if (point == RECORD_START) {
		strcpy(p->name, phase_name[phase]);
		p->start_ms = us / 1000;
		p->start_hi = 0;
	} else if (point == RECORD_END) {
		p->end_ms = us / 1000;
		p->end_hi = 0;
	}
}

void cix_boot_perf_init(FW_BOOT_PHASE phase)
{
	if (phase < ARRAY_SIZE(g_boot_perf_base))
		memset(&g_boot_perf_base[phase], 0, sizeof(boot_perf_t));
	hal_timer_init(NULL);
}

void cix_boot_perf_uninit(FW_BOOT_PHASE phase)
{
	memcpy((void *)BOOT_PERF_DRAM, g_boot_perf_base, sizeof(g_boot_perf_base));
	drv_ci700_slc_flush(0x83e04000ull, sizeof(g_boot_perf_base));
}
