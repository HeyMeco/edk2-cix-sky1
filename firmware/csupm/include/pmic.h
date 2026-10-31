/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* PMICs and the rails of the EDP (power) domains */
#ifndef PMIC_H
#define PMIC_H

#include "pm.h"

enum edp_domain {
	EDP_CPU_LIT,		/* little cores */
	EDP_CPU_GM0, EDP_CPU_GM1,	/* medium cores, two pairs */
	EDP_CPU_GB0, EDP_CPU_GB1,	/* big cores, two pairs */
	EDP_DSU,
	EDP_GPU,
	EDP_SOC,
	EDP_N
};

/* Board layouts (CIX's config_pmic_preset) */
enum pmic_preset {
	PMIC_CUSTOM, PMIC_EVB, PMIC_PHECDA, PMIC_CRB, PMIC_NIO,
	PMIC_CLDBK_POR, PMIC_CLDBK2024, PMIC_ALCOR, PMIC_PRESETS
};

enum { PMIC_NONE, PMIC_MP5475 };

struct rail {
	uint8_t bus, addr, buck, type;
	uint16_t limit;		/* the domain's current limit */
	uint16_t boot_mv;	/* voltage set at power-on */
	uint8_t offset_mv;	/* added at the PMIC */
};

void pmic_init(void);
void pmic_check(void);
/* The board layout found (CIX's dpm_edp_cfg_type); the rails' voltages are
 * set with dpm_edp_set_voltage (src/dpm/dvfs_cfg.h) */
enum pmic_preset pmic_get_preset(void);
/* The rails at the voltages last asked for (after suspend) */
bool dpm_edp_restore_voltage(void);
/* CIX's dev_pmic.c for the messages: revisions, fault status, voltages */
enum pmic_status_time { PMIC_STATUS_POR, PMIC_STATUS_RUN, PMIC_STATUS_ALL };
unsigned pmic_get_versions(uint16_t ver[4]);
bool dev_pmic_read_status(enum pmic_status_time idx);
uint32_t dev_pmic_build_msg_report_buffer(enum pmic_status_time idx, uint32_t *buf, uint32_t len);
bool dev_pmic_readVoltage(const struct rail *r, uint16_t *mv);


#endif
