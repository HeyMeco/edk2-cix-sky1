/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * DDR feature switches (CIX driver/ddr/src/Lib/MemFeatures.c): what the
 * memory configuration's feature block, the BIOS setup options and the
 * eFuse say about each feature, as the rest of the driver asks for it.
 * Translated from CIX's SE firmware debug build, function by function.
 */
#include "mem_types.h"

/* WrLinkEccEn: write link ECC, only for LPDDR5 above 3200 MT/s (1600 MHz);
 * forced on at 6400 MT/s when the boot environment is set */
uint8_t WrLinkEccEn(uint8_t fsp)
{
	uint8_t WrLEccEn;

	if (GetMemInitPtr()->DdrType != 1 || GetMemFspCfgPtr(fsp)->MemFreq <= 1600) {
		WrLEccEn = 0;
	} else {
		WrLEccEn = GetMemFeatPtr()->WrLEcc;
		if (GetMemInitPtr()->DdrType == 1 && GetMemFspCfgPtr(fsp)->MemFreq == 3200 && MemGetBootEnv())
			WrLEccEn = 1;
		MemSetupGetField(0x1f, 1, &WrLEccEn);
	}
	return WrLEccEn != 0;
}

/* RdLinkEccEn: the same for read link ECC */
uint8_t RdLinkEccEn(uint8_t fsp)
{
	uint8_t RdLEccEn;

	if (GetMemInitPtr()->DdrType != 1 || GetMemFspCfgPtr(fsp)->MemFreq <= 1600) {
		RdLEccEn = 0;
	} else {
		RdLEccEn = GetMemFeatPtr()->RdLEcc;
		if (GetMemInitPtr()->DdrType == 1 && GetMemFspCfgPtr(fsp)->MemFreq == 3200 && MemGetBootEnv())
			RdLEccEn = 1;
		MemSetupGetField(0x20, 1, &RdLEccEn);
	}
	return RdLEccEn != 0;
}

/* ByteModeEn */
uint8_t ByteModeEn(void)
{
	return GetMemInitPtr()->ByteMode != 0;
}

/* DataMaskEn */
uint8_t DataMaskEn(void)
{
	return GetMemFeatPtr()->DataMask;
}

/* ReadDbiEn: setup option, off with read link ECC at the top FSP */
uint8_t ReadDbiEn(void)
{
	uint8_t RdDbiEn = 0;

	MemSetupGetField(0x2a, 1, &RdDbiEn);
	if (RdLinkEccEn(2))
		RdDbiEn = 0;
	return RdDbiEn;
}

/* WriteDbiEn */
uint8_t WriteDbiEn(void)
{
	uint8_t WrDbiEn = 0;

	MemSetupGetField(0x29, 1, &WrDbiEn);
	return WrDbiEn;
}

/* MemRfmEn */
uint8_t MemRfmEn(void)
{
	return GetMemFeatPtr()->RfmEn;
}

/* WckAlwaysOn */
uint8_t WckAlwaysOn(void)
{
	return GetMemFeatPtr()->WckAlwaysOn;
}

/* AutoPrechargeEn */
uint8_t AutoPrechargeEn(void)
{
	return GetMemFeatPtr()->AutoPrechargeEn;
}

/* MemPbrEn: per-bank refresh */
uint8_t MemPbrEn(void)
{
	return GetMemFeatPtr()->PbrEn;
}

/* SelBankInQEn */
uint8_t SelBankInQEn(void)
{
	return GetMemFeatPtr()->SelBankInQ;
}

/* BankArch8BMode: 8-bank mode for LPDDR4X (type 0), and for LPDDR5 when
 * the eFuse does not allow bank-group mode */
uint8_t BankArch8BMode(void)
{
	if (GetMemInitPtr()->DdrType == 0)
		return 1;
	return MemFuseBgModeSupported() == 0;
}

/* WrPostambleEn */
uint8_t WrPostambleEn(void)
{
	uint8_t WrPst = 0;

	MemSetupGetField(0x27, 1, &WrPst);
	return WrPst;
}

/* RdPostambleEn */
uint8_t RdPostambleEn(void)
{
	uint8_t RdPst = 0;

	MemSetupGetField(0x28, 1, &RdPst);
	return RdPst;
}

/* RdPreambleLen: 0 for LPDDR5, 1 for LPDDR4X */
uint8_t RdPreambleLen(void)
{
	if (GetMemInitPtr()->DdrType == 1)
		return 0;
	return GetMemInitPtr()->DdrType == 0;
}

/* WrPreambleLen: the same */
uint8_t WrPreambleLen(void)
{
	if (GetMemInitPtr()->DdrType == 1)
		return 0;
	return GetMemInitPtr()->DdrType == 0;
}

/* IEccEn: inline ECC */
uint8_t IEccEn(void)
{
	return GetMemFeatPtr()->IEcc;
}

/* GetAddressMode: inline ECC forces address mode 1 */
uint8_t GetAddressMode(void)
{
	uint8_t AddressMode = GetMemFeatPtr()->AddressMode;

	if (IEccEn())
		AddressMode = 1;
	return AddressMode;
}

/* PhyPllBypassEn */
uint8_t PhyPllBypassEn(void)
{
	return 0;
}

/* MemBreakpointEn */
uint8_t MemBreakpointEn(void)
{
	return GetMemFeatPtr()->BpEn;
}

/* DqsOscEn */
uint8_t DqsOscEn(void)
{
	return 0;
}

/* DdrIrqPrint */
uint8_t DdrIrqPrint(void)
{
	uint8_t DdrIrqPrt = 0;

	MemSetupGetField(0x2c, 1, &DdrIrqPrt);
	return DdrIrqPrt;
}

/* DfsEnable: frequency changes, off with inline ECC or when the top FSP
 * runs below 2400 MT/s (1200 MHz) */
uint8_t DfsEnable(void)
{
	uint8_t DfsEn = GetMemFeatPtr()->DfsEn;

	if (IEccEn() || GetMemFspCfgPtr(2)->MemFreq < 1200)
		DfsEn = 0;
	return DfsEn;
}
