/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * LPDDR5 timings (CIX driver/ddr/src/Tech/lp5/MemLpddr5Timing.c): the JEDEC
 * and DFI timings of each channel and FSP from the JEDEC latency tables
 * and the DRAM's minimum times, and from them the PI, PHY and turnaround
 * values and the PI's sequence waits that MemTiming.c writes.  Translated
 * from CIX's SE firmware debug build, function by function.
 *
 * Wck is the data clock (FSP frequency, MHz), Ck the command clock, Wck
 * divided by the WCK:CK ratio and rounded.
 */
#include "mem_types.h"
#include "mem_regs.h"

/* drv_ddr_assert file code of MemLpddr5Timing.c; the line numbers passed
 * with it are CIX's */
#define FILE_CODE	0x4204

/* JEDEC LPDDR5 latency tables by WCK:CK ratio and frequency band (upper
 * limit, MHz) */
const LPDDR5_RL_RTP_ENTRY ReadLatencyTable[] = {
	/* Ratio, MaxFreq, { RL by set 0-2, nRBTP } link ECC off, { RL by set 0-1, nRBTP } on */
	{ 2,  267, { {  6,  6,  6 }, 0 }, { { 0xff, 0xff }, 0xff } },
	{ 2,  533, { {  8,  8,  8 }, 0 }, { { 0xff, 0xff }, 0xff } },
	{ 2,  800, { { 10, 10, 12 }, 0 }, { { 0xff, 0xff }, 0xff } },
	{ 2, 1067, { { 12, 14, 14 }, 0 }, { { 0xff, 0xff }, 0xff } },
	{ 2, 1375, { { 16, 16, 18 }, 2 }, { { 0xff, 0xff }, 0xff } },
	{ 2, 1600, { { 18, 20, 20 }, 2 }, { { 0xff, 0xff }, 0xff } },
	{ 4,  267, { {  3,  3,  3 }, 0 }, { { 0xff, 0xff }, 0xff } },
	{ 4,  533, { {  4,  4,  4 }, 0 }, { { 0xff, 0xff }, 0xff } },
	{ 4,  800, { {  5,  5,  6 }, 0 }, { { 0xff, 0xff }, 0xff } },
	{ 4, 1067, { {  6,  7,  7 }, 0 }, { { 0xff, 0xff }, 0xff } },
	{ 4, 1375, { {  8,  8,  9 }, 1 }, { { 0xff, 0xff }, 0xff } },
	{ 4, 1600, { {  9, 10, 10 }, 1 }, { { 0xff, 0xff }, 0xff } },
	{ 4, 1867, { { 10, 11, 12 }, 2 }, { { 0x0c, 0x0d }, 0x02 } },
	{ 4, 2133, { { 12, 13, 14 }, 2 }, { { 0x0d, 0x0e }, 0x02 } },
	{ 4, 2400, { { 13, 14, 15 }, 3 }, { { 0x0f, 0x10 }, 0x03 } },
	{ 4, 2750, { { 15, 16, 17 }, 4 }, { { 0x11, 0x12 }, 0x04 } },
	{ 4, 3000, { { 16, 17, 19 }, 4 }, { { 0x12, 0x14 }, 0x04 } },
	{ 4, 3200, { { 17, 18, 20 }, 4 }, { { 0x13, 0x15 }, 0x04 } },
};

const LPDDR5_WL_ENTRY WriteLatencyTable[] = {
	/* Ratio, MaxFreq, WL by set A/B */
	{ 2,  267, {  4,  4 } },
	{ 2,  533, {  4,  6 } },
	{ 2,  800, {  6,  8 } },
	{ 2, 1067, {  8, 10 } },
	{ 2, 1375, {  8, 14 } },
	{ 2, 1600, { 10, 16 } },
	{ 4,  267, {  2,  2 } },
	{ 4,  533, {  2,  3 } },
	{ 4,  800, {  3,  4 } },
	{ 4, 1067, {  4,  5 } },
	{ 4, 1375, {  4,  7 } },
	{ 4, 1600, {  5,  8 } },
	{ 4, 1867, {  6,  9 } },
	{ 4, 2133, {  6, 11 } },
	{ 4, 2400, {  7, 12 } },
	{ 4, 2750, {  8, 14 } },
	{ 4, 3000, {  9, 15 } },
	{ 4, 3200, {  9, 16 } },
};

const LPDDR5_NWR_ENTRY nWRTable[] = {
	/* Ratio, MaxFreq, nWR [byte mode off/on][write link ECC off/on] */
	{ 2,  267, { { 0x05, 0xff }, { 0x06, 0xff } } },
	{ 2,  533, { { 0x0a, 0xff }, { 0x0b, 0xff } } },
	{ 2,  800, { { 0x0f, 0xff }, { 0x10, 0xff } } },
	{ 2, 1067, { { 0x14, 0xff }, { 0x15, 0xff } } },
	{ 2, 1375, { { 0x19, 0xff }, { 0x1a, 0xff } } },
	{ 2, 1600, { { 0x1d, 0xff }, { 0x1f, 0xff } } },
	{ 4,  267, { { 0x03, 0xff }, { 0x03, 0xff } } },
	{ 4,  533, { { 0x05, 0xff }, { 0x05, 0xff } } },
	{ 4,  800, { { 0x07, 0xff }, { 0x08, 0xff } } },
	{ 4, 1067, { { 0x0a, 0xff }, { 0x0b, 0xff } } },
	{ 4, 1375, { { 0x0d, 0xff }, { 0x0d, 0xff } } },
	{ 4, 1600, { { 0x0f, 0xff }, { 0x10, 0xff } } },
	{ 4, 1867, { { 0x11, 0x12 }, { 0x12, 0x13 } } },
	{ 4, 2133, { { 0x14, 0x15 }, { 0x15, 0x16 } } },
	{ 4, 2400, { { 0x16, 0x17 }, { 0x17, 0x18 } } },
	{ 4, 2750, { { 0x19, 0x1b }, { 0x1a, 0x1c } } },
	{ 4, 3000, { { 0x1b, 0x1d }, { 0x1d, 0x1f } } },
	{ 4, 3200, { { 0x1d, 0x1f }, { 0x1f, 0x20 } } },
};

const LPDDR5_TWCK2CK_SYNC_ENTRY Twck2ckSyncTable[] = {
	/* Ratio, MaxFreq, tWCKENL_FS, tWCKPRE_static, tWCKPRE_toggle RD, WR */
	{ 2,  267, 0, 1,  6, 3 },
	{ 2,  533, 0, 2,  7, 3 },
	{ 2,  800, 1, 2,  8, 4 },
	{ 2, 1067, 1, 3,  8, 4 },
	{ 2, 1375, 1, 4, 10, 4 },
	{ 2, 1600, 2, 4, 10, 4 },
	{ 4,  267, 0, 1,  3, 2 },
	{ 4,  533, 0, 1,  4, 2 },
	{ 4,  800, 1, 1,  4, 2 },
	{ 4, 1067, 1, 2,  4, 2 },
	{ 4, 1375, 1, 2,  5, 2 },
	{ 4, 1600, 1, 2,  5, 2 },
	{ 4, 1867, 1, 3,  5, 2 },
	{ 4, 2133, 1, 3,  6, 2 },
	{ 4, 2400, 2, 3,  6, 2 },
	{ 4, 2750, 2, 4,  6, 2 },
	{ 4, 3000, 2, 4,  7, 2 },
	{ 4, 3200, 2, 4,  7, 2 },
};

/* GetRlAdj: the PI's read latency adjustment, read latency in WCK cycles
 * less the 7 ns PHY read path (at least 2) */
static uint8_t GetRlAdj(uint16_t Wck, uint8_t Wck2CkRatio, uint8_t RL)
{
	uint8_t rdlat, delay_adder;

	rdlat = Wck2CkRatio * RL + (Wck2CkRatio == 4 ? 5 : 1);
	rdlat -= RdPreambleLen() == 3 ? 4 : 0;
	delay_adder = MemCalculateAcTimingNck(Wck, 7000, 0) - 1;
	if (delay_adder + 1 < rdlat)
		return rdlat - delay_adder;
	return 2;
}

/* GetWlAdj (inlined in CIX's build): the PI's write latency adjustment */
static inline uint8_t GetWlAdj(uint16_t Wck, uint8_t Wck2CkRatio, uint8_t WL)
{
	uint8_t minus = Wck2CkRatio * WL < 6 ? 1 : 3;

	return Wck2CkRatio * WL - minus + (Wck2CkRatio == 4 ? 5 : 1);
}

/* GetBLnMax (inlined in CIX's build): burst length / n, maximum */
static inline uint8_t GetBLnMax(uint8_t fsp)
{
	uint8_t BLnMax;

	if (GetMemFspCfgPtr(fsp)->BankArchMode == 1)
		BLnMax = 2;
	else
		BLnMax = 4;
	return BLnMax;
}

/* MemLpddr5PiTiming: the PI's latency adjustments, WCK timings, CCD and
 * command gaps of one FSP */
void MemLpddr5PiTiming(uint8_t Channel, uint8_t fsp, PI_TIMING_S *Timings)
{
	LPDDR_TIMING_S *MemTimingsPtr = GetMemTimingsPtr(Channel, fsp);
	JEDEC_TIMING_S *JedecTimings = &MemTimingsPtr->JedecTimings;
	uint16_t Wck = GetMemFspCfgPtr(fsp)->MemFreq;
	uint8_t Wck2CkRatio = GetMemFspCfgPtr(fsp)->FreqRatio;
	uint16_t Ck;
	uint8_t RL, RL_Eccoff = 0, WL, RBTP, BLn, BLnMax, PI_TDFI_PHY_RDLAT, tmpVal;
	uint32_t i;

	for (i = 0; i < sizeof(ReadLatencyTable) / sizeof(ReadLatencyTable[0]); i++) {
		if (ReadLatencyTable[i].Ratio == Wck2CkRatio && ReadLatencyTable[i].MaxFreq >= Wck) {
			RL_Eccoff = ReadLatencyTable[i].LEccOff.RLBySet[GetBusCfgPtr(fsp)->RLSet];
			break;
		}
	}
	Ck = Wck / Wck2CkRatio;
	if (Wck % Wck2CkRatio >= Wck2CkRatio >> 1)
		Ck++;
	RL = JedecTimings->RL;
	WL = JedecTimings->WL;
	RBTP = JedecTimings->Trtp;
	BLn = 8 / Wck2CkRatio;
	BLnMax = GetBLnMax(fsp);
	Timings->PI_RDLAT_ADJ = GetRlAdj(Wck, Wck2CkRatio, RL);
	Timings->PI_WRLAT_ADJ = GetWlAdj(Wck, Wck2CkRatio, WL);
	/* memory BIST reads without link ECC */
	Timings->PI_MBIST_RDLAT_ADJ = GetRlAdj(Wck, Wck2CkRatio, RL_Eccoff);
	PI_TDFI_PHY_RDLAT = MemCalculateAcTimingNck(Wck, 1500, 0);
	tmpVal = MemCalculateAcTimingNck(Wck, 7000, 0);
	Timings->PI_TDELAY_RDWR_2_BUS_IDLE = Timings->PI_RDLAT_ADJ + 38 + PI_TDFI_PHY_RDLAT + tmpVal +
					     (RdPreambleLen() == 3 ? 4 : 0);
	Timings->PI_TDFI_WCK_STOP = MemCalculateAcTimingNck(Ck, 6000, 2) + 2;
	Timings->PI_TMRWCKEL = MemCalculateAcTimingNck(Ck, 14000, 9) + 1;
	Timings->PI_TMRW = JedecTimings->Tmrw;
	Timings->PI_TCKEHDQS = MemCalculateAcTimingNck(Ck, 10000, 0) + 14;
	Timings->PI_525_RSV = MemCalculateAcTimingNck(Ck, 50000, 0);
	/* the WCK postamble in clocks, rounded up */
	tmpVal = WrPostambleEn() / Wck2CkRatio;
	if (Wck2CkRatio * tmpVal != WrPostambleEn())
		tmpVal++;
	Timings->PI_WCK_ACTIVE_RD = RL + 1 + BLnMax + tmpVal;
	Timings->PI_WCK_ACTIVE_WR = WL + 1 + BLnMax + tmpVal;
	if (Wck2CkRatio == 4) {
		Timings->PI_TCCD = 4;
		Timings->PI_TCCD_S = 2;
		Timings->PI_TCCD_L = 4;
		Timings->PI_TCCDMW = 16;
	} else {
		Timings->PI_TCCD = 8;
		Timings->PI_TCCD_S = 4;
		Timings->PI_TCCD_L = 8;
		Timings->PI_TCCDMW = 32;
	}
	Timings->PI_RP_GAP = BLn + RBTP;
	Timings->PI_RR_GAP_L = BLn;
	Timings->PI_RR_GAP_S = BLn;
	Timings->PI_RW_GAP = RL - WL + BLn;
	Timings->PI_WP_GAP = WL + BLn + JedecTimings->Twr + 1;
	Timings->PI_WR_GAP_L = WL + BLn + JedecTimings->TwtrL;
	Timings->PI_WR_GAP_S = WL + BLn + JedecTimings->Twtr;
	Timings->PI_WW_GAP_L = BLn;
	Timings->PI_WW_GAP_S = BLn;
}

/* MemLpddr5PhyTiming: the PHY's frequency-dependent settings of one FSP:
 * pad configuration, read path delays, PLL dividers, and per data slice the
 * read gate and the write path latency from the board's flight times and
 * trace length differences (memory configuration block 0x100a; 500 ps and
 * none without it) */
void MemLpddr5PhyTiming(uint8_t Channel, uint8_t fsp, PHY_TIMING_S *Timings)
{
	MEM_CONFIG_TRACE_LEN_DIFF *MemCfgTraceLenDiff;
	uint16_t Wck, Ck, frac;
	uint8_t DataSlice, cnt;
	uint32_t freq_div_num, val32, total_delay, multi, rx_cal_all_dly_10, rx_cal_all_frac_20;
	uint32_t pll_postdiv, flight_time, size;
	int16_t trace_len_diff;

	Wck = GetMemFspCfgPtr(fsp)->MemFreq;
	Ck = Wck / GetMemFspCfgPtr(fsp)->FreqRatio;
	if (Wck % GetMemFspCfgPtr(fsp)->FreqRatio >= GetMemFspCfgPtr(fsp)->FreqRatio >> 1)
		Ck++;
	Timings->PHY_PST_AMBLE = (WrPostambleEn() & 3) << 2 | (RdPostambleEn() & 3);
	if (Wck > 2666) {
		Timings->PHY_WRLVL_PER_START = 0x7f;
		Timings->PHY_REGULATOR_EN_CNT = MemCalculateAcTimingNck(Wck, 5000, 0);
		Timings->PHY_PAD_DSLICE_IO_CFG = Wck < 2750 ? 4 : 5;
	} else if (Wck > 1332) {
		Timings->PHY_WRLVL_PER_START = 0x40;
		Timings->PHY_REGULATOR_EN_CNT = MemCalculateAcTimingNck(Wck, 5000, 0);
		Timings->PHY_PAD_DSLICE_IO_CFG = Wck >= 1600 ? 4 : 3;
	} else {
		Timings->PHY_WRLVL_PER_START = 0x15;
		Timings->PHY_REGULATOR_EN_CNT = MemCalculateAcTimingNck(Wck, 5000, 0);
		if (Wck < 400)
			Timings->PHY_PAD_DSLICE_IO_CFG = 1;
		else if (Wck < 800)
			Timings->PHY_PAD_DSLICE_IO_CFG = 2;
		else
			Timings->PHY_PAD_DSLICE_IO_CFG = 3;
	}
	/* 20 ns in half cycles, the half cycle rounded to 0, 1 or 2 */
	rx_cal_all_dly_10 = MemCalculateAcTimingNck(Wck, 20000, 0);
	rx_cal_all_frac_20 = rx_cal_all_dly_10 % 10 * 2;
	Timings->PHY_RX_CAL_ALL_DLY = rx_cal_all_dly_10 / 10 * 2 +
				      (rx_cal_all_frac_20 == 0 ? 0 : rx_cal_all_frac_20 < 10 ? 1 : 2);
	/* the smallest power of two (from 4) covering 7 ns */
	val32 = MemCalculateAcTimingNck(Wck, 7000, 0);
	Timings->PHY_RX_PCLK_CLK_SEL = 1;
	if (val32 > 2) {
		pll_postdiv = 2;
		while ((1u << pll_postdiv) < val32)
			pll_postdiv++;
		Timings->PHY_RX_PCLK_CLK_SEL = pll_postdiv;
	}
	val32 = MemCalculateAcTimingNck(Wck, 7000, 0);
	Timings->PHY_RDDATA_EN_DLY = val32 - 1;
	Timings->PHY_RDDATA_EN_OE_DLY = val32 - 1;
	Timings->PHY_RDDATA_EN_TSEL_DLY = (uint8_t)(val32 - 1) - MemCalculateAcTimingNck(Wck, 350, 0);
	Timings->PHY_VREF_SETTING_TIME = MemCalculateAcTimingNck(Wck, 200000, 0);
	Timings->PHY_RPTR_UPDATE = MemCalculateAcTimingNck(Wck, 1500, 0) + 3;
	if (Wck <= 25)
		multi = 7;
	else if (Wck <= 50)
		multi = 6;
	else if (Wck <= 100)
		multi = 5;
	else if (Wck <= 200)
		multi = 4;
	else if (Wck <= 400)
		multi = 3;
	else if (Wck <= 800)
		multi = 2;
	else if (Wck <= 1600)
		multi = 1;
	else
		multi = 0;
	Timings->PHY_PLL_CTRL = multi * 0x200 + 0x1142;
	Timings->PHY_PLL_CTRL_8X = multi * 0x200 + 0x3142;
	Timings->PHY_ADR_MEAS_DLY_STEP_ENABLE = Ck > 781;
	Timings->PHY_ADR_CALVL_DLY_STEP = Ck <= 781 ? 2 : 1;
	if (Ck <= 400)
		Timings->PHY_TCKSRE_WAIT = 1;
	else if (Ck <= 800)
		Timings->PHY_TCKSRE_WAIT = 3;
	else
		Timings->PHY_TCKSRE_WAIT = Ck <= 1000 ? 4 : 5;
	freq_div_num = 0;
	while ((Ck >> freq_div_num) > 25)
		freq_div_num++;
	Timings->PHY_PAD_ACS_IO_CFG = freq_div_num + 0x10e00;
	Timings->PHY_PAD_ACS_RX_PCLK_CLK_SEL = freq_div_num;
	Timings->PHY_RDDQS_DQx_RISE_FALL_SLAVE_DELAY = Wck > 1333 ? 0x50 : 0x40;
	Timings->PHY_WRLVL_RESP_WAIT_CNT = (MemCalculateThresholdNck(Wck, 9000, 0) + 14) >> 1;
	val32 = MemCalculateThresholdNck(Wck, 102400, 0) + 138;
	if (val32 > 511)
		val32 = 511;
	Timings->PHY_RDLVL_MAX_EDGE = val32;
	Timings->PHY_CLK_WRDQx_SLAVE_DELAY = MemCalculateThresholdNck(Wck, 140800, 0) + 320;

	if (CdcbLocateBlockBuf(0x100a, &size, (uint8_t **)&MemCfgTraceLenDiff))
		MemCfgTraceLenDiff = NULL;
	for (DataSlice = 0; DataSlice < 4; DataSlice++) {
		if (MemCfgTraceLenDiff == NULL) {
			trace_len_diff = 0;
			flight_time = 500;
		} else {
			flight_time = MemCfgTraceLenDiff->Channel[Channel].AvgFlightTime[DataSlice];
			trace_len_diff = MemCfgTraceLenDiff->Channel[Channel].TraceLenDiff[DataSlice];
		}
		/* read gate: flight time plus 600 ps, in 1/10 cycles (latency in
		 * cycles, slave delay in 1/256 or finer steps at low speed) */
		total_delay = (flight_time + 600) * 20;
		Timings->PHY_RDDQS_LATENCY_ADJUST[DataSlice] = (MemCalculateAcTimingNck(Wck, total_delay, 0) + 5) / 10;
		if (Wck < 45)
			multi = 1024;
		else if (Wck < 90)
			multi = 512;
		else
			multi = 256;
		Timings->PHY_RDDQS_GATE_SLAVE_DELAY[DataSlice] =
			multi * (MemCalculateAcTimingNck(Wck, total_delay, 0) + 5 -
				 Timings->PHY_RDDQS_LATENCY_ADJUST[DataSlice] * 10) / 10;
		Timings->PHY_GTLVL_LAT_ADJ_START[DataSlice] = MemCalculateThresholdNck(Wck, (flight_time + 600) * 2, 0);
		/* write path: a longer trace adds latency, a shorter one takes
		 * whole cycles off and adds the fraction back */
		if (trace_len_diff == 0) {
			Timings->PHY_WRITE_PATH_LAT_ADD[DataSlice] = 0;
			Timings->PHY_WRITE_PATH_LAT_DEC[DataSlice] = 0;
			Timings->PHY_WRITE_PATH_LAT_FRAC[DataSlice] = 0;
		} else if (trace_len_diff < 0) {
			Timings->PHY_WRITE_PATH_LAT_ADD[DataSlice] = 0;
			for (cnt = 1; 1000000 * cnt / Wck + trace_len_diff < 0; cnt++)
				;
			Timings->PHY_WRITE_PATH_LAT_DEC[DataSlice] = cnt;
			frac = 256000000 * cnt / Wck - trace_len_diff * 256;
			Timings->PHY_WRITE_PATH_LAT_FRAC[DataSlice] = MemCalculateThresholdNck(Wck, frac, 0);
		} else {
			Timings->PHY_WRITE_PATH_LAT_DEC[DataSlice] = 0;
			Timings->PHY_WRITE_PATH_LAT_ADD[DataSlice] = MemCalculateThresholdNck(Wck, trace_len_diff, 0);
			Timings->PHY_WRITE_PATH_LAT_FRAC[DataSlice] =
				MemCalculateThresholdNck(Wck, trace_len_diff << 8, 0);
		}
	}
}

/* MemLpddr5TurnaroundTiming: the controller's turnaround delays of one FSP
 * (mode register access after read/write, different chip select, read to
 * write on the same chip select) */
void MemLpddr5TurnaroundTiming(uint8_t Channel, uint8_t fsp, TURNAROUND_TIMING_S *Timings)
{
	LPDDR_TIMING_S *MemTimingsPtr = GetMemTimingsPtr(Channel, fsp);
	JEDEC_TIMING_S *JedecTimings = &MemTimingsPtr->JedecTimings;
	uint16_t Wck = GetMemFspCfgPtr(fsp)->MemFreq;
	uint8_t Wck2CkRatio = GetMemFspCfgPtr(fsp)->FreqRatio;
	uint8_t BankArchMode = GetMemFspCfgPtr(fsp)->BankArchMode;
	uint16_t Ck;
	uint8_t RL, WL, ODTLON_WR, TODTL_OFF, rdqs_post_pre_amble_term, param_gen_2cycle_rd_preamble;
	uint8_t r2r_diffcs_dly;
	int8_t rtl_val, jedec_val;

	Ck = Wck / Wck2CkRatio;
	if (Wck % Wck2CkRatio >= Wck2CkRatio >> 1)
		Ck++;
	RL = JedecTimings->RL;
	WL = JedecTimings->WL;
	if (Wck2CkRatio == 4) {
		if (Wck <= 800)
			ODTLON_WR = WL - 1;
		else if (Wck <= 1867)
			ODTLON_WR = WL - 2;
		else if (Wck <= 3200)
			ODTLON_WR = WL - 3;
		else
			ODTLON_WR = WL - 4;
		TODTL_OFF = BankArchMode == 0 ? 5 : 3;
	} else {
		if (Wck2CkRatio == 2) {
			if (Wck < 268)
				ODTLON_WR = WL - 1;
			else if (Wck <= 800)
				ODTLON_WR = WL - 2;
			else if (Wck < 1376)
				ODTLON_WR = WL - 3;
			else
				ODTLON_WR = WL - 4;
		} else {
			ODTLON_WR = 0;
		}
		TODTL_OFF = BankArchMode == 0 ? 9 : 5;
	}
	Timings->RW2MRW_DLY = MemCalculateAcTimingNck(Ck, 7500, 4);
	Timings->R2W_DIFFCS_DLY = 2;
	if ((RdPostambleEn() & 3) == 2) {
		rdqs_post_pre_amble_term = RdPreambleLen() & 3;
		if (rdqs_post_pre_amble_term != 3)
			rdqs_post_pre_amble_term = 2;
	} else if ((RdPostambleEn() & 3) == 1) {
		rdqs_post_pre_amble_term = RdPreambleLen() & 3;
		if (rdqs_post_pre_amble_term != 3)
			rdqs_post_pre_amble_term = 2;
	} else {
		rdqs_post_pre_amble_term = (RdPreambleLen() & 3) == 3 ? 2 : 1;
	}
	param_gen_2cycle_rd_preamble = RdPreambleLen();
	r2r_diffcs_dly = rdqs_post_pre_amble_term + 2 + MemCalculateAcTimingNck(Ck, 650, 0);
	r2r_diffcs_dly -= MemCalculateAcTimingNck(Ck, 1900, 0);
	if ((param_gen_2cycle_rd_preamble & 3) == 3)
		r2r_diffcs_dly--;
	Timings->R2R_DIFFCS_DLY = r2r_diffcs_dly;
	Timings->W2W_DIFFCS_DLY = TODTL_OFF + WL - 2 - ODTLON_WR + MemCalculateAcTimingNck(Ck, 3500, 0);
	/* write to read on another chip select: what the read's termination
	 * needs beyond what the controller already leaves (CIX asserts when the
	 * controller's gap is the larger, then carries on) */
	rtl_val = (uint8_t)(WL + 3) - RL - (uint8_t)MemCalculateAcTimingNck(Ck, 650, 0);
	jedec_val = TODTL_OFF + WL - RL + MemCalculateAcTimingNck(Ck, 3500, 0);
	if (rtl_val > jedec_val) {
		drv_ddr_assert(FILE_CODE, 880);
		if (rtl_val > 0)
			Timings->W2R_DIFFCS_DLY = 0;
		else
			Timings->W2R_DIFFCS_DLY = jedec_val < 0 ? 0 : jedec_val;
	} else if (rtl_val <= 0) {
		Timings->W2R_DIFFCS_DLY = jedec_val < 0 ? 0 : jedec_val;
	} else if (rtl_val >= jedec_val) {
		Timings->W2R_DIFFCS_DLY = 0;
	} else {
		Timings->W2R_DIFFCS_DLY = jedec_val - rtl_val;
	}
	Timings->W2R_DIFFCS_DLY++;
	Timings->R2W_SAMECS_DLY = (uint8_t)(WL + 3 - ODTLON_WR + (WrLinkEccEn(fsp) ? 1 : 0)) -
				  MemCalculateAcTimingNck(Ck, 1500, 0);
}

/* MemLpddr5GetSeqWait: the PI's 20 sequence waits of one FSP, in clocks */
void MemLpddr5GetSeqWait(uint8_t Channel, uint8_t fsp, uint32_t *SeqWaitList)
{
	DFI_TIMING_S *DfiTimings = &GetMemTimingsPtr(Channel, fsp)->DfiTimings;
	uint16_t Wck = GetMemFspCfgPtr(fsp)->MemFreq;
	uint16_t Wck2CkRatio = GetMemFspCfgPtr(fsp)->FreqRatio;
	uint16_t Ck, PI_TDFI_WDQLVL_RW, PI_TDFI_WDQLVL_WR, PI_WDQLVL_RW_SEQ_DLY, PI_WDQLVL_WR_SEQ_DLY;
	uint32_t index;

	Ck = Wck / Wck2CkRatio;
	if ((uint16_t)(Wck % Wck2CkRatio) >= Wck2CkRatio >> 1)
		Ck++;
	PI_TDFI_WDQLVL_RW = DfiTimings->TwdqlvlRw;
	PI_TDFI_WDQLVL_WR = DfiTimings->TwdqlvlWr;
	PI_WDQLVL_RW_SEQ_DLY = PI_TDFI_WDQLVL_RW < 20 ? 0 : PI_TDFI_WDQLVL_RW - 19;
	PI_WDQLVL_WR_SEQ_DLY = PI_TDFI_WDQLVL_WR < 14 ? 0 : PI_TDFI_WDQLVL_WR - 13;
	for (index = 1; index <= 20; index++) {
		switch (index) {
		case 1:
			SeqWaitList[index - 1] = MemCalculateAcTimingNck(Ck, 250000, 0);
			break;
		case 2:
		case 4:
		case 5:
		case 7:
			SeqWaitList[index - 1] = MemCalculateAcTimingNck(Ck, 3000, 0);
			break;
		case 3:
			SeqWaitList[index - 1] = MemCalculateAcTimingNck(Ck, 7500, 4) + 3;
			break;
		case 6:
			SeqWaitList[index - 1] = MemCalculateAcTimingNck(Ck, 2000000, 0);
			break;
		case 8:
			SeqWaitList[index - 1] = MemCalculateAcTimingNck(Ck, 35000, 0);
			break;
		case 9:
			SeqWaitList[index - 1] = MemCalculateAcTimingNck(Ck, 100000, 0) + 1;
			break;
		case 10:
		case 13:
		case 16:
			SeqWaitList[index - 1] = MemCalculateAcTimingNck(Ck, 250000, 0) + 1;
			break;
		case 11:
			SeqWaitList[index - 1] = PI_WDQLVL_WR_SEQ_DLY;
			break;
		case 12:
			SeqWaitList[index - 1] = PI_WDQLVL_RW_SEQ_DLY;
			break;
		case 15:
			SeqWaitList[index - 1] = MemCalculateAcTimingNck(Ck, 200000000, 0) + 1;
			break;
		default:
			SeqWaitList[index - 1] = 0;
			break;
		}
	}
}

/* MemJedecTiming (inlined in CIX's build): the JEDEC timings of one channel
 * and FSP, in clocks */
static inline void MemJedecTiming(uint8_t Channel, uint8_t fsp, JEDEC_TIMING_S *Timings)
{
	MEM_SPD_TIMING_S *SpdTimings = GetMemSpdTimingsPtr();
	uint16_t Wck = GetMemFspCfgPtr(fsp)->MemFreq;
	uint32_t Wck2CkRatio = GetMemFspCfgPtr(fsp)->FreqRatio;
	uint8_t BankArch = GetMemFspCfgPtr(fsp)->BankArchMode;
	uint8_t WLSet = GetBusCfgPtr(fsp)->WLSet;
	uint8_t RLSet = GetBusCfgPtr(fsp)->RLSet;
	uint8_t NWRSet, BLn_Max;
	uint16_t Ck, ODTLoff_RD;
	uint32_t i;

	Ck = Wck / Wck2CkRatio;
	if (Wck % Wck2CkRatio >= Wck2CkRatio >> 1)
		Ck++;
	NWRSet = WrLinkEccEn(fsp);
	BLn_Max = GetBLnMax(fsp);
	for (i = 0; i < sizeof(WriteLatencyTable) / sizeof(WriteLatencyTable[0]); i++) {
		if (WriteLatencyTable[i].Ratio == Wck2CkRatio && Wck <= WriteLatencyTable[i].MaxFreq) {
			Timings->WL = WriteLatencyTable[i].WLBySet[WLSet];
			break;
		}
	}
	for (i = 0; i < sizeof(nWRTable) / sizeof(nWRTable[0]); i++) {
		if (nWRTable[i].Ratio == Wck2CkRatio && Wck <= nWRTable[i].MaxFreq) {
			Timings->NWR = nWRTable[i].nWR[ByteModeEn() ? 1 : 0][NWRSet ? 1 : 0];
			break;
		}
	}
	for (i = 0; i < sizeof(ReadLatencyTable) / sizeof(ReadLatencyTable[0]); i++) {
		if (ReadLatencyTable[i].Ratio == Wck2CkRatio && Wck <= ReadLatencyTable[i].MaxFreq) {
			if (!RdLinkEccEn(fsp)) {
				Timings->RL = ReadLatencyTable[i].LEccOff.RLBySet[RLSet];
				Timings->Trtp = ReadLatencyTable[i].LEccOff.Rbtp;
			} else {
				Timings->RL = ReadLatencyTable[i].LEccOn.RLBySet[RLSet];
				Timings->Trtp = ReadLatencyTable[i].LEccOn.Rbtp;
			}
			break;
		}
	}
	if (Wck2CkRatio == 4) {
		if (Wck <= 800)
			ODTLoff_RD = Timings->RL - 2;
		else if (Wck <= 1867)
			ODTLoff_RD = Timings->RL - 3;
		else
			ODTLoff_RD = Timings->RL - 4;
	} else {
		ODTLoff_RD = 0;
	}
	Timings->Tzqcal = MemCalculateAcTimingNck(Ck, SpdTimings->Tzqcal, 0);
	Timings->Trcd = MemCalculateAcTimingNck(Ck, SpdTimings->Trcdrd, 2);
	Timings->TrcdL = MemCalculateAcTimingNck(Ck, SpdTimings->Trcdwr, 2);
	Timings->TrpAb = MemCalculateAcTimingNck(Ck, SpdTimings->TrpAb, 2);
	Timings->TrpPb = MemCalculateAcTimingNck(Ck, SpdTimings->TrpPb, 2);
	Timings->TrfcAb = MemCalculateAcTimingNck(Ck, SpdTimings->TrfcAb, 2);
	Timings->TrfcPb = MemCalculateAcTimingNck(Ck, SpdTimings->TrfcPb, 2);
	Timings->Tpbr2pbr = MemCalculateAcTimingNck(Ck, SpdTimings->Tpbr2pbr, 0);
	Timings->Tosco = MemCalculateAcTimingNck(Ck, 40000, 8);
	Timings->Txsnr = MemCalculateAcTimingNck(Ck, SpdTimings->TrfcAb + 7500, 4);
	Timings->Txsr = MemCalculateAcTimingNck(Ck, SpdTimings->TrfcAb + 8000, 4);
	Timings->TrasMin = MemCalculateAcTimingNck(Ck, 42000, 3);
	Timings->TrasMax = MemCalculateAcTimingNck(Ck, 70200000, 0);
	Timings->Trc = MemCalculateAcTimingNck(Ck, (MemPbrEn() ? SpdTimings->TrpPb : SpdTimings->TrpAb) + 42000, 5);
	Timings->Tmrri = Timings->Trcd + 2;
	Timings->Tmrd = MemCalculateAcTimingNck(Ck, 14000, 5);
	Timings->Tmod = MemCalculateAcTimingNck(Ck, 14000, 5);
	Timings->Tmrwckel = MemCalculateAcTimingNck(Ck, 14000, 6);
	Timings->Tzqcke = 3;
	Timings->Tckckel = MemCalculateAcTimingNck(Ck, 5000, 3);
	Timings->Tcmdcke = 3;
	Timings->Tcke = MemCalculateAcTimingNck(Ck, 10000, 0);
	Timings->Tckesr = MemCalculateAcTimingNck(Ck, 10000, 0);
	Timings->Tckelpd = MemCalculateAcTimingNck(Ck, 10000, 0);
	Timings->Tescke = 2;
	Timings->Tcscke = 0;
	Timings->Tcsckeh = 0;
	Timings->Tckehcs = 0;
	Timings->Tckelcs = 0;
	Timings->Tckehcmd = 0;
	Timings->Tckelcmd = 0;
	Timings->Tccdmw = 16;
	Timings->TccdL = BankArch == 1 ? 2 : 4;
	/* write recovery and write-to-read: longer in byte mode and with write
	 * link ECC */
	Timings->Twr = MemCalculateAcTimingNck(Ck, ByteModeEn() * 2000 + WrLinkEccEn(fsp) * 4000 + 34000, 3);
	Timings->TwtrL = MemCalculateAcTimingNck(Ck, ByteModeEn() * 2000 + WrLinkEccEn(fsp) * 4000 + 12000, 4);
	if (BankArch == 2)
		Timings->Twtr = MemCalculateAcTimingNck(Ck, ByteModeEn() * 2000 + WrLinkEccEn(fsp) * 4000 + 6250, 4);
	else
		Timings->Twtr = Timings->TwtrL;
	if (BankArch != 0) {
		Timings->Trrd = MemCalculateAcTimingNck(Ck, 5000, 2);
		Timings->TrrdL = MemCalculateAcTimingNck(Ck, 5000, 2);
		Timings->Tfaw = MemCalculateAcTimingNck(Ck, 20000, 0);
	} else {
		Timings->Trrd = MemCalculateAcTimingNck(Ck, 10000, 2);
		Timings->TrrdL = MemCalculateAcTimingNck(Ck, 10000, 2);
		Timings->Tfaw = MemCalculateAcTimingNck(Ck, 40000, 0);
	}
	/* with auto precharge: at least tWR - tRBTP - 2 (CIX compares with
	 * tWR - tRBTP - 1) */
	Timings->TwtrAP = Timings->Twtr < Timings->Twr - Timings->Trtp - 1 ? Timings->Twr - 2 - Timings->Trtp :
									       Timings->Twtr;
	Timings->TwtrLAP = Timings->TwtrL < Timings->Twr - Timings->Trtp - 1 ? Timings->Twr - 2 - Timings->Trtp :
									       Timings->TwtrL;
	Timings->Tref = MemCalculateAcTimingNck(Ck, 3900000, 0) - 8;
	Timings->Tzqlat = MemCalculateAcTimingNck(Ck, 30000, 4);
	Timings->Tpdex = MemCalculateAcTimingNck(Ck, 7000, 3);
	Timings->Tsr = MemCalculateAcTimingNck(Ck, 15000, 2);
	Timings->Tppd = 2;
	Timings->Tpbr2act = MemCalculateAcTimingNck(Ck, 7500, 0);
	Timings->Tcacsh = MemCalculateAcTimingNck(Ck, 1750, 0);
	Timings->Tcsh = MemCalculateAcTimingNck(Ck, 3000, 0);
	Timings->Todtup = MemCalculateAcTimingNck(Ck, 250000, 0);
	Timings->Tcaent = MemCalculateAcTimingNck(Ck, 250000, 0);
	Timings->Tcbtrtw = MemCalculateAcTimingNck(Ck, 20000, 12 / Wck2CkRatio);
	Timings->Tfc = MemCalculateAcTimingNck(Ck, 250000, 0);
	Timings->TvrcgDis = MemCalculateAcTimingNck(Ck, 100000, 0) + 1;
	Timings->TvrcgEn = MemCalculateAcTimingNck(Ck, 250000, 0) + 1;
	Timings->TvrefL = MemCalculateAcTimingNck(Ck, 250000, 0) + 1;
	Timings->TvrefS = 0;
	Timings->Texcke = 3;
	Timings->Tmrw = MemCalculateAcTimingNck(Ck, 10000, 10);
	Timings->Txp = MemCalculateAcTimingNck(Ck, 7500, 4) + 3;
	Timings->Twrlvl_wckoff = MemCalculateAcTimingNck(Ck, 14000, 5) + 3;
	Timings->Tdal = Timings->NWR + Timings->TrpPb;
	Timings->Todtl_2cmd = ODTLoff_RD + 1;
	Timings->Todton_min = MemCalculateAcTimingNck(Ck, 1500, 0);
	Timings->Todthrd = RdPreambleLen() != 0;
	Timings->Todthwr = 0;
	Timings->Trfm = MemCalculateAcTimingNck(Ck, 380000, 2);
	Timings->Trfmsb = MemCalculateAcTimingNck(Ck, 260000, 2);
	Timings->Trfm2act = Timings->Tpbr2act;
	Timings->Trfmth = MemCalculateAcTimingNck(Ck, 480000, 0);
	Timings->Tdqsckmin = MemCalculateAcTimingNck(Ck, 650, 0);
	Timings->Tdqsckmax = MemCalculateAcTimingNck(Ck, 1900, 0);
	Timings->Tdq7hwck = MemCalculateAcTimingNck(Ck, 5000, 3);
	Timings->Tdq7lwck = MemCalculateAcTimingNck(Ck, 5000, 3) + 3;
	Timings->Tdq72dq = MemCalculateAcTimingNck(Ck, 250000, 0);
	Timings->Twck2dq7h = MemCalculateAcTimingNck(Ck, 5000, 3) + 2;
	Timings->Tinit0 = MemCalculateAcTimingNck(Ck, 200000000, 0);
	Timings->Tinit1 = MemCalculateAcTimingNck(Ck, 200000000, 0);
	Timings->Tinit3 = MemCalculateAcTimingNck(Ck, 2000000000, 0);
	Timings->Tinit4 = MemCalculateAcTimingNck(Ck, 50000, 0);
	Timings->Tinit5 = MemCalculateAcTimingNck(Ck, 2000000, 0);
	/* the controller's WCK sync windows; both count the write postamble, as
	 * CIX's do */
	Timings->Wr2WckSyncLost = Timings->WL + 1 + BLn_Max + (WrPostambleEn() ? 1 : 0);
	Timings->Rd2WckSyncLost = Timings->RL + 1 + BLn_Max + (WrPostambleEn() ? 1 : 0);
	Timings->Rd2Odth = Timings->RL - Timings->WL - (RdPreambleLen() ? 1 : 0);
	Timings->Tzqreset = MemCalculateAcTimingNck(Ck, 50000, 3);
	Timings->CKSRE = MemCalculateAcTimingNck(Ck, 5000, 3);
	Timings->CKSRX = 2;
}

/* MemDfiTiming (inlined in CIX's build): the DFI timings of one FSP */
static inline void MemDfiTiming(uint8_t fsp, DFI_TIMING_S *Timings)
{
	uint16_t Wck = GetMemFspCfgPtr(fsp)->MemFreq;
	uint32_t Wck2CkRatio = GetMemFspCfgPtr(fsp)->FreqRatio;
	uint32_t WLSet = GetBusCfgPtr(fsp)->WLSet;
	MEM_BUSCFG_ENTRY_S *BusCfg = GetBusCfgPtr(fsp);
	uint32_t RLSet, i;
	uint16_t Ck;
	uint8_t RL = 0, RL_Eccoff = 0, RL_ADJ, WL = 0, PI_BL_LAT, PI_TWTR;
	uint8_t TWCKENL_RD, TWCKENL_RD_ECCOFF, TWCKENL_WR, TWCKENL_FS = 0, TWCKPRE_STATIC = 0;
	uint8_t TWCKPRE_TOGGLE_WR = 0, TWCKPRE_TOGGLE_RD = 0;

	Ck = Wck / Wck2CkRatio;
	if (Wck % Wck2CkRatio >= Wck2CkRatio >> 1)
		Ck++;
	for (i = 0; i < sizeof(ReadLatencyTable) / sizeof(ReadLatencyTable[0]); i++) {
		if (ReadLatencyTable[i].Ratio == Wck2CkRatio && Wck <= ReadLatencyTable[i].MaxFreq) {
			RLSet = BusCfg->RLSet;
			RL_Eccoff = ReadLatencyTable[i].LEccOff.RLBySet[RLSet];
			if (!RdLinkEccEn(fsp))
				RL = RL_Eccoff;
			else
				RL = ReadLatencyTable[i].LEccOn.RLBySet[RLSet];
			break;
		}
	}
	for (i = 0; i < sizeof(WriteLatencyTable) / sizeof(WriteLatencyTable[0]); i++) {
		if (WriteLatencyTable[i].Ratio == Wck2CkRatio && Wck <= WriteLatencyTable[i].MaxFreq) {
			WL = WriteLatencyTable[i].WLBySet[WLSet];
			break;
		}
	}
	RL_ADJ = GetRlAdj(Wck, Wck2CkRatio, RL);
	PI_BL_LAT = 16 / Wck2CkRatio;
	Timings->TwdqlvlRw = PI_BL_LAT + 2 + RL;
	PI_TWTR = MemCalculateAcTimingNck(Ck, ByteModeEn() * 2000 + WrLinkEccEn(fsp) * 4000 + 12000, 4);
	Timings->TwdqlvlWr = PI_BL_LAT + 4 + WL + PI_TWTR;
	Timings->Upd_ctrlupd_high_threshold = MemCalculateThresholdNck(Ck, 68554, 0);
	Timings->Upd_ctrlupd_norm_threshold = MemCalculateThresholdNck(Ck, 38085, 0);
	Timings->Upd_ctrlupd_sw_promote_threshold = 4;
	Timings->Upd_ctrlupd_timeout = MemCalculateThresholdNck(Ck, 76171, 0);
	Timings->Upd_phyupd_dfi_promote_threshold = 0;
	for (i = 0; i < sizeof(Twck2ckSyncTable) / sizeof(Twck2ckSyncTable[0]); i++) {
		if (Twck2ckSyncTable[i].Ratio == Wck2CkRatio && Wck <= Twck2ckSyncTable[i].MaxFreq) {
			TWCKPRE_STATIC = Twck2ckSyncTable[i].TwckpreStatic;
			TWCKPRE_TOGGLE_RD = Twck2ckSyncTable[i].TwckpreToggleRd;
			TWCKPRE_TOGGLE_WR = Twck2ckSyncTable[i].TwckpreToggleWr;
			TWCKENL_FS = Twck2ckSyncTable[i].TwckenlFs;
			break;
		}
	}
	TWCKENL_RD = RL + 1 - (TWCKPRE_STATIC + TWCKPRE_TOGGLE_RD);
	TWCKENL_WR = WL + 1 - TWCKPRE_STATIC - TWCKPRE_TOGGLE_WR;
	TWCKENL_RD_ECCOFF = RL_Eccoff - (TWCKPRE_STATIC + TWCKPRE_TOGGLE_RD);
	/* in DFI clocks: 2:1 doubles the CK values, 4:1 quadruples them (the
	 * enable latencies one CK later) */
	if (Wck2CkRatio == 2) {
		Timings->Twck_en_rd = TWCKENL_RD << 1;
		Timings->Twck_en_wr = TWCKENL_WR << 1;
		Timings->Twck_en_fs = TWCKENL_FS << 1;
		Timings->Mbist_twckenl_rd_adj = (TWCKENL_RD_ECCOFF + 1) * 2;
		Timings->TwckpreStatic = TWCKPRE_STATIC << 1;
		Timings->TwckpreToggleWr = TWCKPRE_TOGGLE_WR << 1;
		Timings->TwckpreToggleRd = TWCKPRE_TOGGLE_RD << 1;
		Timings->Twck_toggle = TWCKPRE_STATIC << 1;
		Timings->Tcalvl_capture = (MemCalculateAcTimingNck(Ck, 20000, 0) + 19) / 2 + 2;
	} else {
		Timings->Twck_en_rd = (TWCKENL_RD + 1) * 4;
		Timings->Twck_en_wr = (TWCKENL_WR + 1) * 4;
		Timings->Twck_en_fs = (TWCKENL_FS + 1) * 4;
		Timings->Mbist_twckenl_rd_adj = (TWCKENL_RD_ECCOFF + 2) * 4;
		Timings->TwckpreStatic = Wck2CkRatio * TWCKPRE_STATIC;
		Timings->TwckpreToggleWr = Wck2CkRatio * TWCKPRE_TOGGLE_WR;
		Timings->TwckpreToggleRd = Wck2CkRatio * TWCKPRE_TOGGLE_RD;
		Timings->Twck_toggle = Wck2CkRatio * TWCKPRE_STATIC;
		Timings->Tcalvl_capture = MemCalculateAcTimingNck(Ck, 20000, 0) + 19 + 2;
	}
	Timings->Tcalvl_cc = Timings->Tcalvl_capture + GetMemInitPtr()->GlobalTimings.PHY_ADR_CALVL_RESP_WAIT_CNT + 38;
	Timings->Tcalvl_strobe = MemCalculateAcTimingNck(Ck, 2000, 0) + 5;
	Timings->Twck_stop = Wck2CkRatio * (MemCalculateAcTimingNck(Ck, 6000, 2) + 2);
	Timings->Twck_fast_toggle = 4;
	Timings->Twck_dis = 8;
	Timings->Tphymstr_max = MemCalculateAcTimingNck(Ck, GetMemInitPtr()->RanksPerChannel * 124800000, 0);
	Timings->Tphymstr_type0 = Timings->Tphymstr_max;
	Timings->Tphymstr_type1 = Timings->Tphymstr_max;
	Timings->Tphymstr_type2 = Timings->Tphymstr_max;
	Timings->Tphymstr_type3 = Timings->Tphymstr_max;
	Timings->Tphymstr_resp = MemCalculateAcTimingNck(Ck, 27300000, 0);
	Timings->Tctrlupd_max = MemCalculateAcTimingNck(Ck, 7800000, 0);
	Timings->Tctrlupd_interval = MemCalculateAcTimingNck(Ck, 78000000, 0);
	Timings->Tphyupd_resp = MemCalculateAcTimingNck(Ck, 35100000, 0);
	Timings->Tphyupd_type0 = 0x200;
	Timings->Tphyupd_type1 = 0x200;
	Timings->Tphyupd_type2 = 0x200;
	Timings->Tphyupd_type3 = 0x200;
	Timings->Tctrlmsg_resp = 0x20;
	Timings->Tphy_wrdata = 3;
	Timings->Tctrl_delay = 2;
	Timings->Trddata_en = RL_ADJ - 1;
	/* in WCK cycles, x4 */
	Timings->Tphy_rdlat = (MemCalculateAcTimingNck(Wck, 1500, 0) + MemCalculateAcTimingNck(Wck, 7000, 0) + 13 +
			       ((RdPreambleLen() & 3) == 3 ? 2 : 1)) * 4;
	Timings->Twrlvl_ww = ((MemCalculateThresholdNck(Wck, 9000, 0) + 14) >> 1) + 29;
	Timings->Tphy_wrlat = WL * 4 + 1;
	Timings->Tphy_rdcslat = RL << 2;
	Timings->Tphy_wrcslat = WL * 4 - 2;
}

/* MemLpddr5TimingInit: the controller's global timings, the Vref training
 * ranges (+-10 steps around the configured value, at 0 when the Vref is at
 * its minimum), and the JEDEC and DFI timings of every populated channel
 * and FSP */
void MemLpddr5TimingInit(void)
{
	MEMORY_INIT_S *MemInitPtr = GetMemInitPtr();
	LPDDR_TIMING_S *MemTimingsPtr;
	uint32_t Channel;
	uint8_t fsp;

	MemInitPtr->GlobalTimings.TSREF2PHYMSTR = 16;
	MemInitPtr->GlobalTimings.TBST_INT_INTERVAL = 2;
	MemInitPtr->GlobalTimings.TCCD = BankArch8BMode() ? 4 : 2;
	MemInitPtr->GlobalTimings.TAAD = 8;
	MemInitPtr->GlobalTimings.TMRR = 4;
	MemInitPtr->GlobalTimings.TREF_INTERVAL = 5;
	MemInitPtr->GlobalTimings.TRFC_OPT_THRESHOLD = 3;
	MemInitPtr->GlobalTimings.TRAS_LOCKOUT = 1;
	MemInitPtr->GlobalTimings.PHY_ADR_CALVL_RESP_WAIT_CNT = 0;
	for (fsp = 0; fsp < 3; fsp++) {
		MemInitPtr->DqVrefSp[fsp] = GetBusCfgPtr(fsp)->DqVref <= 100 ? -10 : (GetBusCfgPtr(fsp)->DqVref - 100) / 5 - 10;
		MemInitPtr->DqVrefEp[fsp] = GetBusCfgPtr(fsp)->DqVref <= 100 ? 10 : (GetBusCfgPtr(fsp)->DqVref - 100) / 5 + 10;
		MemInitPtr->CaVrefSp[fsp] = GetBusCfgPtr(fsp)->CaVref <= 100 ? -10 : (GetBusCfgPtr(fsp)->CaVref - 100) / 5 - 10;
		MemInitPtr->CaVrefEp[fsp] = GetBusCfgPtr(fsp)->CaVref <= 100 ? 10 : (GetBusCfgPtr(fsp)->CaVref - 100) / 5 + 10;
	}
	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel))
			continue;
		for (fsp = 0; fsp < 3; fsp++) {
			MemTimingsPtr = GetMemTimingsPtr(Channel, fsp);
			MemJedecTiming(Channel, fsp, &MemTimingsPtr->JedecTimings);
			MemDfiTiming(fsp, &MemTimingsPtr->DfiTimings);
		}
	}
}
