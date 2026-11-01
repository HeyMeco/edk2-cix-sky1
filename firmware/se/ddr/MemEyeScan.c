/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Data eye scan and read DQ delay adjustment with the PI's BIST (CIX
 * driver/ddr/src/Feat/BIST/MemEyeScan.c): per DQ, the read (PHY
 * per-bit delays, PHY Vref) or write (write DQ delays, DRAM MR14/MR15 Vref)
 * eye is swept and checked with the BIST.  Translated from CIX's SE
 * firmware debug build, function by function.
 *
 * A read DQ delay register holds the rising- and falling-edge delays of one
 * DQ (bits 15:0 and 31:16), a write DQ delay register those of two DQs.
 * mem_types.h declares the DqDelayOri and Vref parameters as plain pointers;
 * CIX's are uint32_t (*)[8] and uint8_t (*)[2][2], as used here.
 */
#include "mem_types.h"
#include "mem_regs.h"

/* DqDelayMinus: a DQ delay less Delta; for a read delay both edges, or
 * 0xffffffff if either would go below 0 */
static uint32_t DqDelayMinus(uint32_t DqDelay, uint16_t Delta, MBIST_EYE_TYPE type)
{
	uint32_t RetDelay;

	if (type == MBIST_EYE_WR_DQ)
		return DqDelay - Delta;
	if ((DqDelay & 0xffff) < Delta || (DqDelay >> 16) < Delta)
		return 0xffffffff;
	RetDelay = DqDelay - (Delta | (uint32_t)Delta << 16);
	return RetDelay;
}

/* MemDdrcSaveDqDelay: the DQ delays of the four data slices (in the boot
 * environment only): read delays from PHY registers 113-120, write delays
 * (11 bits each) from 107-110 */
void MemDdrcSaveDqDelay(uint8_t Channel, uint32_t *DqDelayOri_, MBIST_EYE_TYPE type)
{
	uint32_t (*DqDelayOri)[8] = (uint32_t (*)[8])DqDelayOri_;
	uint32_t Addr, Val32;
	uint8_t DataSlice, Dq;

	if (!MemGetBootEnv())
		return;
	if (type == MBIST_EYE_WR_DQ) {
		for (DataSlice = 0; DataSlice < 4; DataSlice++) {
			for (Dq = 0; Dq < 8; Dq += 2) {
				Addr = DENALI_PHY(107) + PHY_SLICE(DataSlice) + (Dq >> 1) * 4;
				Val32 = MemDdrcRegRead(Addr, Channel);
				DqDelayOri[DataSlice][Dq] = Val32 & 0x7ff;
				DqDelayOri[DataSlice][Dq + 1] = (Val32 >> 16) & 0x7ff;
			}
		}
	} else {
		for (DataSlice = 0; DataSlice < 4; DataSlice++) {
			for (Dq = 0; Dq < 8; Dq++) {
				Addr = DENALI_PHY(113) + PHY_SLICE(DataSlice) + Dq * 4;
				DqDelayOri[DataSlice][Dq] = MemDdrcRegRead(Addr, Channel);
			}
		}
	}
}

/* MemDdrcRestoreDqDelay: write them back */
void MemDdrcRestoreDqDelay(uint8_t Channel, uint32_t *DqDelayOri_, MBIST_EYE_TYPE type)
{
	uint32_t (*DqDelayOri)[8] = (uint32_t (*)[8])DqDelayOri_;
	uint32_t Addr, Val32;
	uint8_t DataSlice, Dq;

	if (!MemGetBootEnv())
		return;
	if (type == MBIST_EYE_WR_DQ) {
		for (DataSlice = 0; DataSlice < 4; DataSlice++) {
			for (Dq = 0; Dq < 8; Dq += 2) {
				Addr = DENALI_PHY(107) + PHY_SLICE(DataSlice) + (Dq >> 1) * 4;
				Val32 = DqDelayOri[DataSlice][Dq] | DqDelayOri[DataSlice][Dq + 1] << 16;
				MemDdrcRegWrite(Addr, Channel, Val32);
			}
		}
	} else {
		for (DataSlice = 0; DataSlice < 4; DataSlice++) {
			for (Dq = 0; Dq < 8; Dq++) {
				Addr = DENALI_PHY(113) + PHY_SLICE(DataSlice) + Dq * 4;
				MemDdrcRegWrite(Addr, Channel, DqDelayOri[DataSlice][Dq]);
			}
		}
	}
}

/* MemDdrcOverrideDqDelayPerDq: set one DQ's delay (Dq 0-31) */
void MemDdrcOverrideDqDelayPerDq(uint8_t Channel, uint8_t Dq, uint32_t DqDelay, MBIST_EYE_TYPE type)
{
	uint32_t Addr, Val32;

	if (!MemGetBootEnv())
		return;
	if (type == MBIST_EYE_WR_DQ) {
		Addr = DENALI_PHY(107) + PHY_SLICE(Dq >> 3) + ((Dq & 7) >> 1) * 4;
		Val32 = MemDdrcRegRead(Addr, Channel);
		if (!(Dq & 1))
			Val32 = (Val32 & 0xffff0000) | DqDelay;
		else
			Val32 = (Val32 & 0xffff) | DqDelay << 16;
		MemDdrcRegWrite(Addr, Channel, Val32);
	} else {
		Addr = DENALI_PHY(113) + PHY_SLICE(Dq >> 3) + (Dq & 7) * 4;
		MemDdrcRegWrite(Addr, Channel, DqDelay);
	}
}

/* MemDdrcSaveVref: the write Vref of each rank and device (MR14, MR15), or
 * the read Vref of each data slice (PHY pad Vref, low and high byte, less
 * the range bit 0) */
void MemDdrcSaveVref(uint8_t Channel, uint8_t *Vref_, MBIST_EYE_TYPE type)
{
	uint8_t (*Vref)[2][2] = (uint8_t (*)[2][2])Vref_;
	DENALI_PHY_87 PhyVref_L;
	DENALI_PHY_88 PhyVref_H;
	uint8_t rank, Device, DataSlice;

	if (type == MBIST_EYE_WR_DQ) {
		for (rank = 0; rank < GetMemInitPtr()->RanksPerChannel; rank++) {
			for (Device = 0; Device < 2; Device++) {
				Vref[rank][Device][0] = MemModeRegRead(Channel, rank, Device, 14);
				Vref[rank][Device][1] = MemModeRegRead(Channel, rank, Device, 15);
			}
		}
	} else if (type == MBIST_EYE_RD_DQ) {
		for (DataSlice = 0; DataSlice < 4; DataSlice++) {
			PhyVref_L.value = MemDdrcRegRead(DENALI_PHY(87) + PHY_SLICE(DataSlice), Channel);
			PhyVref_H.value = MemDdrcRegRead(DENALI_PHY(88) + PHY_SLICE(DataSlice), Channel);
			Vref[DataSlice >> 1][DataSlice & 1][0] = PhyVref_L.fields.PHY_PAD_VREF_CTRL_DQ_0 >> 1;
			Vref[DataSlice >> 1][DataSlice & 1][1] = PhyVref_H.fields.PHY_PAD_VREF_H_CTRL_DQ_0 >> 1;
		}
	}
}

/* MemDdrcRestoreVref: write them back (the write Vref of device 0) */
void MemDdrcRestoreVref(uint8_t Channel, uint8_t *Vref_, MBIST_EYE_TYPE type)
{
	uint8_t (*Vref)[2][2] = (uint8_t (*)[2][2])Vref_;
	DENALI_PHY_87 PhyVref_L;
	DENALI_PHY_88 PhyVref_H;
	uint8_t rank, DataSlice;

	if (type == MBIST_EYE_WR_DQ) {
		for (rank = 0; rank < GetMemInitPtr()->RanksPerChannel; rank++) {
			MemModeRegWrite(Channel, rank, 14, Vref[rank][0][0]);
			MemModeRegWrite(Channel, rank, 15, Vref[rank][0][1]);
		}
	} else if (type == MBIST_EYE_RD_DQ) {
		for (DataSlice = 0; DataSlice < 4; DataSlice++) {
			PhyVref_L.value = MemDdrcRegRead(DENALI_PHY(87) + PHY_SLICE(DataSlice), Channel);
			PhyVref_H.value = MemDdrcRegRead(DENALI_PHY(88) + PHY_SLICE(DataSlice), Channel);
			PhyVref_L.fields.PHY_PAD_VREF_CTRL_DQ_0 = (PhyVref_L.fields.PHY_PAD_VREF_CTRL_DQ_0 & 0x3e00) |
								 Vref[DataSlice >> 1][DataSlice & 1][0] << 1;
			PhyVref_H.fields.PHY_PAD_VREF_H_CTRL_DQ_0 = (PhyVref_H.fields.PHY_PAD_VREF_H_CTRL_DQ_0 & 0x3e00) |
								   Vref[DataSlice >> 1][DataSlice & 1][1] << 1;
			MemDdrcRegWrite(DENALI_PHY(87) + PHY_SLICE(DataSlice), Channel, PhyVref_L.value);
			MemDdrcRegWrite(DENALI_PHY(88) + PHY_SLICE(DataSlice), Channel, PhyVref_H.value);
		}
	}
}

/* MemDdrcSetVref: one Vref for a rank's DRAM (write), or for every data
 * slice's low byte, the high byte keeping its offset from it (read) */
void MemDdrcSetVref(uint8_t Channel, uint8_t Rank, uint8_t Vref, MBIST_EYE_TYPE type)
{
	DENALI_PHY_87 PhyVref_L;
	DENALI_PHY_88 PhyVref_H;
	uint32_t delta;
	uint8_t DataSlice;

	if (type == MBIST_EYE_WR_DQ) {
		MemModeRegWrite(Channel, Rank, 14, Vref);
		MemModeRegWrite(Channel, Rank, 15, Vref);
	} else if (type == MBIST_EYE_RD_DQ) {
		for (DataSlice = 0; DataSlice < 4; DataSlice++) {
			PhyVref_L.value = MemDdrcRegRead(DENALI_PHY(87) + PHY_SLICE(DataSlice), Channel);
			PhyVref_H.value = MemDdrcRegRead(DENALI_PHY(88) + PHY_SLICE(DataSlice), Channel);
			delta = ((PhyVref_H.fields.PHY_PAD_VREF_H_CTRL_DQ_0 >> 1) & 0xff) -
				((PhyVref_L.fields.PHY_PAD_VREF_CTRL_DQ_0 >> 1) & 0xff);
			PhyVref_L.fields.PHY_PAD_VREF_CTRL_DQ_0 = Vref << 1 | (PhyVref_L.fields.PHY_PAD_VREF_CTRL_DQ_0 & 0x3e00);
			PhyVref_H.fields.PHY_PAD_VREF_H_CTRL_DQ_0 = (PhyVref_H.fields.PHY_PAD_VREF_H_CTRL_DQ_0 & 0x3e00) |
								   (delta + Vref) << 1;
			MemDdrcRegWrite(DENALI_PHY(87) + PHY_SLICE(DataSlice), Channel, PhyVref_L.value);
			MemDdrcRegWrite(DENALI_PHY(88) + PHY_SLICE(DataSlice), Channel, PhyVref_H.value);
		}
	}
}

/* MemDataEyeBistRun: the PI's BIST (mode 2) on the ranks of RankMsk, the
 * second rank from the address past the first; every DQ but Dq masked
 * (0xff: none).  Returns the failed channels */
uint8_t MemDataEyeBistRun(uint8_t Chmsk, uint8_t RankMsk, uint8_t Dq, uint8_t AddrSpace)
{
	MEMORY_INIT_S *MemInitPtr = GetMemInitPtr();
	uint32_t BistMask, StartAddrHi, StartAddrLo, AddrPins;
	uint8_t ret;

	BistMask = Dq == 0xff ? 0 : ~(1u << Dq);
	ret = RankMsk & 1;
	if (ret)
		ret = MemDdrpiBistDataCheck(Chmsk, BistMask, 0, 0, AddrSpace, 2);
	if (MemInitPtr->RanksPerChannel > 1 && (RankMsk & 2)) {
		AddrPins = MemInitPtr->RowBits + MemInitPtr->ColBits + MemInitPtr->BaBits + MemInitPtr->BgBits + 2;
		if (AddrPins < 32) {
			StartAddrLo = 1u << AddrPins;
			StartAddrHi = 0;
		} else {
			StartAddrHi = 1u << (AddrPins - 32);
			StartAddrLo = 0;
		}
		ret |= MemDdrpiBistDataCheck(Chmsk, BistMask, StartAddrHi, StartAddrLo, AddrSpace, 2);
	}
	return ret;
}

/*
 * MemBistDataEye: for read and then write, each channel, rank and DQ: from
 * the trained Vref, Vref steps of 5 (read, 0-255) or 4 (write, 0-127) up to
 * 9 either side, and at each the DQ's delay from at most 48 below the
 * trained one upwards in steps of 2 for 49 points, each point checked by
 * the BIST; the trained values restored after each rank.  CIX's code
 * collects the passing points per DQ and per channel only to print them,
 * which the port leaves out: the BIST runs, its results are not kept.
 */
void MemBistDataEye(void)
{
	MEM_CONFIG_DQ_SWAP *DqSwapPtr;
	uint32_t size, DqDelayOri[4][8];
	uint8_t SavedVref[2][2][2] = { { { 0 } } };
	uint8_t DqMap[32];
	uint8_t Channel, rank, RankMsk, PhyDq, DramDq, vref_base, VrefStep = 4, VrefEnd = 127;
	uint8_t DataSlice, DramDataSlice, Dq, AddrSpace, cur_vref;
	uint32_t vref_index, delay_index, vref_index_start, vref_index_end, left_edge, left_edge_delay;
	uint32_t i, j, cur_dqdelay, range;
	uint16_t Delta;
	MBIST_EYE_TYPE type;
	DENALI_PHY_6 ddrp_6;

	memset(DqDelayOri, 0, sizeof(DqDelayOri));
	if (CdcbLocateBlockBuf(0x1008, &size, (uint8_t **)&DqSwapPtr))
		DqSwapPtr = NULL;
	for (type = MBIST_EYE_RD_DQ; type <= MBIST_EYE_WR_DQ; type++) {
		if (type == MBIST_EYE_RD_DQ) {
			VrefStep = 5;
			VrefEnd = 255;
		} else if (type == MBIST_EYE_WR_DQ) {
			VrefStep = 4;
			VrefEnd = 127;
		}
		for (Channel = 0; Channel < 4; Channel++) {
			if (!MemChannelPopulated(Channel))
				continue;
			/* PHY DQ to DRAM DQ, from the board's DQ swap block (0x1008) */
			if (DqSwapPtr == NULL) {
				for (i = 0; i < 32; i++)
					DqMap[i] = i;
			} else {
				for (DataSlice = 0; DataSlice < 4; DataSlice++) {
					switch (DataSlice) {
					case 0: DramDataSlice = DqSwapPtr->Channel[Channel].Byte0; break;
					case 1: DramDataSlice = DqSwapPtr->Channel[Channel].Byte1; break;
					case 2: DramDataSlice = DqSwapPtr->Channel[Channel].Byte2; break;
					case 3: DramDataSlice = DqSwapPtr->Channel[Channel].Byte3; break;
					}
					for (Dq = 0; Dq < 8; Dq++)
						DqMap[DataSlice * 8 + Dq] =
							DqSwapPtr->Channel[Channel].DataSlice[DataSlice].DQ[Dq] + DramDataSlice * 8;
				}
			}
			MemDdrcRegWrite(DENALI_PHY(1281), Channel, 0x10000);
			for (rank = 0; rank < GetMemInitPtr()->RanksPerChannel; rank++) {
				RankMsk = 1 << rank;
				for (DataSlice = 0; DataSlice < 4; DataSlice++) {
					ddrp_6.value = MemDdrcRegRead(DENALI_PHY(6) + PHY_SLICE(DataSlice), Channel);
					ddrp_6.fields.PHY_PER_CS_TRAINING_INDEX_0 = rank;
					ddrp_6.fields.PHY_PER_CS_TRAINING_MULTICAST_EN_0 = 0;
					MemDdrcRegWrite(DENALI_PHY(6) + PHY_SLICE(DataSlice), Channel, ddrp_6.value);
				}
				MemDdrcSaveDqDelay(Channel, DqDelayOri[0], type);
				MemDdrcSaveVref(Channel, SavedVref[0][0], type);
				for (PhyDq = 0; PhyDq < 32; PhyDq++) {
					DramDq = DqMap[PhyDq];
					if (type == MBIST_EYE_RD_DQ)
						vref_base = SavedVref[PhyDq >> 4][(PhyDq >> 3) & 1][0];
					else
						vref_base = SavedVref[rank][DramDq >> 4][(DramDq >> 3) & 1];
					MemDdrcSetVref(Channel, rank, vref_base, type);
					MemDdrcRestoreDqDelay(Channel, DqDelayOri[0], type);
					cur_dqdelay = DqDelayOri[PhyDq >> 3][PhyDq & 7];
					/* the left edge: the largest of 48, 46, ... the delay
					 * can go down by */
					for (i = 0, Delta = 48;; i++, Delta -= 2) {
						left_edge_delay = DqDelayMinus(cur_dqdelay, Delta, type);
						if (left_edge_delay != 0xffffffff)
							break;
					}
					left_edge = i;
					/* Vref steps below and above, as far as the range allows */
					vref_index_start = 0;
					if (vref_base < VrefStep * 9) {
						for (range = VrefStep * 8;; range -= VrefStep) {
							vref_index_start++;
							if (range <= vref_base)
								break;
						}
					}
					vref_index_end = 18;
					if ((uint32_t)(VrefEnd - vref_base) < VrefStep * 9u) {
						for (range = VrefStep * 8;; range -= VrefStep) {
							vref_index_end--;
							if (range <= (uint32_t)(VrefEnd - vref_base))
								break;
						}
					}
					if (vref_index_end < vref_index_start)
						continue;
					cur_vref = vref_base + VrefStep * (vref_index_start - 9);
					AddrSpace = 12;
					for (vref_index = vref_index_start; vref_index <= vref_index_end; vref_index++) {
						MemDdrcSetVref(Channel, rank, cur_vref, type);
						if (left_edge <= 48) {
							for (delay_index = left_edge, j = 0; delay_index < 49; delay_index++, j += 2) {
								MemDdrcOverrideDqDelayPerDq(Channel, PhyDq,
									(type == MBIST_EYE_WR_DQ ? j : j | j << 16) + left_edge_delay, type);
								/* pass or fail went to the log */
								MemDataEyeBistRun(1 << Channel, RankMsk, DramDq, AddrSpace);
							}
						}
						cur_vref += VrefStep;
					}
				}
				MemDdrcRestoreVref(Channel, SavedVref[0][0], type);
				MemDdrcRestoreDqDelay(Channel, DqDelayOri[0], type);
			}
		}
	}
}

/* MemShiftRdDqDelay: move every read DQ delay of the channels in ChannelMsk
 * by delta % 100 (up to 100: later, above: earlier), each rank; then each
 * data slice's DM delay (PHY register 121) is set from DQ0's, eight times
 * over, as CIX's code does */
void MemShiftRdDqDelay(uint8_t ChannelMsk, uint8_t delta)
{
	uint32_t DqDelayOri[4][8];
	uint32_t RetDelay;
	uint8_t rank, Channel, Dq, DataSlice, shift;
	DENALI_PHY_6 ddrp_6;

	memset(DqDelayOri, 0, sizeof(DqDelayOri));
	shift = delta % 100;
	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel) || !((ChannelMsk >> Channel) & 1))
			continue;
		for (rank = 0; rank < GetMemInitPtr()->RanksPerChannel; rank++) {
			MemDdrcRegWrite(DENALI_PHY(1281), Channel, 0x10000);
			for (DataSlice = 0; DataSlice < 4; DataSlice++) {
				ddrp_6.value = MemDdrcRegRead(DENALI_PHY(6) + PHY_SLICE(DataSlice), Channel);
				ddrp_6.fields.PHY_PER_CS_TRAINING_INDEX_0 = rank;
				ddrp_6.fields.PHY_PER_CS_TRAINING_MULTICAST_EN_0 = 0;
				MemDdrcRegWrite(DENALI_PHY(6) + PHY_SLICE(DataSlice), Channel, ddrp_6.value);
			}
			MemDdrcSaveDqDelay(Channel, DqDelayOri[0], MBIST_EYE_RD_DQ);
			for (Dq = 0; Dq < 32; Dq++) {
				if (delta <= 100)
					RetDelay = DqDelayOri[Dq >> 3][Dq & 7] + (shift | (uint32_t)shift << 16);
				else
					RetDelay = DqDelayMinus(DqDelayOri[Dq >> 3][Dq & 7], shift, MBIST_EYE_RD_DQ);
				DqDelayOri[Dq >> 3][Dq & 7] = RetDelay;
			}
			MemDdrcRestoreDqDelay(Channel, DqDelayOri[0], MBIST_EYE_RD_DQ);
			for (DataSlice = 0; DataSlice < 4; DataSlice++) {
				for (Dq = 0; Dq < 8; Dq++)
					MemDdrcRegWrite(DENALI_PHY(121) + PHY_SLICE(DataSlice), Channel,
							MemDdrcRegRead(DENALI_PHY(113) + PHY_SLICE(DataSlice), Channel));
			}
		}
	}
}

/*
 * MemAdjustDqDelay: centre every read DQ delay of the channels in
 * ChannelMsk in its passing window, each rank: the right and the left margin
 * found by binary search (steps from 32 down, at most 56), the BIST on all
 * those channels at once; the delay set to the middle of the window.
 */
void MemAdjustDqDelay(uint8_t ChannelMsk)
{
	uint32_t DqDelayOri[4][4][8];
	uint32_t DqDelay;
	uint8_t step[4], left_margin[4], right_margin[4];
	uint8_t rank, Channel, DataSlice, Dq, chmsk, status, alldone, startstep = 32, AddrSpace = 8;
	DENALI_PHY_6 ddrp_6;

	memset(DqDelayOri, 0, sizeof(DqDelayOri));
	for (rank = 0; rank < GetMemInitPtr()->RanksPerChannel; rank++) {
		for (Channel = 0; Channel < 4; Channel++) {
			if (!MemChannelPopulated(Channel) || !((ChannelMsk >> Channel) & 1))
				continue;
			MemDdrcRegWrite(DENALI_PHY(1281), Channel, 0x10000);
			for (DataSlice = 0; DataSlice < 4; DataSlice++) {
				ddrp_6.value = MemDdrcRegRead(DENALI_PHY(6) + PHY_SLICE(DataSlice), Channel);
				ddrp_6.fields.PHY_PER_CS_TRAINING_INDEX_0 = rank;
				ddrp_6.fields.PHY_PER_CS_TRAINING_MULTICAST_EN_0 = 0;
				MemDdrcRegWrite(DENALI_PHY(6) + PHY_SLICE(DataSlice), Channel, ddrp_6.value);
			}
			MemDdrcSaveDqDelay(Channel, DqDelayOri[Channel][0], MBIST_EYE_RD_DQ);
		}
		for (Dq = 0; Dq < 32; Dq++) {
			/* right margin */
			for (Channel = 0; Channel < 4; Channel++) {
				if (MemChannelPopulated(Channel) && ((ChannelMsk >> Channel) & 1)) {
					right_margin[Channel] = 0;
					step[Channel] = startstep;
				}
			}
			do {
				chmsk = 0;
				for (Channel = 0; Channel < 4; Channel++) {
					if (!MemChannelPopulated(Channel) || !((ChannelMsk >> Channel) & 1) || !step[Channel])
						continue;
					right_margin[Channel] += step[Channel];
					MemDdrcOverrideDqDelayPerDq(Channel, Dq,
						(right_margin[Channel] | (uint32_t)right_margin[Channel] << 16) +
						DqDelayOri[Channel][Dq >> 3][Dq & 7], MBIST_EYE_RD_DQ);
					chmsk |= 1 << Channel;
				}
				status = MemDataEyeBistRun(chmsk, 1 << rank, 0xff, AddrSpace);
				alldone = 1;
				for (Channel = 0; Channel < 4; Channel++) {
					if (!MemChannelPopulated(Channel) || !((ChannelMsk >> Channel) & 1) || !step[Channel])
						continue;
					if (!((status >> Channel) & 1)) {
						if (right_margin[Channel] > 55) {
							right_margin[Channel] = 56;
							step[Channel] = 0;
							continue;
						}
					} else {
						right_margin[Channel] -= step[Channel];
					}
					step[Channel] >>= 1;
					if (step[Channel])
						alldone = 0;
				}
			} while (!alldone);
			/* left margin, at most the delay itself */
			for (Channel = 0; Channel < 4; Channel++) {
				if (MemChannelPopulated(Channel) && ((ChannelMsk >> Channel) & 1)) {
					left_margin[Channel] = 0;
					step[Channel] = startstep;
				}
			}
			do {
				chmsk = 0;
				for (Channel = 0; Channel < 4; Channel++) {
					if (!MemChannelPopulated(Channel) || !((ChannelMsk >> Channel) & 1) || !step[Channel])
						continue;
					left_margin[Channel] += step[Channel];
					MemDdrcOverrideDqDelayPerDq(Channel, Dq,
						DqDelayMinus(DqDelayOri[Channel][Dq >> 3][Dq & 7], left_margin[Channel],
							     MBIST_EYE_RD_DQ), MBIST_EYE_RD_DQ);
					chmsk |= 1 << Channel;
				}
				status = MemDataEyeBistRun(chmsk, 1 << rank, 0xff, AddrSpace);
				alldone = 1;
				for (Channel = 0; Channel < 4; Channel++) {
					if (!MemChannelPopulated(Channel) || !((ChannelMsk >> Channel) & 1) || !step[Channel])
						continue;
					if (!((status >> Channel) & 1)) {
						if (left_margin[Channel] > 55) {
							left_margin[Channel] = 56;
							step[Channel] = 0;
						}
						DqDelay = DqDelayOri[Channel][Dq >> 3][Dq & 7];
						if ((DqDelay & 0x1ff) <= left_margin[Channel] || (DqDelay >> 16) <= left_margin[Channel]) {
							step[Channel] = 0;
							continue;
						}
					} else {
						left_margin[Channel] -= step[Channel];
					}
					step[Channel] >>= 1;
					if (step[Channel])
						alldone = 0;
				}
			} while (!alldone);
			/* the middle */
			for (Channel = 0; Channel < 4; Channel++) {
				if (!MemChannelPopulated(Channel) || !((ChannelMsk >> Channel) & 1))
					continue;
				DqDelay = DqDelayOri[Channel][Dq >> 3][Dq & 7];
				MemDdrcOverrideDqDelayPerDq(Channel, Dq,
					(DqDelayMinus(DqDelay, left_margin[Channel], MBIST_EYE_RD_DQ) +
					 (right_margin[Channel] | (uint32_t)right_margin[Channel] << 16) + DqDelay) >> 1 &
						0x7fff7fff, MBIST_EYE_RD_DQ);
			}
		}
	}
}
