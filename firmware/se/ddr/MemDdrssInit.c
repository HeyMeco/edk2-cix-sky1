/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Training (CIX driver/ddr/src/phy/MemDdrssInit.c): the PI trains every
 * populated channel at each frequency set point, asking the SE for the
 * clock changes in between (PM fan-out registers); then the controller's
 * DFS, DBI, the memory configuration's training corrections, a memory test
 * and the final mode registers.  A first pass may only detect the DRAM
 * (density, width, ranks) and ask for another one.  Translated from CIX's
 * SE firmware debug build, function by function; its training report on
 * the log is left out.
 */
#include "mem_types.h"

#define FILE_CODE		0x3001		/* drv_ddr_assert file code */
#define DDR_FANOUT_SMN		0x06510600	/* PM public DDR fan-out registers, per channel */
#define DDRC_STRAP_SMN		0x0c000300	/* RCSU DDRC strap pins, per channel (+ channel << 17) */
#define GUID_TRAIN_OPTIMIZE	0x100b
#define TRAIN_LOOP_COUNT	1000001

/* fan-out register fields (DEV_PM_PUBLIC_REG_DDR_FANOUT_0) */
#define FANOUT_LP_EXT_REQ		(1u << 0)
#define FANOUT_LP_EXT_ACK		(1u << 1)
#define FANOUT_LP_EXT_CMD_STRB		(1u << 10)
#define FANOUT_LP_EXT_DONE		(1u << 11)
#define FANOUT_FREQ_CHANGE_ACK		(1u << 14)
#define FANOUT_FREQ_CHANGE_REQ		(1u << 20)
#define FANOUT_CNTRL_FREQ_CHANGE_REQ	(1u << 30)
#define FANOUT_CNTRL_FREQ_CHANGE_ACK	(1u << 31)

/* UpdateFreqChangeReqStatus: channels whose PI asks for a frequency
 * change; all of them must ask for the same one */
void UpdateFreqChangeReqStatus(uint8_t *ReqStatus, uint8_t *ReqType)
{
	uint32_t pm_ddr_fanout;
	uint8_t Channel;

	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel) || (*ReqStatus >> Channel & 1))
			continue;
		pm_ddr_fanout = SmnRegRead32(DDR_FANOUT_SMN + Channel * 4);
		if (!(pm_ddr_fanout & FANOUT_FREQ_CHANGE_REQ))
			continue;
		*ReqStatus |= 1 << Channel;
		if (*ReqType == 0xff) {
			*ReqType = pm_ddr_fanout >> 15 & 0x1f;		/* freq_change_req_type */
		} else if ((pm_ddr_fanout >> 15 & 0x1f) != *ReqType) {
			log_print(0x80000000, "ERROR: Frequency change request type mismatch!\n");
			drv_ddr_assert(FILE_CODE, 112);			/* CIX's line number */
		}
	}
}

/* UpdateInitCompleteStatus: channels whose INIT_COMP came; 1 when one
 * of them reports a DFI error (INT_STATUS 0x598 bit 18) */
uint8_t UpdateInitCompleteStatus(uint8_t *TrainDone)
{
	uint8_t Channel, ret = 0;

	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel) || (*TrainDone >> Channel & 1))
			continue;
		if (!get_ddr_init_status(Channel))
			continue;
		*TrainDone |= 1 << Channel;
		if (MemDdrcRegRead(0x598, Channel) & 0x40000)
			ret = 1;
	}
	return ret;
}

/*
 * MemDdrDfsSwtich: switch every populated channel to frequency set point
 * FreqSet through the PM's low-power interface: request (lp_ext_req,
 * command 0x14, set point in fc_reg_copy), strobe, the controller's clock
 * change request, the PLL's post divider, acknowledge, done.
 */
void MemDdrDfsSwtich(uint8_t FreqSet)
{
	uint32_t pm_ddr_fanout, addr;
	uint8_t Channel;

	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel))
			continue;
		addr = DDR_FANOUT_SMN + Channel * 4;
		pm_ddr_fanout = SmnRegRead32(addr);
		SmnRegWrite32(addr, (pm_ddr_fanout & 0xfffffc03) | FANOUT_LP_EXT_REQ | (FreqSet & 3) << 2 | 0x14 << 4);
	}
	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel))
			continue;
		addr = DDR_FANOUT_SMN + Channel * 4;
		while (!(SmnRegRead32(addr) & FANOUT_LP_EXT_ACK))
			;
		pm_ddr_fanout = SmnRegRead32(addr);
		SmnRegWrite32(addr, pm_ddr_fanout | FANOUT_LP_EXT_CMD_STRB);
		SmnRegWrite32(addr, pm_ddr_fanout & ~FANOUT_LP_EXT_CMD_STRB);
	}
	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel))
			continue;
		while (!(SmnRegRead32(DDR_FANOUT_SMN + Channel * 4) & FANOUT_CNTRL_FREQ_CHANGE_REQ))
			;
	}
	DdrcDfsPllSwitch(FreqSet);
	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel))
			continue;
		addr = DDR_FANOUT_SMN + Channel * 4;
		SmnRegWrite32(addr, SmnRegRead32(addr) | FANOUT_CNTRL_FREQ_CHANGE_ACK);
		while (SmnRegRead32(addr) & FANOUT_CNTRL_FREQ_CHANGE_REQ)
			;
		SmnRegWrite32(addr, SmnRegRead32(addr) & ~FANOUT_CNTRL_FREQ_CHANGE_ACK);
	}
	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel))
			continue;
		addr = DDR_FANOUT_SMN + Channel * 4;
		while (!(SmnRegRead32(addr) & FANOUT_LP_EXT_DONE))
			;
		SmnRegWrite32(addr, SmnRegRead32(addr) & ~FANOUT_LP_EXT_REQ);
	}
}

/* ConfigurePiLvlSrcEn: CIX only sets PI_3 bit 16 */
void ConfigurePiLvlSrcEn(uint32_t pi_lvl_en, uint8_t Channel)
{
	MemDdrcRegWrite(0x200c, Channel, MemDdrcRegRead(0x200c, Channel) | 0x10000);
}

/* each training step of pi_lvl_en as a two-bit enable */
#define LVL_EN(bit)	((pi_lvl_en >> (bit) & 1) ? 3u : 0u)

/*
 * ConfigurePiLvlEn: the PI's training steps (pi_lvl_en bits, MemInitPtr's
 * TrainLvlType) and the chip selects they run on; with bit 0 the CA Vref
 * range, with bit 4 the DQ Vref range for set points 1 and 2.
 */
void ConfigurePiLvlEn(uint32_t pi_lvl_en, uint8_t Channel)
{
	MEMORY_INIT_S *MemInitPtr = GetMemInitPtr();
	uint32_t v, CsMap = MemInitPtr->RanksPerChannel == 2 ? 0xf : 5;

	v = MemDdrcRegRead(0x2098, Channel);			/* PI_38 */
	MemDdrcRegWrite(0x2098, Channel, (v & 0xfffff0ff) | CsMap << 8);
	v = MemDdrcRegRead(0x20dc, Channel);			/* PI_55 */
	MemDdrcRegWrite(0x20dc, Channel, (v & 0xfff0f0ff) | CsMap << 8 | CsMap << 16);
	v = MemDdrcRegRead(0x210c, Channel);			/* PI_67 */
	MemDdrcRegWrite(0x210c, Channel, (v & 0xf0f0f0ff) | CsMap << 24 | CsMap << 8 | CsMap << 16);
	v = MemDdrcRegRead(0x226c, Channel);			/* PI_155 */
	MemDdrcRegWrite(0x226c, Channel, (v & 0xfff0ffff) | CsMap << 16);
	v = MemDdrcRegRead(0x2114, Channel);			/* PI_69 */
	MemDdrcRegWrite(0x2114, Channel, (v & 0xfcffffff) | LVL_EN(5) << 24);
	v = MemDdrcRegRead(0x2118, Channel);			/* PI_70 */
	MemDdrcRegWrite(0x2118, Channel, (v & 0xfcfcfcfc) | LVL_EN(5) << 16 | LVL_EN(6) | LVL_EN(6) << 24 |
			 LVL_EN(12) << 8);
	v = MemDdrcRegRead(0x211c, Channel);			/* PI_71 */
	MemDdrcRegWrite(0x211c, Channel, (v & 0xfffffffc) | LVL_EN(12));
	v = MemDdrcRegRead(0x2298, Channel);			/* PI_166 */
	MemDdrcRegWrite(0x2298, Channel, (v & 0xfffff0ff) | CsMap << 8);
	v = MemDdrcRegRead(0x26a8, Channel);			/* PI_426 */
	MemDdrcRegWrite(0x26a8, Channel, (v & 0xfffffffc) | LVL_EN(1));
	v = MemDdrcRegRead(0x26ac, Channel);			/* PI_427 */
	MemDdrcRegWrite(0x26ac, Channel, (v & 0xfffffffc) | LVL_EN(1));
	v = MemDdrcRegRead(0x26bc, Channel);			/* PI_431 */
	MemDdrcRegWrite(0x26bc, Channel, (v & 0xfcfcffff) | LVL_EN(2) << 24 | LVL_EN(3) << 16);
	v = MemDdrcRegRead(0x26c0, Channel);			/* PI_432 */
	MemDdrcRegWrite(0x26c0, Channel, (v & 0xfffffcfc) | LVL_EN(2) << 8 | LVL_EN(3));
	v = MemDdrcRegRead(0x26ec, Channel);			/* PI_443 */
	MemDdrcRegWrite(0x26ec, Channel, (v & 0xfffcfcff) | LVL_EN(0) << 8 | LVL_EN(0) << 16);
	v = MemDdrcRegRead(0x2764, Channel);			/* PI_473 */
	MemDdrcRegWrite(0x2764, Channel, (v & 0xfcffffff) | LVL_EN(4) << 24);
	v = MemDdrcRegRead(0x2770, Channel);			/* PI_476 */
	MemDdrcRegWrite(0x2770, Channel, (v & 0xfcffffff) | LVL_EN(4) << 24);
	v = MemDdrcRegRead(0x2778, Channel);			/* PI_478 */
	MemDdrcRegWrite(0x2778, Channel, (v & 0xfffcfcff) | LVL_EN(3) << 8 | LVL_EN(3) << 16);
	v = MemDdrcRegRead(0x277c, Channel);			/* PI_479 */
	MemDdrcRegWrite(0x277c, Channel, (v & 0xfffcfcff) | LVL_EN(1) << 8 | LVL_EN(1) << 16);
	if (pi_lvl_en & 1) {
		v = MemDdrcRegRead(0x2718, Channel);		/* PI_454 */
		MemDdrcRegWrite(0x2718, Channel, (v & 0x80ffffff) | (MemInitPtr->CaVrefSp[1] & 0x7f) << 24);
		v = MemDdrcRegRead(0x271c, Channel);		/* PI_455 */
		MemDdrcRegWrite(0x271c, Channel, (v & 0x8080ff80) | (MemInitPtr->CaVrefSp[2] & 0x7f) << 16 |
				 (MemInitPtr->CaVrefEp[1] & 0x7f) | (MemInitPtr->CaVrefEp[2] & 0x7f) << 24);
	}
	if (pi_lvl_en & 0x10) {
		v = MemDdrcRegRead(0x2764, Channel);
		MemDdrcRegWrite(0x2764, Channel, (v & 0xffff8080) | (MemInitPtr->DqVrefSp[1] & 0x7f) |
				 (MemInitPtr->DqVrefEp[1] & 0x7f) << 8);
		v = MemDdrcRegRead(0x2770, Channel);
		MemDdrcRegWrite(0x2770, Channel, (v & 0xffff8080) | (MemInitPtr->DqVrefSp[2] & 0x7f) |
				 (MemInitPtr->DqVrefEp[2] & 0x7f) << 8);
	}
}

/* LpStateSwitch: a low-power command (CTL_175 LP_CMD); the state reached
 * (CTL_190 LP_STATE, once valid) */
uint8_t LpStateSwitch(uint8_t Channel, uint8_t lp_cmd)
{
	uint32_t v;

	v = MemDdrcRegRead(0x2bc, Channel);
	MemDdrcRegWrite(0x2bc, Channel, (v & 0xff80ffff) | (lp_cmd & 0x7f) << 16);
	do
		v = MemDdrcRegRead(0x2f8, Channel);
	while (!(v & 0x4000));
	MemDdrcRegWrite(0x5a8, Channel, MemDdrcRegRead(0x5a8, Channel) | 1);	/* INT_ACK_LOWPOWER */
	return v >> 8 & 0x7f;
}

/* MemTest: the controller's BIST over the start of each channel */
uint8_t MemTest(void)
{
	MEMORY_INIT_S *MemInitPtr = GetMemInitPtr();
	uint32_t Addr;

	if (MemInitPtr->ChannelCount == 4)
		Addr = 0x20000000;
	else if (MemInitPtr->ChannelCount == 2)
		Addr = 0x40000000;
	else
		Addr = 0x80000000;
	return MemDdrcBistTest(MemInitPtr->ChannelMask, 0, Addr, 17, MEM_BIST_DDRPI_BEST_BANK);
}

/* the training state machine (MEM_TRAIN_STATUS); returns 0 when all
 * channels trained, 1 on a DFI error, 2 on a time-out */
static uint8_t MemTrainWait(uint8_t ChannelMsk)
{
	uint8_t ReqStatus = 0, TrainDone = 0, ReqStatusOld, ReqType = 0xff, CurFreqSet = 0xff, timeoutmsk;
	uint32_t loopcycle, st, addr;
	MEM_TRAIN_STATUS TrainStatus;
	uint8_t Channel;

	if (MemGetBootEnv() == 0) {
		postcode(0x91d5);
		TrainStatus = WAIT_FOR_INIT_COMP;
	} else {
		postcode(0x91d1);
		TrainStatus = TRAINING_IN_PROGRESS;
	}
	for (loopcycle = TRAIN_LOOP_COUNT; loopcycle; loopcycle--) {
		switch (TrainStatus) {
		case TRAINING_IN_PROGRESS:
			ReqStatusOld = ReqStatus;
			UpdateFreqChangeReqStatus(&ReqStatus, &ReqType);
			if (ReqStatus != ReqStatusOld) {
				postcode(0x91d2);
				TrainStatus = WAIT_FOR_REQ;
			} else if (UpdateInitCompleteStatus(&TrainDone) == 1) {
				return 1;
			} else if (TrainDone) {
				postcode(0x91d5);
				TrainStatus = WAIT_FOR_INIT_COMP;
			}
			break;
		case WAIT_FOR_REQ:
			UpdateFreqChangeReqStatus(&ReqStatus, &ReqType);
			if (ReqStatus == ChannelMsk) {
				postcode(0x91d3);
				TrainStatus = FREQ_CHANGE_REQ;
			}
			break;
		case FREQ_CHANGE_REQ:
			if (ReqType != CurFreqSet) {
				DdrcPllFrequencyChange(GetMemFspCfgPtr(ReqType)->MemFreq /
						       GetMemFspCfgPtr(ReqType)->FreqRatio);
				CurFreqSet = ReqType;
			}
			postcode(0x91d4);
			TrainStatus = FREQ_CHANGE_ACK;
			break;
		case FREQ_CHANGE_ACK:
			ReqType = 0xff;
			for (Channel = 0; Channel < 4; Channel++) {
				if (!MemChannelPopulated(Channel) || !(ReqStatus >> Channel & 1))
					continue;
				addr = DDR_FANOUT_SMN + Channel * 4;
				SmnRegWrite32(addr, SmnRegRead32(addr) | FANOUT_FREQ_CHANGE_ACK);
				st = SmnRegRead32(addr);
				while (st & FANOUT_FREQ_CHANGE_REQ)
					st = SmnRegRead32(addr);
				if ((st & (FANOUT_FREQ_CHANGE_REQ | FANOUT_FREQ_CHANGE_ACK)) == FANOUT_FREQ_CHANGE_ACK)
					SmnRegWrite32(addr, st & ~FANOUT_FREQ_CHANGE_ACK);
				ReqStatus &= ~(1 << Channel);
			}
			postcode(0x91d1);
			TrainStatus = TRAINING_IN_PROGRESS;
			break;
		case WAIT_FOR_INIT_COMP:
			UpdateInitCompleteStatus(&TrainDone);
			if (TrainDone == ChannelMsk) {
				postcode(0x91dd);
				return 0;
			}
			break;
		case TRAINING_DONE:
			return 0;
		default:
			break;
		}
	}
	/* time-out: CIX logs the channels that did not finish */
	timeoutmsk = TrainStatus == WAIT_FOR_INIT_COMP ? ChannelMsk ^ TrainDone :
		     TrainStatus == WAIT_FOR_REQ ? ChannelMsk ^ ReqStatus :
		     TrainStatus == TRAINING_IN_PROGRESS ? ChannelMsk : 0;
	if (timeoutmsk)
		log_print(0x4000, "Training timeout: channel mask 0x%x\n", timeoutmsk);
	return 2;
}

/* the DRAM detection pass: density and device width from MR8, a second
 * rank from whether MR14 of rank 1 keeps a value written to it; then the
 * timings again and another pass (3) */
static uint8_t MemDramPkgDetect(MEMORY_INIT_S *MemInitPtr)
{
	/* Gb; CIX's table has the first nine and reads past it for MR8 density
	 * codes above 8 */
	static const uint8_t DensityTlb[16] = { 2, 3, 4, 6, 8, 12, 16, 24, 32 };
	uint8_t Channel, mr8_val = 0, mr14_val;

	for (Channel = 0; Channel < 4; Channel++) {
		if (MemChannelPopulated(Channel)) {
			mr8_val = MemModeRegRead(Channel, 0, 0, 8);
			break;
		}
	}
	if (MemInitPtr->DramPkgDetect & 1)
		MemInitPtr->Density = DensityTlb[mr8_val >> 2 & 0xf];
	if (MemInitPtr->DramPkgDetect & 4)
		MemInitPtr->DeviceWidth = (mr8_val & 0x3fc0) ? 8 : 16;
	if (MemInitPtr->DramPkgDetect & 2) {
		for (Channel = 0; Channel < 4; Channel++) {
			if (!MemChannelPopulated(Channel))
				continue;
			mr14_val = MemModeRegRead(Channel, 1, 0, 14);
			mr14_val = ((mr14_val ^ 1) & 1) | (mr14_val & 0xfe);
			MemModeRegWrite(Channel, 1, 14, mr14_val);
			MemInitPtr->RanksPerChannel = MemModeRegRead(Channel, 1, 0, 14) == mr14_val ? 2 : 1;
			break;
		}
	}
	MemSpdAnalyze();
	return 3;
}

/*
 * The memory configuration's training corrections (block 0x100b): for each
 * channel the first entry that applies (frequency, rank count, channel):
 * a read DQ delay above the threshold fails the training (1), Center has
 * the DQ delays re-centred, Shift moves the read DQ delays.
 */
static uint8_t MemTrainOptimize(void)
{
	MEM_CONFIG_TRAIN_OPTIMIZE *TrnOptTlb;
	MEM_CONFIG_TRAIN_OPTIMIZE_ENTRY *e;
	uint32_t size, EntryCount, index, DqDelayOri[4][8];
	uint8_t RddlyShift[4] = { 0 }, AdjChMsk = 0, Channel, byte, dq;

	if (CdcbLocateBlockBuf(GUID_TRAIN_OPTIMIZE, &size, (uint8_t **)&TrnOptTlb))
		return 0;
	memset(DqDelayOri, 0, sizeof(DqDelayOri));
	EntryCount = (uint32_t)(TrnOptTlb->Header.BlockSize - sizeof(MEM_CONFIG_BLOCK_HEADER)) >> 3;
	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel) ||
		    (uint32_t)(TrnOptTlb->Header.BlockSize - sizeof(MEM_CONFIG_BLOCK_HEADER)) <= 7)
			continue;
		for (index = 0; index < EntryCount; index++) {
			e = &TrnOptTlb->TrnOpt[index];
			if (GetMemFspCfgPtr(2)->MemFreq > e->MaxMemFreq)
				continue;
			if (!(e->RankPerCh >> (GetMemInitPtr()->RanksPerChannel - 1) & 1))
				continue;
			if (!(e->ChMsk & (1 << Channel)))
				continue;
			if (e->Threshold) {
				MemDdrcSaveDqDelay(Channel, DqDelayOri[0], MBIST_EYE_RD_DQ);
				for (byte = 0; byte < 4; byte++) {
					for (dq = 0; dq < 8; dq++) {
						if ((DqDelayOri[byte][dq] & 0xffff) > e->Threshold ||
						    (DqDelayOri[byte][dq] >> 16) > e->Threshold)
							return 1;
					}
				}
			}
			if (e->Center) {
				AdjChMsk |= 1 << Channel;
				GetMemInitPtr()->RetrainFilter = 0;
			}
			if (e->Shift) {
				RddlyShift[Channel] = e->Shift;
				GetMemInitPtr()->RetrainFilter = 0;
			}
			break;
		}
	}
	if (AdjChMsk)
		MemAdjustDqDelay(AdjChMsk);
	for (Channel = 0; Channel < 4; Channel++) {
		if (MemChannelPopulated(Channel) && RddlyShift[Channel])
			MemShiftRdDqDelay(1 << Channel, RddlyShift[Channel]);
	}
	return 0;
}

/*
 * MemDdrssInit: 0 trained, 1 failed (DFI error, memory test, delay over
 * the threshold), 2 timed out, 3 DRAM detected, train again.
 */
uint8_t MemDdrssInit(void)
{
	MEMORY_INIT_S *MemInitPtr = GetMemInitPtr();
	uint32_t pi_lvl_en = MemInitPtr->TrainLvlType, v, addr;
	uint8_t OriTrnMode = GetMemFeatPtr()->TrainMode, Channel, ChannelMsk, status, ret;

	if (MemInitPtr->DramPkgDetect)
		pi_lvl_en = 1;
	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel))
			continue;
		enable_ddr_init_monitor(Channel);
		if (MemGetBootEnv() != 0)
			ConfigurePiLvlEn(pi_lvl_en, Channel);
		MemDdrcRegWrite(0x578, Channel, 0x400);
		/* PI_3: bit 8 on, bit 16 when the training mode is 0 */
		v = MemDdrcRegRead(0x200c, Channel) | 0x100;
		v = (v & ~0x10000u) | (uint32_t)(GetMemFeatPtr()->TrainMode == 0) << 16;
		MemDdrcRegWrite(0x200c, Channel, v);
		/* CTL_198: initial frequency 2, initial FSP 1 */
		v = MemDdrcRegRead(0x318, Channel);
		MemDdrcRegWrite(0x318, Channel, (v & 0xfcfcffff) | 2 << 16 | 1 << 24);
		addr = DDRC_STRAP_SMN + ((uint32_t)Channel << 17);	/* zq_status_in = 1 */
		SmnRegWrite32(addr, (SmnRegRead32(addr) & ~3u) | 1);
		/* PHY_1281: frequency select 1, not multicast */
		v = MemDdrcRegRead(0x5404, Channel);
		MemDdrcRegWrite(0x5404, Channel, (v & 0xfffcfeff) | 1 << 16);
	}
	MemBpCheckpoint(4);

	ChannelMsk = GetMemInitPtr()->ChannelMask;
	postcode(0x91d0);
	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel))
			continue;
		MemDdrcRegWrite(0x2000, Channel, MemDdrcRegRead(0x2000, Channel) | 1);	/* PI_START */
		v = MemDdrcRegRead(0x5404, Channel) & ~0x100u;
		MemDdrcRegWrite(0x5404, Channel, v);
		v &= ~0x30000u;
		MemDdrcRegWrite(0x5404, Channel, v);
		MemDdrcRegWrite(0x5404, Channel, v | 0x100);
		MemDdrcRegWrite(0, Channel, MemDdrcRegRead(0, Channel) | 1);		/* START */
	}
	status = MemTrainWait(ChannelMsk);
	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel))
			continue;
		disable_ddr_init_monitor(Channel);
		MemDdrcRegWrite(0x578, Channel, 0xffffffff);
	}
	if (status)
		return status;

	GetMemFeatPtr()->TrainMode = OriTrnMode;
	if (MemInitPtr->DramPkgDetect)
		return MemDramPkgDetect(MemInitPtr);

	/* acknowledge the PI's training status (CIX reports it on the log) */
	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel))
			continue;
		v = MemDdrcRegRead(0x24a0, Channel);		/* PI_INT_STATUS */
		addr = MemDdrcRegRead(0x24a4, Channel) & 7;
		MemDdrcRegRead(0x24a8, Channel);
		MemDdrcRegWrite(0x24a8, Channel, v);		/* PI_INT_ACK */
		MemDdrcRegWrite(0x24ac, Channel, (MemDdrcRegRead(0x24ac, Channel) & ~3u) | (addr & 3));
	}
	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel))
			continue;
		LpStateSwitch(Channel, 0x11);
		LpStateSwitch(Channel, 2);
	}
	if (DfsEnable()) {
		for (Channel = 0; Channel < 4; Channel++) {
			if (!MemChannelPopulated(Channel))
				continue;
			v = MemDdrcRegRead(0x2094, Channel);
			v = (v & ~0x10000u) | (uint32_t)(GetMemFspCfgPtr(1)->MemFreq < 1200) << 16;
			MemDdrcRegWrite(0x2094, Channel, v);
		}
		MemDdrDfsSwtich(1);
		for (Channel = 0; Channel < 4; Channel++) {
			if (!MemChannelPopulated(Channel))
				continue;
			MemDdrcRegWrite(0x2094, Channel, MemDdrcRegRead(0x2094, Channel) | 0x10000);
			MemDdrcRegWrite(0x20d4, Channel, MemDdrcRegRead(0x20d4, Channel) | 0x1000000);
			MemDdrcRegWrite(0x20d8, Channel, MemDdrcRegRead(0x20d8, Channel) | 0x10000);
			MemDdrcRegWrite(0x226c, Channel, MemDdrcRegRead(0x226c, Channel) | 1);
			MemDdrcRegWrite(0x22ac, Channel, MemDdrcRegRead(0x22ac, Channel) | 0x1000000);
		}
		ret = MemTest();
		if (ret) {
			log_print(0x4000, "MemTest failed after DFS: 0x%x\n", ret);
			return 1;
		}
		MemDdrDfsSwtich(2);
	}
	for (Channel = 0; Channel < 4; Channel++) {			/* PI_369: DBI */
		if (!MemChannelPopulated(Channel))
			continue;
		v = MemDdrcRegRead(0x25c4, Channel);
		v = (v & ~0x100u) | (ReadDbiEn() & 1u) << 8;
		v = (v & ~1u) | (WriteDbiEn() & 1u);
		MemDdrcRegWrite(0x25c4, Channel, v);
	}
	MemBpCheckpoint(8);
	if (MemTrainOptimize())
		return 1;
	ret = MemTest();
	if (ret) {
		log_print(0x4000, "MemTest failed: 0x%x\n", ret);
		return 1;
	}
	for (Channel = 0; Channel < 4; Channel++) {			/* CTL_59 bit 24 */
		if (MemChannelPopulated(Channel))
			MemDdrcRegWrite(0xec, Channel, MemDdrcRegRead(0xec, Channel) & ~0x1000000u);
	}
	MemModeRegisterOverride();
	for (Channel = 0; Channel < 4; Channel++) {
		if (MemChannelPopulated(Channel))
			MemDdrcRegWrite(0xec, Channel, MemDdrcRegRead(0xec, Channel) | 0x1000000);
	}
	return 0;
}
