/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Mode register values (CIX driver/ddr/src/Tech/MemModeRegisters.c): the
 * DRAM technology's callbacks (MemLpddr5ModeRegister.c for LPDDR5) behind
 * one interface.  Translated from CIX's SE firmware debug build, function
 * by function.
 */
#include "mem_types.h"

/* MemCalcMrVal: value of mode register Mr for a channel, rank, device and
 * FSP; 0 for a register the technology does not compute */
uint8_t MemCalcMrVal(uint8_t Mr, uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp)
{
	MEM_MODE_REGISTER_CALLBACK *MemModeRegisterCallback = MemGetTechFuncPtr()->MemModeRegisterCallback;
	MEM_CALC_MR_VAL_CALLBACK MemCalcMrValCallBack;

	switch (Mr) {
	case 0: MemCalcMrValCallBack = MemModeRegisterCallback->CalcMR0Val; break;
	case 1: MemCalcMrValCallBack = MemModeRegisterCallback->CalcMR1Val; break;
	case 2: MemCalcMrValCallBack = MemModeRegisterCallback->CalcMR2Val; break;
	case 3: MemCalcMrValCallBack = MemModeRegisterCallback->CalcMR3Val; break;
	case 4: MemCalcMrValCallBack = MemModeRegisterCallback->CalcMR4Val; break;
	case 8: MemCalcMrValCallBack = MemModeRegisterCallback->CalcMR8Val; break;
	case 10: MemCalcMrValCallBack = MemModeRegisterCallback->CalcMR10Val; break;
	case 11: MemCalcMrValCallBack = MemModeRegisterCallback->CalcMR11Val; break;
	case 12: MemCalcMrValCallBack = MemModeRegisterCallback->CalcMR12Val; break;
	case 13: MemCalcMrValCallBack = MemModeRegisterCallback->CalcMR13Val; break;
	case 14: MemCalcMrValCallBack = MemModeRegisterCallback->CalcMR14Val; break;
	case 15: MemCalcMrValCallBack = MemModeRegisterCallback->CalcMR15Val; break;
	case 16: MemCalcMrValCallBack = MemModeRegisterCallback->CalcMR16Val; break;
	case 17: MemCalcMrValCallBack = MemModeRegisterCallback->CalcMR17Val; break;
	case 18: MemCalcMrValCallBack = MemModeRegisterCallback->CalcMR18Val; break;
	case 19: MemCalcMrValCallBack = MemModeRegisterCallback->CalcMR19Val; break;
	case 20: MemCalcMrValCallBack = MemModeRegisterCallback->CalcMR20Val; break;
	case 22: MemCalcMrValCallBack = MemModeRegisterCallback->CalcMR22Val; break;
	case 23: MemCalcMrValCallBack = MemModeRegisterCallback->CalcMR23Val; break;
	case 25: MemCalcMrValCallBack = MemModeRegisterCallback->CalcMR25Val; break;
	case 28: MemCalcMrValCallBack = MemModeRegisterCallback->CalcMR28Val; break;
	case 30: MemCalcMrValCallBack = MemModeRegisterCallback->CalcMR30Val; break;
	case 31: MemCalcMrValCallBack = MemModeRegisterCallback->CalcMR31Val; break;
	case 32: MemCalcMrValCallBack = MemModeRegisterCallback->CalcMR32Val; break;
	case 33: MemCalcMrValCallBack = MemModeRegisterCallback->CalcMR33Val; break;
	case 34: MemCalcMrValCallBack = MemModeRegisterCallback->CalcMR34Val; break;
	case 35: MemCalcMrValCallBack = MemModeRegisterCallback->CalcMR35Val; break;
	case 37: MemCalcMrValCallBack = MemModeRegisterCallback->CalcMR37Val; break;
	case 40: MemCalcMrValCallBack = MemModeRegisterCallback->CalcMR40Val; break;
	case 41: MemCalcMrValCallBack = MemModeRegisterCallback->CalcMR41Val; break;
	default: return 0;
	}
	if (MemCalcMrValCallBack == NULL)
		return 0;
	return MemCalcMrValCallBack(Channel, Rank, Device, fsp);
}

/* MemModeRegisterOverride */
void MemModeRegisterOverride(void)
{
	MEM_MODE_REGISTER_CALLBACK *MemModeRegisterCallback = MemGetTechFuncPtr()->MemModeRegisterCallback;

	if (MemModeRegisterCallback->ModeRegisterOverride != NULL)
		MemModeRegisterCallback->ModeRegisterOverride();
}

/* MemGetRfmMrCfg: refresh management settings of a channel's DRAM, all 0
 * when the technology has none */
MR_RFM_CFG MemGetRfmMrCfg(uint8_t Channel)
{
	MEM_MODE_REGISTER_CALLBACK *MemModeRegisterCallback = MemGetTechFuncPtr()->MemModeRegisterCallback;
	MR_RFM_CFG RfmCfg;

	if (MemModeRegisterCallback->GetRfmMrCfg == NULL) {
		RfmCfg.RfmEn = 0;
		RfmCfg.RAAIMT = 0;
		RfmCfg.RAAMULT = 0;
	} else {
		RfmCfg = MemModeRegisterCallback->GetRfmMrCfg(Channel);
	}
	return RfmCfg;
}
