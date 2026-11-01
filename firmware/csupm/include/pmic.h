/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * PMICs and the rails of the EDP (power) domains: CIX's dev_pmic.c and its
 * drivers (dev_mp5475.c; dev_paged_pmic.c with dev_mp2845b.c and
 * dev_au4683.c), the board's rail layout (cfg_dpm.c's dpm_edp_scheme,
 * pwr_rail_bind.c, dpm_dashboard.c's rail voltages), pmic_profile_setup and
 * the PMIC event stream (pmic_event.c).
 */
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

/* Board layouts (CIX's config_pmic_preset); PMIC_CUSTOM is the one a BIOS
 * PM configuration gives */
enum pmic_preset {
	PMIC_CUSTOM, PMIC_EVB, PMIC_PHECDA, PMIC_CRB, PMIC_NIO,
	PMIC_CLDBK_POR, PMIC_CLDBK2024, PMIC_ALCOR, PMIC_PRESETS
};

/* Regulator types (CIX's vr_type): MP5475 (four bucks, a register each),
 * MP2845 and AU4683 (PMBus-like, a page per buck) */
enum { PMIC_NONE, PMIC_MP5475, PMIC_MP2845, PMIC_AU4683, PMIC_TYPES };

/* A rail: CIX's DPM_PWR_RAIL_CFG, unpacked */
struct rail {
	uint8_t bus, addr, buck, type;
	uint16_t limit;		/* the domain's current limit (pwr_cap) */
	uint16_t boot_mv;	/* voltage set at power-on (vboot_mV) */
	int16_t offset_mv;	/* added at the PMIC (delta_mV: IR drop) */
};

/* CIX's pmic_profile_setup (power-on): the layout (the BIOS's, or found
 * by probing), the PMICs registered, shared bucks bound, the rails at
 * their boot voltages, the PMICs' status read */
void pmic_init(void);
/* CIX's dpm_edp_cfg_set_type (cfg_dpm.c): the layout, and the topology
 * (pmic_type_id) a custom one has */
void dpm_edp_cfg_set_type(enum pmic_preset type);
enum pmic_preset dpm_edp_cfg_get_type(void);
uint32_t dpm_edp_cfg_get_pmic_topology_id(void);
const char *dpm_edp_cfg_get_type_name(void);
/* the old name, for the layout */
enum pmic_preset pmic_get_preset(void);
/* CIX's get_pmic_scheme (pm_config.c): the BIOS configuration's layout
 * word as it is, valid or not; EVB unless it holds one */
enum pmic_preset get_pmic_scheme(void);
/* CIX's ss_s3_exit: Phecda's MP2845s' PSI setting again */
void pmic_resume(void);
/* CIX's pmic_scheme_config: a custom layout from the BIOS (pm_config_pmic_t:
 * opp_max[13], then 8 DPM_PWR_RAIL_CFG of two words) */
void pmic_set_custom_scheme(const uint8_t *opp_max, const uint8_t *rails);
/* CIX's dpm_edp_get_pmic_i2c_addr: the PMIC on an I2C port, 0xff for none */
uint8_t dpm_edp_get_pmic_i2c_addr(uint8_t port);
const struct rail *dpm_edp_get_pwr_rail_config_from_i2cinfo(uint8_t port, uint8_t buck);
/* The rails at the voltages last asked for (after suspend); the EDP
 * domains whose rail could not be set, a bit each (not CIX's) */
uint32_t dpm_edp_restore_voltage(void);
/* CIX's dpm_edp_set_gb: a rail's offset changed, its voltage set again */
bool dpm_edp_set_gb(uint8_t edp_id, int32_t gb_mV);

/* CIX's pwr_rail_bind.c: two domains on one buck (is_physical) or kept
 * together; the higher voltage wins */
bool pwr_rail_bind(uint8_t edp_id0, uint8_t edp_id1, bool is_physical);
void pwr_rail_unbind(void);
uint8_t pwr_rail_bound_buck(uint8_t edp_id, bool *is_physical);
bool pwr_rail_setVoltage(uint8_t edp_id, uint16_t old_voltage, uint16_t new_voltage, bool debug);

/* CIX's dev_pmic.c */
enum pmic_status_time { PMIC_STATUS_POR, PMIC_STATUS_RUN, PMIC_STATUS_ALL };
unsigned pmic_get_versions(uint16_t ver[4]);
bool dev_pmic_setVoltage(const struct rail *r, uint16_t old_mv, uint16_t new_mv, bool debug);
bool dev_pmic_read_status(enum pmic_status_time idx);
uint32_t dev_pmic_build_msg_report_buffer(enum pmic_status_time idx, uint32_t *buf, uint32_t len);
bool dev_pmic_readVoltage(const struct rail *r, uint16_t *mv);
void dev_pmic_register(uint8_t i2c_ch, uint8_t i2c_addr, uint8_t vr_type, uint8_t buck_id,
		       uint8_t bit_idx);
/* the MP5475s' fault registers 07, 08, 09 (CIX's err_flags), per PMIC */
bool pmic_status_get(enum pmic_status_time idx, uint8_t s07[3], uint8_t s08[3], uint8_t s09[3]);

/* CIX's config_pmic.c msg_p2a_on_pmic_error: the PMICs' status to the AP
 * (a CIX SCMI notification, protocol 0x81) */
void msg_p2a_on_pmic_error(void);

/* CIX's set_mp2845b_psi7 (subsys_hsm.c): the MP2845s' power-saving mode
 * on (suspend) or off; config_enable_psi7 is the BIOS's say (pm_config.c) */
extern bool config_enable_psi7;
void set_mp2845b_psi7(bool enable);

/* CIX's pmic_event.c: the rails' voltage changes for the AP's stream */
struct pmic_event_item {
	uint64_t timestamp;
	uint8_t i2c_port, buck;
	uint16_t voltage;
	uint32_t reserved;
};
void pmic_event_stream_init(void);
void pmic_event_stream_setup(void);
void pmic_event_stream_clear(void);
void pmic_event_fetch_snapshot(void);
void pmic_event_free_snapshot(void);
uint8_t pmic_event_item_cnt(void);
const struct pmic_event_item *pmic_event_get_item(uint8_t idx);
void pmic_event_put(uint8_t i2c_port, uint8_t buck, uint16_t voltage);

#endif
