/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * DRAM low power (CIX's Source/common/mclp): the four LPDDR channels'
 * Cadence (Denali) controllers, their PHYs and TrustZone controllers
 * (TZC-400), as the PM drives them.
 *
 * mclp_swif.c   register access and the controllers' low-power commands
 * mclp_hwif.c   frequency changes (DFS) through the PM's DDR clock block
 * mclp_retention.c  suspend: the controllers' state kept in PM SRAM while
 *               DRAM self-refreshes, and restored after
 *
 * A channel's registers (SoC addresses through the PM's window, CIX's
 * g_rcsu_id2base): controller 0x4c010000 + 0x20000 * ch (PI at +0x2000,
 * PHY at +0x4000), TZC-400 0x4c0b0000 + 0x20000 * ch.
 */
#ifndef MCLP_H
#define MCLP_H

#include "pm.h"

#define MCLP_CHANNELS		4
extern uint32_t g_chEnMask;		/* channels present (CIX: 0xf) */

enum mem_reg_type { MEM_REG_DDRC, MEM_REG_TZC, MEM_REG_PHY };

/* DRAM the PM keeps its state in across suspend (CIX's choice) */
#define DDR4PM_DRAM		0x83610000ull
#define SRAM32K_DRAM		0x83608000ull

/* mclp_swif.c */
void mclp_swif_reg_read(enum mem_reg_type type, uint32_t off, uint32_t ch, uint32_t *v);
void mclp_swif_reg_write(enum mem_reg_type type, uint32_t off, uint32_t ch, uint32_t v);
uint32_t mclp_swif_disable_auto_enter_lp(uint32_t ch);
uint32_t mclp_swif_disable_auto_exit_lp(uint32_t ch);
void mclp_swif_enable_auto_enter_lp(uint32_t ch, uint32_t mode);
bool mclp_swif_poll_cmd_complate(uint32_t ch, uint32_t timeout_us);
int32_t mclp_swif_poll_cmd_respond(uint32_t ch, uint32_t timeout_us);
void mclp_swif_set_lp_cmd(uint32_t ch, uint32_t cmd);
void mclp_swif_ack_interrupt(uint32_t ch, uint32_t idx);
bool mclp_swif_parallel_force_lp_cmd(uint32_t cmd);

/* mclp_hwif.c */
void mclp_hwif_init(void);
bool mclp_hwif_power_on_reset(uint32_t ch);
bool mclp_hwif_parallel_run_lp_cmd(uint32_t lp_cmd, uint32_t timeout_us, uint32_t copy_n,
				   bool align_to_vblank);
bool mclp_hwif_dfs(uint32_t copy_n);
bool mclp_hwif_is_dfs_supported(void);

/* mclp_retention.c */
void ddr_s3_enter(void);
void ddr_s3_exit(void);
bool ddr_sr_enter(void);
/* double_check_sum (CIX's u_misc): src/clk/clk.h */

#endif
