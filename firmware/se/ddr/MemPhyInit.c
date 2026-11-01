/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * PHY and PI set-up (CIX driver/ddr/src/phy/MemPhyInit.c): CIX's register
 * tables, the board's DQ and byte swaps, pad drive strengths, terminations
 * and equalisation per frequency set point, link ECC, chip selects; the
 * PI's frequency map, chip selects, byte mode, read DFE and preambles.
 * Translated from CIX's SE firmware debug build, function by function.
 */
#include "mem_types.h"

#define GUID_DQ_SWAP	0x1008

/* SetBitField: bits StartBit..EndBit of *OriValue = FieldVal */
static void SetBitField(uint32_t *OriValue, uint32_t FieldVal, uint8_t StartBit, uint8_t EndBit)
{
	uint32_t mask = ((1u << (EndBit + 1)) - 1) ^ ((1u << StartBit) - 1);

	*OriValue = ((FieldVal << StartBit ^ *OriValue) & mask) ^ *OriValue;
}

/* MemPhyLEccCfg: write link ECC per data slice (PHY_70 PHY_WR_LINK_ECC_EN)
 * for set points 1 and 2, through PHY_1281's frequency select */
void MemPhyLEccCfg(void)
{
	DENALI_PHY_70 ddrp_70;
	DENALI_PHY_1281 ddrp_1281;
	uint8_t Channel, fsp, DataSlice;

	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel))
			continue;
		ddrp_1281.value = MemDdrcRegRead(0x5404, Channel);
		ddrp_1281.fields.PHY_FREQ_SEL_MULTICAST_EN = 0;
		MemDdrcRegWrite(0x5404, Channel, ddrp_1281.value);
		for (fsp = 0; fsp < 2; fsp++) {
			ddrp_1281.fields.PHY_FREQ_SEL_INDEX = fsp;
			MemDdrcRegWrite(0x5404, Channel, ddrp_1281.value);
			for (DataSlice = 0; DataSlice < 4; DataSlice++) {
				ddrp_70.value = MemDdrcRegRead(0x4118 + DataSlice * 0x400, Channel);
				ddrp_70.fields.PHY_WR_LINK_ECC_EN_0 = WrLinkEccEn(fsp + 1) != 0;
				MemDdrcRegWrite(0x4118 + DataSlice * 0x400, Channel, ddrp_70.value);
			}
		}
		ddrp_1281.fields.PHY_FREQ_SEL_MULTICAST_EN = 1;
		MemDdrcRegWrite(0x5404, Channel, ddrp_1281.value);
	}
}

/* MemPhyDqSwapCfg: the board's byte and DQ/DM bit swaps (block 0x1008):
 * the address/command slice's data byte map, each data slice's DQ map */
void MemPhyDqSwapCfg(void)
{
	MEM_CONFIG_DQ_SWAP *DqSwapPtr;
	uint32_t size, val32;
	uint8_t Channel, DataSlice, Dq;

	if (CdcbLocateBlockBuf(GUID_DQ_SWAP, &size, (uint8_t **)&DqSwapPtr))
		return;
	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel))
			continue;
		MemDdrcRegWrite(0x5510, Channel,
				DqSwapPtr->Channel[Channel].Byte0 | DqSwapPtr->Channel[Channel].Byte1 << 4 |
				DqSwapPtr->Channel[Channel].Byte2 << 8 | DqSwapPtr->Channel[Channel].Byte3 << 12 |
				0x76540000);
		for (DataSlice = 0; DataSlice < 4; DataSlice++) {
			val32 = 0;
			for (Dq = 0; Dq < 8; Dq++)
				val32 |= (uint32_t)DqSwapPtr->Channel[Channel].DataSlice[DataSlice].DQ[Dq] << (Dq * 4);
			MemDdrcRegWrite(0x41a4 + DataSlice * 0x400, Channel, val32);
			MemDdrcRegWrite(0x41a8 + DataSlice * 0x400, Channel,
					DqSwapPtr->Channel[Channel].DataSlice[DataSlice].DM);
		}
	}
}

/*
 * MemPhyPadProgram: per set point 1 and 2 (PHY_1281 frequency select):
 * data slices' read DFE, DQ and DQS drive/termination selects and FFE taps,
 * receive CTLE; the feedback, CA, CK, CKE, CS and reset pads' drive and FFE.
 */
void MemPhyPadProgram(void)
{
	MEM_PHY_PAD_CFG_ENTRY_S *MemPhyPadCfg;
	DENALI_PHY_1281 ddrp_1281;
	uint32_t Val32, offset;
	uint8_t Channel, fsp;

	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel))
			continue;
		ddrp_1281.value = MemDdrcRegRead(0x5404, Channel);
		ddrp_1281.fields.PHY_FREQ_SEL_MULTICAST_EN = 0;
		for (fsp = 0; fsp < 2; fsp++) {
			ddrp_1281.fields.PHY_FREQ_SEL_INDEX = fsp;
			MemDdrcRegWrite(0x5404, Channel, ddrp_1281.value);
			MemPhyPadCfg = GetPhyPadCfgPtr(fsp + 1);
			for (offset = 0x410c; offset != 0x510c; offset += 0x400) {	/* PHY_67 of each slice */
				Val32 = MemDdrcRegRead(offset, Channel);
				SetBitField(&Val32, MemPhyPadCfg->DqRxDFE ? 3 : 0, 8, 9);	/* PHY_RX_DFE_EN */
				MemDdrcRegWrite(offset, Channel, Val32);
				Val32 = MemDdrcRegRead(offset + 4, Channel);		/* PHY_DQ_TSEL_SELECT */
				SetBitField(&Val32, MemPhyPadCfg->DqOdt, 0, 7);
				SetBitField(&Val32, MemPhyPadCfg->DqDrv, 8, 15);
				MemDdrcRegWrite(offset + 4, Channel, Val32);
				Val32 = MemDdrcRegRead(offset + 0xf8, Channel);	/* PHY_129 */
				SetBitField(&Val32, MemPhyPadCfg->DqTxFFE, 24, 27);	/* PHY_DQ_FFE_TAP */
				SetBitField(&Val32, MemPhyPadCfg->CTLE, 16, 21);	/* PHY_DSLICE_PAD_RX_CTLE_SETTING */
				MemDdrcRegWrite(offset + 0xf8, Channel, Val32);
				Val32 = MemDdrcRegRead(offset + 8, Channel);		/* PHY_DQS_TSEL_SELECT */
				SetBitField(&Val32, MemPhyPadCfg->DqsOdt, 0, 7);
				SetBitField(&Val32, MemPhyPadCfg->DqsDrv, 8, 15);
				MemDdrcRegWrite(offset + 8, Channel, Val32);
				Val32 = MemDdrcRegRead(offset + 0xfc, Channel);	/* PHY_DQS_FFE_TAP */
				SetBitField(&Val32, MemPhyPadCfg->DqsTxFFE, 0, 3);
				MemDdrcRegWrite(offset + 0xfc, Channel, Val32);
			}
			Val32 = MemDdrcRegRead(0x555c, Channel);
			SetBitField(&Val32, MemPhyPadCfg->FdbkDrv, 0, 7);
			MemDdrcRegWrite(0x555c, Channel, Val32);
			Val32 = MemDdrcRegRead(0x5560, Channel);
			SetBitField(&Val32, MemPhyPadCfg->FdbkOdt, 0, 7);
			MemDdrcRegWrite(0x5560, Channel, Val32);
			Val32 = MemDdrcRegRead(0x5574, Channel);
			SetBitField(&Val32, MemPhyPadCfg->CaDrv, 17, 24);
			SetBitField(&Val32, MemPhyPadCfg->CaTxFFE != 0, 29, 29);
			SetBitField(&Val32, MemPhyPadCfg->CaTxFFE, 25, 28);
			MemDdrcRegWrite(0x5574, Channel, Val32);
			Val32 = MemDdrcRegRead(0x5580, Channel);
			SetBitField(&Val32, MemPhyPadCfg->CkDrv, 0, 7);
			MemDdrcRegWrite(0x5580, Channel, Val32);
			Val32 = MemDdrcRegRead(0x5584, Channel);
			SetBitField(&Val32, MemPhyPadCfg->CkTxFFE != 0, 15, 15);
			SetBitField(&Val32, MemPhyPadCfg->CkTxFFE, 16, 19);
			MemDdrcRegWrite(0x5584, Channel, Val32);
			Val32 = MemDdrcRegRead(0x558c, Channel);
			SetBitField(&Val32, MemPhyPadCfg->CkeDrv, 17, 24);
			SetBitField(&Val32, MemPhyPadCfg->CkeTxFFE != 0, 29, 29);
			SetBitField(&Val32, MemPhyPadCfg->CkeTxFFE, 25, 28);
			MemDdrcRegWrite(0x558c, Channel, Val32);
			Val32 = MemDdrcRegRead(0x559c, Channel);
			SetBitField(&Val32, MemPhyPadCfg->CsDrv, 17, 24);
			SetBitField(&Val32, MemPhyPadCfg->CsTxFFE != 0, 29, 29);
			SetBitField(&Val32, MemPhyPadCfg->CsTxFFE, 25, 28);
			MemDdrcRegWrite(0x559c, Channel, Val32);
			Val32 = MemDdrcRegRead(0x5594, Channel);
			SetBitField(&Val32, MemPhyPadCfg->RstDrv, 17, 24);
			SetBitField(&Val32, MemPhyPadCfg->RstTxFFE != 0, 29, 29);
			SetBitField(&Val32, MemPhyPadCfg->RstTxFFE, 25, 28);
			MemDdrcRegWrite(0x5594, Channel, Val32);
		}
		ddrp_1281.fields.PHY_FREQ_SEL_MULTICAST_EN = 1;
		MemDdrcRegWrite(0x5404, Channel, ddrp_1281.value);
	}
}

/* MemPhyInit */
void MemPhyInit(void)
{
	DENALI_PHY_6 ddrp_6;
	uint32_t offset;
	uint8_t Channel;

	postcode(0x9140);
	MemGetTechFuncPtr()->MemDdrpRegInit();
	MemPhyDqSwapCfg();
	MemPhyTimingCfg();
	MemPhyPadProgram();
	MemPhyLEccCfg();
	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel))
			continue;
		for (offset = 0x4018; offset != 0x5018; offset += 0x400) {	/* PHY_6 of each slice */
			ddrp_6.value = MemDdrcRegRead(offset, Channel);
			ddrp_6.fields.PHY_PER_RANK_CS_MAP_0 = GetMemInitPtr()->RanksPerChannel == 2 ? 3 : 1;
			MemDdrcRegWrite(offset, Channel, ddrp_6.value);
		}
	}
}

/* MemPiByteSwapCfg: the PI's data byte swap, when the board swaps bytes */
void MemPiByteSwapCfg(void)
{
	MEM_CONFIG_DQ_SWAP *DqSwapPtr;
	DENALI_PI_321 ddrpi_321;
	uint32_t size;
	uint8_t Channel;

	if (CdcbLocateBlockBuf(GUID_DQ_SWAP, &size, (uint8_t **)&DqSwapPtr))
		return;
	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel))
			continue;
		ddrpi_321.value = MemDdrcRegRead(0x2504, Channel);
		if (DqSwapPtr->Channel[Channel].Byte0 == 0 && DqSwapPtr->Channel[Channel].Byte1 == 1 &&
		    DqSwapPtr->Channel[Channel].Byte2 == 2 && DqSwapPtr->Channel[Channel].Byte3 == 3) {
			ddrpi_321.fields.PI_DATA_BYTE_SWAP_EN = 0;
			MemDdrcRegWrite(0x2504, Channel, ddrpi_321.value);
		} else {
			ddrpi_321.fields.PI_DATA_BYTE_SWAP_EN = 1;
			MemDdrcRegWrite(0x2504, Channel, ddrpi_321.value);
			MemDdrcRegWrite(0x2508, Channel,		/* PI_322 PI_DATA_BYTE_SWAP_SLICE0-3 */
					DqSwapPtr->Channel[Channel].Byte0 | DqSwapPtr->Channel[Channel].Byte1 << 8 |
					DqSwapPtr->Channel[Channel].Byte2 << 16 |
					(uint32_t)DqSwapPtr->Channel[Channel].Byte3 << 24);
		}
	}
}

/*
 * MemPiInit: (not in boot environment 0) CIX's PI table, then: frequency
 * set point registers on, frequencies 0 and 2 (and 1 with DFS) trained,
 * ending at 2; chip selects; byte mode; read DFE per set point; read and
 * write preambles; byte mode overrides of four PI registers (CIX's
 * values); byte swap, timings and data arrays.
 */
void MemPiInit(void)
{
	MEMORY_INIT_S *MemInitPtr;
	DENALI_PI_11 ddrpi_11;
	DENALI_PI_14 ddrpi_14;
	DENALI_PI_21 ddrpi_21;
	DENALI_PI_399 ddrpi_399;
	DENALI_PI_433 ddrpi_433;
	DENALI_PI_435 ddrpi_435;
	DENALI_PI_436 ddrpi_436;
	DENALI_PI_536 ddrpi_536;
	DENALI_PI_537 ddrpi_537;
	uint8_t CsMask, Channel, PiPreambleSupport;

	postcode(0x9150);
	MemInitPtr = GetMemInitPtr();
	CsMask = MemInitPtr->RanksPerChannel == 2 ? 0xf : 5;
	if (MemGetBootEnv() == 0)
		return;
	MemGetTechFuncPtr()->MemDdrpiRegInit();
	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel))
			continue;
		ddrpi_11.value = MemDdrcRegRead(0x202c, Channel);
		ddrpi_11.fields.PI_FSP_FREQ_EN = 1;
		MemDdrcRegWrite(0x202c, Channel, ddrpi_11.value);
		MemDdrcRegRead(0x2034, Channel);		/* PI_13: CIX reads it and writes the map whole */
		MemDdrcRegWrite(0x2034, Channel, DfsEnable() ? 7 : 5);
		ddrpi_14.value = MemDdrcRegRead(0x2038, Channel);
		ddrpi_14.fields.PI_INIT_WORK_FREQ = 2;
		MemDdrcRegWrite(0x2038, Channel, ddrpi_14.value);
		ddrpi_21.value = MemDdrcRegRead(0x2054, Channel);
		ddrpi_21.fields.PI_MC_CS_MAP = CsMask;
		ddrpi_21.fields.PI_CS_MAP = CsMask;
		MemDdrcRegWrite(0x2054, Channel, ddrpi_21.value);
		ddrpi_399.value = MemDdrcRegRead(0x263c, Channel);
		ddrpi_399.fields.PI_BYTE_MODE_EN = ByteModeEn() != 0;
		ddrpi_399.fields.PI_WDQLVL_BYTE_MODE_EN = ByteModeEn() != 0;
		MemDdrcRegWrite(0x263c, Channel, ddrpi_399.value);
		ddrpi_433.value = MemDdrcRegRead(0x26c4, Channel);
		ddrpi_433.fields.PI_RDLVL_DFE_EN_F0 = GetPhyPadCfgPtr(0)->DqRxDFE ? 0xf : 0;
		MemDdrcRegWrite(0x26c4, Channel, ddrpi_433.value);
		ddrpi_435.value = MemDdrcRegRead(0x26cc, Channel);
		ddrpi_435.fields.PI_RDLVL_DFE_EN_F1 = GetPhyPadCfgPtr(1)->DqRxDFE ? 0xf : 0;
		MemDdrcRegWrite(0x26cc, Channel, ddrpi_435.value);
		ddrpi_436.value = MemDdrcRegRead(0x26d0, Channel);
		ddrpi_436.fields.PI_RDLVL_DFE_EN_F2 = GetPhyPadCfgPtr(2)->DqRxDFE ? 0xf : 0;
		MemDdrcRegWrite(0x26d0, Channel, ddrpi_436.value);
		PiPreambleSupport = (RdPreambleLen() != 0) | (WrPreambleLen() ? 2 : 0);
		ddrpi_536.value = MemDdrcRegRead(0x2860, Channel);
		ddrpi_536.fields.PI_PREAMBLE_SUPPORT_F0 = PiPreambleSupport;
		MemDdrcRegWrite(0x2860, Channel, ddrpi_536.value);
		ddrpi_537.value = MemDdrcRegRead(0x2864, Channel);
		ddrpi_537.fields.PI_PREAMBLE_SUPPORT_F1 = PiPreambleSupport;
		ddrpi_537.fields.PI_PREAMBLE_SUPPORT_F2 = PiPreambleSupport;
		MemDdrcRegWrite(0x2864, Channel, ddrpi_537.value);
		if (ByteModeEn()) {
			MemDdrcRegWrite(0x28d8, Channel, 0x19a10c11);
			MemDdrcRegWrite(0x2a14, Channel, 0x01a10c11);
			MemDdrcRegWrite(0x31a0, Channel, 0x19a10c11);
			MemDdrcRegWrite(0x31a4, Channel, 0x10051f00);
		}
	}
	MemPiByteSwapCfg();
	MemPiTimingCfg();
	MemPiDataArrayCfg();
}
