/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Register access gaskets of the reset/clock security units (CIX
 * driver/rcsu/drv_rcsu.c, hal/common/hal_rcsu.c).
 *
 * Each RCSU has eight groups: an address range of the block's registers
 * and two initiator rules (initiator ID, secure/non-secure, read/write).
 * The SE fences the control registers of the security-relevant blocks
 * (SE, PM, GPU, NPU, display) to the initiators that own them; the AP's
 * UEFI asks for the FCH xSPI fence once it has taken the flash.
 */
#include "se.h"
#include "arch.h"
#include "mailbox.h"
#include "soc.h"

#define GSKT_ENABLE(b)		((b) + 0x40000050)
#define GSKT_GRP_ATTR(b, g)	((b) + 0x40000054 + (g) * 0xc)
#define GSKT_GRP_START(b, g)	((b) + 0x40000058 + (g) * 0xc)
#define GSKT_GRP_END(b, g)	((b) + 0x4000005c + (g) * 0xc)
#define GSKT_ERR_ADDR(b)	((b) + 0x40000180)
#define GSKT_ERR_INFO(b)	((b) + 0x40000184)

#define IRQn_SEC_FAIL		31
#define IRQn_PM			66
#define IRQn_DP0		77
#define IRQn_DP1		78
#define IRQn_DP2		79
#define IRQn_DP3		80
#define IRQn_DP4		81
#define IRQn_FCH		88
#define IRQn_GPU		90
#define IRQn_NPU		99

/* initiator rule: id_en, id, secure, non-secure, write, read */
#define ID(en, id, s, ns, w, r)	((en) << 1 | (id) << 2 | (s) << 8 | (ns) << 9 | (w) << 12 | (r) << 13)
#define NONE			0

typedef struct {
	uint8_t grp_id;
	uint8_t active;
	uint32_t start, end;
	uint16_t id_a, id_b;
} gskt_grp_t;

#define SE_A(s, ns)		ID(1, 0, s, ns, 1, 1)
#define AP_RO(ns)		ID(1, 5, 1, ns, 0, 1)		/* initiator 5 (AP) reads */

static const gskt_grp_t g_csu_se_rcsu_gasket_init_cfg[] = {
	{ 0, 1, 0x05000050, 0x0500017f, SE_A(1, 1), AP_RO(1) },
	{ 1, 1, 0x05080000, 0x0509ffff, ID(1, 1, 1, 0, 1, 1), AP_RO(1) },
	{ 2, 1, 0x050e0000, 0x050effff, ID(1, 2, 1, 0, 1, 1), AP_RO(1) },
	{ 3, 1, 0x05000034, 0x0500004b, SE_A(1, 1), AP_RO(1) },
	{ 4, 1, 0x05080000, 0x0509ffff, ID(1, 2, 1, 0, 1, 1), NONE },
};

static const gskt_grp_t g_csu_pm_rcsu_gasket_init_cfg[] = {
	{ 0, 1, 0x06000050, 0x0600017f, SE_A(1, 1), AP_RO(1) },
	{ 1, 1, 0x06a10000, 0x06b17fff, SE_A(1, 0), AP_RO(0) },
	{ 2, 1, 0x06010000, 0x064fffff, ID(1, 2, 1, 1, 1, 1), AP_RO(0) },
	{ 3, 1, 0x065b0000, 0x065cffff, ID(1, 1, 1, 0, 1, 1), AP_RO(1) },
	{ 4, 1, 0x06610000, 0x0661ffff, ID(1, 2, 1, 0, 1, 1), AP_RO(1) },
	{ 5, 1, 0x06000034, 0x0600004b, SE_A(1, 0), AP_RO(1) },
	{ 6, 0, 0x06574100, 0x06574ffc, NONE, NONE },
	{ 7, 0, 0x06577100, 0x06577ffc, NONE, NONE },
};

static const gskt_grp_t g_gpu_rcsu_gasket_init_cfg[] = {
	{ 0, 1, 0x15000050, 0x1500017f, SE_A(1, 0), AP_RO(1) },
	{ 1, 1, 0x15000304, 0x15000305, ID(1, 1, 1, 0, 1, 1), AP_RO(1) },
	{ 2, 1, 0x15000304, 0x15000305, ID(1, 1, 0, 1, 0, 1), NONE },
};

static const gskt_grp_t g_npu_rcsu_gasket_init_cfg[] = {
	{ 0, 1, 0x14250050, 0x1425017f, SE_A(1, 0), AP_RO(1) },
	{ 1, 1, 0x14250300, 0x14250301, ID(1, 1, 1, 0, 1, 1), AP_RO(1) },
	{ 2, 1, 0x14250300, 0x14250301, ID(1, 2, 1, 0, 1, 1), NONE },
	{ 3, 1, 0x14250308, 0x14250309, ID(1, 1, 1, 0, 1, 1), AP_RO(1) },
	{ 4, 1, 0x14250308, 0x14250309, ID(1, 2, 1, 0, 1, 1), NONE },
};

/* Display port n: gasket control, the HDCP key window, its status word */
#define DP_CFG(base, ap_s)	{ \
	{ 0, 1, (base) + 0x50, (base) + 0x17f, ID(1, 1, 1, 0, 1, 1), ID(1, 5, ap_s, 1, 0, 1) }, \
	{ 1, 1, (base) + 0x14400, (base) + 0x144ff, ID(1, 1, 1, 0, 1, 1), AP_RO(1) }, \
	{ 2, 1, (base) + 0x1448c, (base) + 0x1448f, ID(1, 1, 0, 1, 1, 1), NONE }, \
}

static const gskt_grp_t g_dp_rcsu_gasket_init_cfg[5][3] = {
	DP_CFG(0x14050000, 1), DP_CFG(0x140c0000, 0), DP_CFG(0x14130000, 1),
	DP_CFG(0x141a0000, 0), DP_CFG(0x14210000, 1),
};

static const gskt_grp_t g_fch_xspi_static_rcsu_gasket_init_cfg[] = {
	{ 0, 0, 0x04180000, 0x0418102b, ID(0, 1, 1, 0, 1, 1), NONE },
	{ 1, 0, 0x04180000, 0x0418102b, ID(0, 1, 1, 0, 1, 1), NONE },
	{ 2, 0, 0x04180000, 0x0418102b, ID(0, 1, 1, 0, 1, 1), NONE },
	{ 3, 0, 0x04180000, 0x0418102b, ID(0, 1, 1, 0, 1, 1), NONE },
	{ 4, 0, 0x04180000, 0x0418102b, ID(0, 1, 1, 0, 1, 1), NONE },
	{ 5, 0, 0x04180000, 0x0418102b, ID(0, 1, 1, 0, 1, 1), NONE },
	{ 6, 0, 0x04180000, 0x0418102b, ID(0, 1, 1, 0, 1, 1), NONE },
	{ 7, 0, 0x04180000, 0x0418102b, ID(0, 1, 1, 0, 1, 1), NONE },
};

static const gskt_grp_t g_fch_xspi_dynamic_rcsu_gasket_init_cfg[] = {
	{ 0, 1, 0x04180000, 0x0418102b, ID(1, 1, 1, 0, 1, 1), NONE },
};

static uint8_t rcsu_region_need_set;

static uint32_t drv_rcsu_gskt_enable(uint32_t base)
{
	REG32(GSKT_ENABLE(base)) = 1;
	dsb();
	isb();
	return 0;
}

static uint32_t drv_rcsu_gskt_grp_set_attr(uint32_t base, const gskt_grp_t *g)
{
	if (g->grp_id > 7 || g->start >= g->end)
		return 0xffffffff;
	REG32(GSKT_GRP_START(base, g->grp_id)) = g->start;
	REG32(GSKT_GRP_END(base, g->grp_id)) = g->end;
	REG32(GSKT_GRP_ATTR(base, g->grp_id)) = (REG32(GSKT_GRP_ATTR(base, g->grp_id)) & 0xc001c001) |
		g->id_a | (uint32_t)g->id_b << 16;
	return 0;
}

static uint32_t drv_rcsu_gskt_init(uint32_t base, const gskt_grp_t *cfg, uint32_t n)
{
	uint32_t i, err;

	if (n > 8)
		return 0xffffffff;
	for (i = 0; cfg && i < n; i++) {
		err = drv_rcsu_gskt_grp_set_attr(base, &cfg[i]);
		if (err)
			return err;
		if (cfg[i].active)
			REG32(GSKT_GRP_ATTR(base, cfg[i].grp_id)) |= 1;
	}
	return 0;
}

static void rcsu_irq_handler(void *ctx)
{
	static const char *const initiator[7] = { "se", "pm", "ap_s", "ap_ns", "dfd", "others", "unknown" };
	uint32_t base = (uint32_t)(uintptr_t)ctx, info, id;

	hal_printf_level(LOG_ERR, "%s rcsu_base = %08x \n", __func__, (unsigned)base);
	REG32(GSKT_ERR_INFO(base)) |= 1;
	info = REG32(GSKT_ERR_INFO(base));
	id = MIN((info & 0x1ff) >> 3, 6u);
	hal_printf_level(LOG_ERR, "violate-id:%s, error address 0x%x\n", initiator[id],
			 (unsigned)REG32(GSKT_ERR_ADDR(base)));
	hal_printf_level(LOG_ERR, "violate:%s\n", info & 2 ? "write" : "read");
	hal_printf_level(LOG_ERR, "violate:%s\n", info & 4 ? "sec" : "non_sec");
}

/* UEFI, done with the flash: fence the FCH xSPI to the PM */
static void dispatcher_rcsu_fencing_handler(void *param, uint32_t plen, void *resp, uint32_t *rlen)
{
	uint32_t err;

	if (param == NULL || resp == NULL) {
		hal_printf_level(LOG_ERR, "ERR %s, %d\n", __func__, __LINE__);
		return;
	}
	if ((*(uint32_t *)param & 0xf) != 1) {
		SE_INFO("%s, rcsu id error \n", __func__);
		return;
	}
	err = drv_rcsu_gskt_init(0, g_fch_xspi_dynamic_rcsu_gasket_init_cfg, 1);
	if (!err)
		err = drv_rcsu_gskt_enable(0);
	((uint32_t *)resp)[0] = err;
	((uint32_t *)resp)[1] = 0xffffffff;
	*rlen = 8;
	rcsu_region_need_set = 1;
	SE_INFO("\n UEFI send mbox to se, and enable the fch region\n");
}

uint32_t hal_rcsu_init(uint32_t is_str)
{
	static const struct {
		uint32_t base;
		const gskt_grp_t *cfg;
		uint8_t n;
		uint8_t irq;
	} rcsu[] = {
		{ 0x05000000, g_csu_se_rcsu_gasket_init_cfg, 5, IRQn_SEC_FAIL },
		{ 0x06000000, g_csu_pm_rcsu_gasket_init_cfg, 8, IRQn_PM },
		{ 0x15000000, g_gpu_rcsu_gasket_init_cfg, 3, IRQn_GPU },
		{ 0x14250000, g_npu_rcsu_gasket_init_cfg, 5, IRQn_NPU },
		{ 0x14050000, g_dp_rcsu_gasket_init_cfg[0], 3, IRQn_DP0 },
		{ 0x140c0000, g_dp_rcsu_gasket_init_cfg[1], 3, IRQn_DP1 },
		{ 0x14130000, g_dp_rcsu_gasket_init_cfg[2], 3, IRQn_DP2 },
		{ 0x141a0000, g_dp_rcsu_gasket_init_cfg[3], 3, IRQn_DP3 },
		{ 0x14210000, g_dp_rcsu_gasket_init_cfg[4], 3, IRQn_DP4 },
	};
	static const uint8_t irq_order[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	uint32_t i, err;

	SE_INFO("%s: init rcsu-gskt \n", __func__);
	for (i = 0; i < ARRAY_SIZE(rcsu); i++) {
		err = drv_rcsu_gskt_init(rcsu[i].base, rcsu[i].cfg, rcsu[i].n);
		if (!err)
			err = drv_rcsu_gskt_enable(rcsu[i].base);
		if (err) {
			SE_ERR(err);
			return err;
		}
	}
	err = drv_rcsu_gskt_init(0, g_fch_xspi_static_rcsu_gasket_init_cfg, 8);
	if (err) {
		SE_ERR(err);
		return err;
	}
	if (is_str && rcsu_region_need_set) {
		err = drv_rcsu_gskt_init(0, g_fch_xspi_dynamic_rcsu_gasket_init_cfg, 1);
		if (!err)
			err = drv_rcsu_gskt_enable(0);
		if (err) {
			SE_ERR(err);
			return err;
		}
	}
	/* PM, GPU, NPU, DP0..4, FCH, then the SE's own */
	for (i = 0; i < ARRAY_SIZE(irq_order); i++)
		core_irq_register(rcsu[irq_order[i]].irq, rcsu_irq_handler,
				  (void *)(uintptr_t)rcsu[irq_order[i]].base);
	core_irq_register(IRQn_FCH, rcsu_irq_handler, NULL);
	core_irq_register(IRQn_SEC_FAIL, rcsu_irq_handler, (void *)0x05000000);
	for (i = 0; i < ARRAY_SIZE(irq_order); i++)
		core_irq_enable(rcsu[irq_order[i]].irq);
	core_irq_enable(IRQn_FCH);
	core_irq_enable(IRQn_SEC_FAIL);
	if (!is_str) {
		err = fw_dispatcher_reg_handler(0x82000012, dispatcher_rcsu_fencing_handler);
		if (err)
			SE_ERR(err);
	}
	return err;
}
