/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The board's rail layout and the rails' voltages: CIX's cfg_dpm.c
 * (dpm_edp_scheme, dpm_edp_cfg_set_type and the lookups), pwr_rail_bind.c
 * and dpm_dashboard.c's dpm_edp_set_voltage, dpm_edp_get_voltage,
 * dpm_edp_restore_voltage, dpm_edp_set_gb.
 *
 * Which buck of which PMIC feeds which EDP domain depends on the board.
 * The built-in layouts are CIX's dpm_edp_scheme; a BIOS PM configuration
 * can give its own (CUSTOM, pm_config.c's pmic_scheme_config), as the
 * Orange Pi 6's does.  Two domains on one buck are bound (pwr_rail_bind):
 * setting one sets the buck to the higher of the two.
 */
#include "pmic.h"
#include "dpm/dpm.h"

/*
 * Board layouts, indexed by pmic_preset and EDP domain: I2C bus and
 * address, buck, regulator type, the domain's current limit, its boot
 * voltage and the offset added at the PMIC (IR drop).  CUSTOM is filled
 * from the BIOS configuration.
 */
#define R(b, a, k, t, lim, mv, off)	{ b, a, k, t, lim, mv, off }
__attribute__((section(".dram1.data"))) static struct rail dpm_edp_scheme[PMIC_PRESETS][EDP_N] = {
	[PMIC_EVB] = {
		R(1, 0x60, 2, 1, 2400, 790, 25), R(0, 0x60, 1, 1, 6700, 790, 30),
		R(1, 0x60, 3, 1, 6500, 790, 30), R(1, 0x60, 0, 1, 8000, 790, 30),
		R(0, 0x60, 2, 1, 8000, 790, 30), R(0, 0x60, 0, 1, 5500, 790, 15),
		R(2, 0x60, 2, 1, 12000, 790, 20), R(2, 0x60, 0, 1, 9000, 790, 15),
	},
	[PMIC_PHECDA] = {
		R(0, 0x45, 1, 2, 2500, 790, 25), R(1, 0x45, 2, 2, 6500, 790, 30),
		R(0, 0x45, 0, 2, 6500, 790, 30), R(0, 0x45, 2, 2, 8000, 790, 30),
		R(0, 0x45, 3, 2, 8000, 790, 30), R(1, 0x45, 3, 2, 5500, 790, 15),
		R(1, 0x45, 1, 2, 12000, 790, 20), R(1, 0x45, 0, 2, 9000, 790, 15),
	},
	[PMIC_CRB] = {
		R(1, 0x60, 1, 1, 2400, 790, 25), R(0, 0x60, 2, 1, 6000, 790, 30),
		R(1, 0x60, 0, 1, 6000, 790, 30), R(1, 0x60, 3, 1, 8000, 790, 30),
		R(1, 0x60, 2, 1, 8000, 790, 30), R(0, 0x60, 3, 1, 5500, 790, 15),
		R(0, 0x60, 0, 1, 12000, 790, 20), R(0, 0x00, 0, 0, 9000, 790, 15),
	},
	[PMIC_NIO] = {
		R(0, 0x60, 1, 1, 2000, 790, 25), R(1, 0x60, 2, 1, 4000, 790, 30),
		R(0, 0x60, 0, 1, 4000, 790, 30), R(1, 0x60, 0, 1, 5000, 790, 30),
		R(1, 0x60, 1, 1, 5000, 790, 30), R(1, 0x60, 3, 1, 3500, 790, 15),
		R(0, 0x60, 3, 1, 6000, 790, 20), R(0, 0x60, 2, 1, 6000, 790, 15),
	},
	[PMIC_CLDBK_POR] = {
		R(0, 0x00, 0, 0, 0, 790, 25), R(0, 0x45, 1, 2, 6000, 790, 30),
		R(0, 0x00, 0, 0, 0, 790, 30), R(0, 0x00, 0, 0, 0, 790, 30),
		R(0, 0x45, 0, 2, 6000, 790, 30), R(0, 0x45, 2, 2, 5500, 790, 15),
		R(0, 0x45, 3, 2, 6000, 790, 20), R(0, 0x00, 0, 0, 8000, 790, 15),
	},
	[PMIC_CLDBK2024] = {
		R(1, 0x60, 1, 1, 0, 790, 25), R(0, 0x60, 3, 1, 6000, 790, 30),
		R(1, 0x60, 0, 1, 0, 790, 30), R(1, 0x60, 3, 1, 0, 790, 30),
		R(1, 0x60, 2, 1, 6000, 790, 30), R(0, 0x60, 2, 1, 5500, 790, 15),
		R(0, 0x60, 0, 1, 6000, 790, 20), R(0, 0x00, 0, 0, 8000, 790, 15),
	},
	[PMIC_ALCOR] = {
		R(0, 0x00, 0, 0, 2500, 825, 25), R(1, 0x21, 1, 3, 6500, 790, 30),
		R(0, 0x21, 1, 3, 6500, 790, 30), R(0, 0x21, 2, 3, 8000, 790, 30),
		R(1, 0x21, 2, 3, 8000, 790, 30), R(0, 0x00, 0, 0, 5500, 815, 15),
		R(1, 0x21, 0, 3, 12000, 790, 20), R(0, 0x21, 0, 3, 9000, 790, 15),
	},
};

/* CIX's dpm_edp_cfg_type: none until the BIOS or the probe says */
static uint8_t dpm_edp_cfg_type = PMIC_PRESETS;
/* CIX's pmic_type_id: the layout's topology, which the V/F tables and the
 * AP (the version page) see */
static uint8_t pmic_type_id;

/*
 * CIX's dpm_edp_cfg_set_type.  A built-in layout is its own topology; a
 * custom one is classified by its regulators: by the SOC rail's (MP5475 on
 * port 2: EVB, on port 0: NIO; MP2845: Phecda; AU4683: Alcor) or, with no
 * regulator on SOC, by the GM0 rail's (MP2845: CldBk PoC; else by its buck:
 * 3 CldBk 2024, 2 CRB).
 */
void dpm_edp_cfg_set_type(enum pmic_preset type)
{
	uint8_t id = 0;

	dpm_edp_cfg_type = (uint8_t)type;
	if (type >= PMIC_EVB && type < PMIC_PRESETS) {
		id = (uint8_t)type;
	} else if (type == PMIC_CUSTOM) {
		const struct rail *soc = &dpm_edp_scheme[PMIC_CUSTOM][EDP_SOC];
		const struct rail *gm0 = &dpm_edp_scheme[PMIC_CUSTOM][EDP_CPU_GM0];

		switch (soc->type) {
		case PMIC_NONE:
			if (gm0->type == PMIC_MP2845)
				id = PMIC_CLDBK_POR;
			else if (gm0->buck == 3)
				id = PMIC_CLDBK2024;
			else if (gm0->buck == 2)
				id = PMIC_CRB;
			break;
		case PMIC_MP5475:
			id = soc->bus == 2 ? PMIC_EVB : soc->bus == 0 ? PMIC_NIO : 0;
			break;
		case PMIC_MP2845:
			id = PMIC_PHECDA;
			break;
		case PMIC_AU4683:
			id = PMIC_ALCOR;
			break;
		}
	}
	pmic_type_id = id;
}

enum pmic_preset dpm_edp_cfg_get_type(void)
{
	return (enum pmic_preset)dpm_edp_cfg_type;
}

enum pmic_preset pmic_get_preset(void)
{
	return dpm_edp_cfg_get_type();
}

uint32_t dpm_edp_cfg_get_pmic_topology_id(void)
{
	return pmic_type_id;
}

const char *dpm_edp_cfg_get_type_name(void)
{
	static const char *const name[PMIC_PRESETS] = {
		"CUSTOM", "Merak", "Phecda", "CRB", "NIO", "CldBk", "CldBk-PoC", "Alcor",
	};

	return dpm_edp_cfg_type < PMIC_PRESETS ? name[dpm_edp_cfg_type] : "N/A";
}

/*
 * CIX's pmic_scheme_config copies the BIOS's pm_config_pmic_t into
 * dpm_edp_scheme[CUSTOM]: the 13 OPP maxima, then the rails, each
 * DPM_PWR_RAIL_CFG's two words: [2:0] regulator type, [18:3] current
 * limit, [21:19] I2C port, [28:22] address, [31:29] buck; [11:0] boot
 * voltage, [21:12] offset (signed).
 */
static uint32_t le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

void pmic_set_custom_scheme(const uint8_t *opp_max, const uint8_t *rails)
{
	for (unsigned i = 0; i < DPM_DxS_MAX; i++)
		dpm_edp_opp_max[PMIC_CUSTOM][i] = (uint16_t)(opp_max[2 * i] | opp_max[2 * i + 1] << 8);
	for (unsigned d = 0; d < EDP_N; d++) {
		uint32_t w0 = le32(rails + 8 * d), w1 = le32(rails + 8 * d + 4);
		struct rail *r = &dpm_edp_scheme[PMIC_CUSTOM][d];

		r->type = (uint8_t)(w0 & 7);
		r->limit = (uint16_t)(w0 >> 3);
		r->bus = (uint8_t)((w0 >> 19) & 7);
		r->addr = (uint8_t)((w0 >> 22) & 0x7f);
		r->buck = (uint8_t)(w0 >> 29);
		r->boot_mv = (uint16_t)(w1 & 0xfff);
		r->offset_mv = (int16_t)((int32_t)(w1 << 10) >> 22);
	}
}

/* CIX's dpm_edp_get_pwr_rail_config (cfg_dpm.c) */
const struct rail *dpm_edp_get_pwr_rail_config(DPM_EDP_DOMAIN edp_id)
{
	if (edp_id >= EDP_N || dpm_edp_cfg_type >= PMIC_PRESETS)
		return NULL;
	return &dpm_edp_scheme[dpm_edp_cfg_type][edp_id];
}

static struct rail *rail_cfg(uint8_t edp_id)
{
	return (struct rail *)dpm_edp_get_pwr_rail_config((DPM_EDP_DOMAIN)edp_id);
}

/* CIX's dpm_edp_get_pwr_rail_config_from_i2cinfo */
const struct rail *dpm_edp_get_pwr_rail_config_from_i2cinfo(uint8_t port, uint8_t buck)
{
	for (uint8_t d = 0; d < EDP_N; d++) {
		const struct rail *r = rail_cfg(d);

		if (r && r->bus == port && r->type && r->buck == buck)
			return r;
	}
	return NULL;
}

/* CIX's dpm_edp_get_pmic_i2c_addr: the first rail on the port with an
 * address */
uint8_t dpm_edp_get_pmic_i2c_addr(uint8_t port)
{
	for (uint8_t d = 0; d < EDP_N; d++) {
		const struct rail *r = rail_cfg(d);

		if (r && r->bus == port && r->addr)
			return r->addr;
	}
	return 0xff;
}

/* --- binding (CIX's pwr_rail_bind.c) ----------------------------------------- */

static struct {
	uint8_t edp_id0, edp_id1;
	bool is_physical;
} s_binding[2] = {
	{ EDP_N, EDP_N, false },
	{ EDP_N, EDP_N, false },
};

bool pwr_rail_bind(uint8_t edp_id0, uint8_t edp_id1, bool is_physical)
{
	const struct rail *r0, *r1;

	for (unsigned i = 0; i < ARRAY_SIZE(s_binding); i++) {
		if (s_binding[i].edp_id0 != EDP_N)
			continue;
		r0 = rail_cfg(edp_id0);
		r1 = rail_cfg(edp_id1);
		if (!r0 || !r1 || !r0->type || !r1->type)
			return false;
		s_binding[i].edp_id0 = edp_id0;
		s_binding[i].edp_id1 = edp_id1;
		s_binding[i].is_physical = is_physical;
		return true;
	}
	return false;
}

void pwr_rail_unbind(void)
{
	for (unsigned i = 0; i < ARRAY_SIZE(s_binding); i++) {
		s_binding[i].edp_id0 = EDP_N;
		s_binding[i].edp_id1 = EDP_N;
		s_binding[i].is_physical = false;
	}
}

uint8_t pwr_rail_bound_buck(uint8_t edp_id, bool *is_physical)
{
	for (uint8_t i = 0; i < ARRAY_SIZE(s_binding); i++) {
		if (s_binding[i].edp_id0 == edp_id || s_binding[i].edp_id1 == edp_id) {
			if (is_physical)
				*is_physical = s_binding[i].is_physical;
			return i;
		}
	}
	return 0xff;
}

/*
 * A bound domain's buck goes to the higher of the two domains' voltages
 * (old and new alike; nothing is written when that does not change); a
 * binding that is not physical sets the other domain's regulator too.
 */
bool pwr_rail_setVoltage(uint8_t edp_id, uint16_t old_voltage, uint16_t new_voltage, bool debug)
{
	uint8_t b = pwr_rail_bound_buck(edp_id, NULL), other;
	uint16_t v = 0;
	bool ok;

	if (b >= ARRAY_SIZE(s_binding))
		return dev_pmic_setVoltage(rail_cfg(edp_id), old_voltage, new_voltage, debug);
	other = s_binding[b].edp_id0 == edp_id ? s_binding[b].edp_id1 : s_binding[b].edp_id0;
	ok = dpm_edp_get_voltage((DPM_EDP_DOMAIN)other, &v);
	if (v > old_voltage)
		old_voltage = v;
	if (v > new_voltage)
		new_voltage = v;
	if (old_voltage == new_voltage)
		return ok;
	ok = dev_pmic_setVoltage(rail_cfg(edp_id), old_voltage, new_voltage, debug);
	if (!s_binding[b].is_physical && ok)
		ok = dev_pmic_setVoltage(rail_cfg(other), old_voltage, new_voltage, debug);
	return ok;
}

/* --- the rails' voltages (CIX's dpm_dashboard.c) --------------------------- */

#define EDP_MV_MAX		980

static uint16_t volt_without_gb[EDP_N];	/* asked for */
static uint16_t volt_set[EDP_N];	/* at the regulator */

/*
 * CIX's dpm_edp_set_voltage: the board's offset added, at most 980 mV,
 * written when it changes (a rail without a regulator only records it).
 */
bool dpm_edp_set_voltage(DPM_EDP_DOMAIN edp_id, uint32_t vol_mV)
{
	const struct rail *r;
	uint32_t mv;

	if (edp_id >= EDP_N)
		return false;
	volt_without_gb[edp_id] = (uint16_t)vol_mV;
	r = rail_cfg((uint8_t)edp_id);
	if (!r)
		return false;
	if (r->type == PMIC_NONE)
		return true;
	mv = (uint32_t)((int32_t)vol_mV + r->offset_mv);
	if (mv > EDP_MV_MAX)
		mv = EDP_MV_MAX;
	if (mv == volt_set[edp_id])
		return true;
	if (!pwr_rail_setVoltage((uint8_t)edp_id, volt_set[edp_id], (uint16_t)mv, true))
		return false;
	volt_set[edp_id] = (uint16_t)mv;
	return true;
}

/*
 * CIX's dpm_edp_restore_voltage (after suspend): each rail asked for a
 * voltage gets the one it had again (pwr_rail_setVoltage from 0, so it is
 * written whatever the regulator has).
 */
bool dpm_edp_restore_voltage(void)
{
	bool ok = true;

	for (uint8_t d = 0; d < EDP_N; d++) {
		if (rail_cfg(d) && volt_set[d])
			ok &= pwr_rail_setVoltage(d, 0, volt_set[d], true);
	}
	return ok;
}

/* the voltage at the regulator, or the boot voltage of a rail without one */
bool dpm_edp_get_voltage(DPM_EDP_DOMAIN edp_id, uint16_t *pVol_mV)
{
	const struct rail *r;

	if (edp_id >= EDP_N || !(r = rail_cfg((uint8_t)edp_id)))
		return false;
	if (pVol_mV)
		*pVol_mV = r->type != PMIC_NONE ? volt_set[edp_id] : r->boot_mv;
	return true;
}

/* CIX's dpm_edp_set_gb: a new offset for a rail, its voltage set again */
bool dpm_edp_set_gb(uint8_t edp_id, int32_t gb_mV)
{
	struct rail *r;

	if (edp_id >= EDP_N || !(r = rail_cfg(edp_id)) || !r->type)
		return false;
	r->offset_mv = (int16_t)((int32_t)((uint32_t)gb_mV << 22) >> 22);
	return dpm_edp_set_voltage((DPM_EDP_DOMAIN)edp_id, volt_without_gb[edp_id]);
}
