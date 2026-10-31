/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Multimedia hub (CIX driver/mmhub/drv_mmhub.c): route the multimedia
 * masters' requests (port group register 0x48 of each port, 2) for the
 * blocks the SoC has.
 */
#include "se.h"
#include "platform.h"
#include "soc.h"

#define MMHUB_PORT(n)	(0x4d044048u + (n) * 0x1000)

static void drv_mmhub_remap_init(const uint8_t *ports)
{
	unsigned int i;

	for (i = 0; i < 32 && ports[i] != 0xff; i++)
		REG32(MMHUB_PORT(ports[i])) = 2;
}

void drv_mmhub_init(void)
{
	static const uint8_t csi[] = { 0, 1, 2, 3, 0xff };
	static const uint8_t isp[] = { 0x13, 0x14, 0x15, 0xff };
	static const uint8_t dpu[] = { 10, 11, 12, 0xff };
	static const uint8_t vpu[] = { 0x19, 0x1a, 0xff };
	static const uint8_t npu[] = { 0x16, 0x17, 0xff };
	uint32_t rev = plat_get_rtl_revision(), i;

	if (rev <= 0x686c)
		return;
	if (rev < 0x896c || plat_get_plat_id() != PLAT_FPGA) {
		/* ports 0..0x19 but 0x18 */
		for (i = 0; i <= 0x19; i++)
			if (i != 0x18)
				REG32(MMHUB_PORT(i)) = 2;
		return;
	}
	if (plat_get_csi_model())
		drv_mmhub_remap_init(csi);
	if (plat_get_isp_model())
		drv_mmhub_remap_init(isp);
	if (plat_get_dpu_model() || plat_get_dp_model())
		drv_mmhub_remap_init(dpu);
	if (plat_get_vpu_model())
		drv_mmhub_remap_init(vpu);
	if (plat_get_npu_model())
		drv_mmhub_remap_init(npu);
}
