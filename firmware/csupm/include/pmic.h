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
bool pmic_set_mv(unsigned domain, uint32_t mv);

#endif
