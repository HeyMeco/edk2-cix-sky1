/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Refresh management (CIX driver/ddr/src/Feat/RFM/MemRfmMode.c): when the
 * feature is on and the DRAM asks for it (MR27), the controller's RFM
 * timings per FSP and its rolling accumulated ACT thresholds.  Translated
 * from CIX's SE firmware debug build.
 */
#include "mem_types.h"
#include "mem_regs.h"

/* MemRfmModeCfg */
void MemRfmModeCfg(void)
{
	LPDDR_TIMING_S *MemFsp0TimingsPtr, *MemFsp1TimingsPtr, *MemFsp2TimingsPtr;
	MR_RFM_CFG RfmCfg;
	DENALI_CTL_603 ddrc_603;
	DENALI_CTL_604 ddrc_604;
	DENALI_CTL_605 ddrc_605;
	DENALI_CTL_606 ddrc_606;
	DENALI_CTL_607 ddrc_607;
	DENALI_CTL_608 ddrc_608;
	DENALI_CTL_609 ddrc_609;
	DENALI_CTL_610 ddrc_610;
	DENALI_CTL_611 ddrc_611;
	DENALI_CTL_612 ddrc_612;
	DENALI_CTL_613 ddrc_613;
	DENALI_CTL_614 ddrc_614;
	uint8_t Channel;

	if (!MemRfmEn())
		return;
	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel))
			continue;
		MemFsp0TimingsPtr = GetMemTimingsPtr(Channel, 0);
		MemFsp1TimingsPtr = GetMemTimingsPtr(Channel, 1);
		MemFsp2TimingsPtr = GetMemTimingsPtr(Channel, 2);
		RfmCfg = MemGetRfmMrCfg(Channel);
		if (!RfmCfg.RfmEn) {
			ddrc_613.value = 0;
			MemDdrcRegWrite(DENALI_CTL(613), Channel, ddrc_613.value);
			continue;
		}
		ddrc_603.value = 0;
		ddrc_603.fields.TRFM_F0 = MemFsp0TimingsPtr->JedecTimings.Trfm;
		ddrc_603.fields.TRFM_F1 = MemFsp1TimingsPtr->JedecTimings.Trfm;
		MemDdrcRegWrite(DENALI_CTL(603), Channel, ddrc_603.value);
		ddrc_604.value = 0;
		ddrc_604.fields.TRFM_F2 = MemFsp2TimingsPtr->JedecTimings.Trfm;
		ddrc_604.fields.TRFM_SB_F0_0 = MemFsp0TimingsPtr->JedecTimings.Trfmsb;
		MemDdrcRegWrite(DENALI_CTL(604), Channel, ddrc_604.value);
		ddrc_605.value = 0;
		ddrc_605.fields.TRFM2ACT_F0_0 = MemFsp0TimingsPtr->JedecTimings.Trfm2act;
		ddrc_605.fields.TRFM2ACT_F1_0 = MemFsp1TimingsPtr->JedecTimings.Trfm2act;
		ddrc_605.fields.TRFM_SB_F1_0 = MemFsp1TimingsPtr->JedecTimings.Trfmsb;
		MemDdrcRegWrite(DENALI_CTL(605), Channel, ddrc_605.value);
		ddrc_606.value = 0;
		ddrc_606.fields.TRFM2ACT_F2_0 = MemFsp2TimingsPtr->JedecTimings.Trfm2act;
		ddrc_606.fields.TRFM_SB_F2_0 = MemFsp2TimingsPtr->JedecTimings.Trfmsb;
		MemDdrcRegWrite(DENALI_CTL(606), Channel, ddrc_606.value);
		ddrc_607.value = 0;
		ddrc_607.fields.TRFM2ACT_F0_1 = MemFsp0TimingsPtr->JedecTimings.Trfm2act;
		ddrc_607.fields.TRFM_SB_F0_1 = MemFsp0TimingsPtr->JedecTimings.Trfmsb;
		MemDdrcRegWrite(DENALI_CTL(607), Channel, ddrc_607.value);
		ddrc_608.value = 0;
		ddrc_608.fields.TRFM2ACT_F1_1 = MemFsp1TimingsPtr->JedecTimings.Trfm2act;
		ddrc_608.fields.TRFM_SB_F1_1 = MemFsp1TimingsPtr->JedecTimings.Trfmsb;
		MemDdrcRegWrite(DENALI_CTL(608), Channel, ddrc_608.value);
		ddrc_609.value = 0;
		ddrc_609.fields.TRFM2ACT_F2_1 = MemFsp2TimingsPtr->JedecTimings.Trfm2act;
		ddrc_609.fields.TRFM_SB_F2_1 = MemFsp2TimingsPtr->JedecTimings.Trfmsb;
		ddrc_609.fields.REF_RFM_PRIO = 1;
		MemDdrcRegWrite(DENALI_CTL(609), Channel, ddrc_609.value);
		ddrc_610.value = 0;
		ddrc_610.fields.RAAIMT = RfmCfg.RAAIMT;
		ddrc_610.fields.RAA_CNT_DECR_PER_REF = 8;
		MemDdrcRegWrite(DENALI_CTL(610), Channel, ddrc_610.value);
		ddrc_611.value = 0;
		ddrc_611.fields.RAAMMT = RfmCfg.RAAMULT * RfmCfg.RAAIMT;
		ddrc_611.fields.RFM_SB_CONT_EN_THRESHOLD = RfmCfg.RAAIMT + 1;
		MemDdrcRegWrite(DENALI_CTL(611), Channel, ddrc_611.value);
		ddrc_612.value = 0;
		ddrc_612.fields.RFM_NORM_THRESHOLD = RfmCfg.RAAIMT + 6;
		ddrc_612.fields.RFM_HIGH_THRESHOLD = RfmCfg.RAAIMT + 7;
		MemDdrcRegWrite(DENALI_CTL(612), Channel, ddrc_612.value);
		ddrc_613.value = 0;
		ddrc_613.fields.TRFMTH_F0 = RfmCfg.RAAIMT / 8 * MemFsp0TimingsPtr->JedecTimings.Trfmth;
		ddrc_613.fields.RFM_EN = 1;
		MemDdrcRegWrite(DENALI_CTL(613), Channel, ddrc_613.value);
		ddrc_614.value = 0;
		ddrc_614.fields.TRFMTH_F1 = RfmCfg.RAAIMT / 8 * MemFsp1TimingsPtr->JedecTimings.Trfmth;
		ddrc_614.fields.TRFMTH_F2 = RfmCfg.RAAIMT / 8 * MemFsp2TimingsPtr->JedecTimings.Trfmth;
		MemDdrcRegWrite(DENALI_CTL(614), Channel, ddrc_614.value);
	}
}
