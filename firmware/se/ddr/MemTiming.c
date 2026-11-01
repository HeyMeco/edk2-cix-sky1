/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Timing registers (CIX driver/ddr/src/Tech/MemTiming.c): the controller's,
 * the PHY-independent interface's (PI) and the PHY's timing registers of
 * each populated channel, from the timings the technology computes
 * (MemLpddr5Timing.c), and the picosecond-to-clock conversion.
 * Translated from CIX's SE firmware debug build, function by function.
 */
#include "mem_types.h"
#include "mem_regs.h"

/* MemConvertPsToClks: NumberOfPs at Speed MHz in clocks, rounded up when
 * asked (by 999000 / 10^6: a remainder up to 1000 ps x MHz still rounds
 * down), the 64-bit division done by steps of 4294 x 10^6 */
uint32_t MemConvertPsToClks(uint32_t Speed, uint32_t NumberOfPs, bool RoundUp)
{
	uint64_t val64 = (uint64_t)NumberOfPs * Speed + (RoundUp ? 999000 : 0);
	uint32_t step = 4294;
	uint32_t nck = 0;

	while (val64 > 0xffffffff) {
		val64 -= (uint64_t)step * 1000000;
		nck += step;
	}
	return nck + (uint32_t)val64 / 1000000;
}

/* MemCalculateAcTimingNck: a minimum time in clocks, rounded up, at least
 * MinClocks */
uint32_t MemCalculateAcTimingNck(uint32_t Speed, uint32_t MinCycleTime, uint32_t MinClocks)
{
	uint32_t nCK = MemConvertPsToClks(Speed, MinCycleTime, true);

	if (nCK < MinClocks)
		nCK = MinClocks;
	return nCK;
}

/* MemCalculateThresholdNck: the same, rounded down */
uint32_t MemCalculateThresholdNck(uint32_t Speed, uint32_t MinCycleTime, uint32_t MinClocks)
{
	uint32_t nCK = MemConvertPsToClks(Speed, MinCycleTime, false);

	if (nCK < MinClocks)
		nCK = MinClocks;
	return nCK;
}

/* MemPiTimingCfg: the PI's timings of FSP 1 and 2, and its sequence waits
 * of all three FSPs */
void MemPiTimingCfg(void)
{
	LPDDR_TIMING_S *MemFsp0TimingsPtr, *MemFsp1TimingsPtr, *MemFsp2TimingsPtr;
	PI_TIMING_S MemFsp0PiTimings, MemFsp1PiTimings, MemFsp2PiTimings;
	uint32_t PiSeqWaitList[20];
	uint8_t Channel, index, fsp;
	DENALI_PI_164 ddrpi_164;
	DENALI_PI_406 ddrpi_406;
	DENALI_PI_408 ddrpi_408;
	DENALI_PI_409 ddrpi_409;
	DENALI_PI_413 ddrpi_413;
	DENALI_PI_414 ddrpi_414;
	DENALI_PI_415 ddrpi_415;
	DENALI_PI_416 ddrpi_416;
	DENALI_PI_417 ddrpi_417;
	DENALI_PI_418 ddrpi_418;
	DENALI_PI_420 ddrpi_420;
	DENALI_PI_421 ddrpi_421;
	DENALI_PI_422 ddrpi_422;
	DENALI_PI_423 ddrpi_423;
	DENALI_PI_426 ddrpi_426;
	DENALI_PI_427 ddrpi_427;
	DENALI_PI_428 ddrpi_428;
	DENALI_PI_429 ddrpi_429;
	DENALI_PI_430 ddrpi_430;
	DENALI_PI_437 ddrpi_437;
	DENALI_PI_438 ddrpi_438;
	DENALI_PI_439 ddrpi_439;
	DENALI_PI_441 ddrpi_441;
	DENALI_PI_442 ddrpi_442;
	DENALI_PI_444 ddrpi_444;
	DENALI_PI_445 ddrpi_445;
	DENALI_PI_446 ddrpi_446;
	DENALI_PI_449 ddrpi_449;
	DENALI_PI_450 ddrpi_450;
	DENALI_PI_451 ddrpi_451;
	DENALI_PI_452 ddrpi_452;
	DENALI_PI_453 ddrpi_453;
	DENALI_PI_457 ddrpi_457;
	DENALI_PI_458 ddrpi_458;
	DENALI_PI_465 ddrpi_465;
	DENALI_PI_466 ddrpi_466;
	DENALI_PI_467 ddrpi_467;
	DENALI_PI_472 ddrpi_472;
	DENALI_PI_474 ddrpi_474;
	DENALI_PI_475 ddrpi_475;
	DENALI_PI_477 ddrpi_477;
	DENALI_PI_484 ddrpi_484;
	DENALI_PI_485 ddrpi_485;
	DENALI_PI_486 ddrpi_486;
	DENALI_PI_487 ddrpi_487;
	DENALI_PI_488 ddrpi_488;
	DENALI_PI_489 ddrpi_489;
	DENALI_PI_490 ddrpi_490;
	DENALI_PI_491 ddrpi_491;
	DENALI_PI_492 ddrpi_492;
	DENALI_PI_493 ddrpi_493;
	DENALI_PI_499 ddrpi_499;
	DENALI_PI_500 ddrpi_500;
	DENALI_PI_501 ddrpi_501;
	DENALI_PI_512 ddrpi_512;
	DENALI_PI_518 ddrpi_518;
	DENALI_PI_521 ddrpi_521;
	DENALI_PI_522 ddrpi_522;
	DENALI_PI_523 ddrpi_523;
	DENALI_PI_524 ddrpi_524;
	DENALI_PI_525 ddrpi_525;
	DENALI_PI_528 ddrpi_528;
	DENALI_PI_529 ddrpi_529;
	DENALI_PI_530 ddrpi_530;
	DENALI_PI_531 ddrpi_531;
	DENALI_PI_533 ddrpi_533;
	DENALI_PI_534 ddrpi_534;
	DENALI_PI_535 ddrpi_535;
	DENALI_PI_536 ddrpi_536;
	DENALI_PI_539 ddrpi_539;

	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel))
			continue;
		MemFsp0TimingsPtr = GetMemTimingsPtr(Channel, 0);
		MemFsp1TimingsPtr = GetMemTimingsPtr(Channel, 1);
		MemFsp2TimingsPtr = GetMemTimingsPtr(Channel, 2);
		MemGetTechFuncPtr()->MemGetPiTiming(Channel, 0, &MemFsp0PiTimings);
		MemGetTechFuncPtr()->MemGetPiTiming(Channel, 1, &MemFsp1PiTimings);
		MemGetTechFuncPtr()->MemGetPiTiming(Channel, 2, &MemFsp2PiTimings);
		ddrpi_164.value = MemDdrcRegRead(DENALI_PI(164), Channel);
		ddrpi_164.fields.PI_TCKCKEL_F1 = MemFsp1TimingsPtr->JedecTimings.Tckckel;
		ddrpi_164.fields.PI_TCKCKEL_F2 = MemFsp2TimingsPtr->JedecTimings.Tckckel;
		MemDdrcRegWrite(DENALI_PI(164), Channel, ddrpi_164.value);
		ddrpi_406.value = MemDdrcRegRead(DENALI_PI(406), Channel);
		ddrpi_406.fields.PI_TSDO_F1 = MemFsp1TimingsPtr->JedecTimings.Tsdo;
		ddrpi_406.fields.PI_TSDO_F2 = MemFsp2TimingsPtr->JedecTimings.Tsdo;
		MemDdrcRegWrite(DENALI_PI(406), Channel, ddrpi_406.value);
		ddrpi_408.value = MemDdrcRegRead(DENALI_PI(408), Channel);
		ddrpi_408.fields.PI_TDELAY_RDWR_2_BUS_IDLE_F1 = MemFsp1PiTimings.PI_TDELAY_RDWR_2_BUS_IDLE;
		MemDdrcRegWrite(DENALI_PI(408), Channel, ddrpi_408.value);
		ddrpi_409.value = MemDdrcRegRead(DENALI_PI(409), Channel);
		ddrpi_409.fields.PI_TDELAY_RDWR_2_BUS_IDLE_F2 = MemFsp2PiTimings.PI_TDELAY_RDWR_2_BUS_IDLE;
		MemDdrcRegWrite(DENALI_PI(409), Channel, ddrpi_409.value);
		ddrpi_413.value = MemDdrcRegRead(DENALI_PI(413), Channel);
		ddrpi_413.fields.PI_WRLAT_F1 = MemFsp1TimingsPtr->JedecTimings.WL;
		ddrpi_413.fields.PI_TWCKENL_WR_ADJ_F1 = MemFsp1TimingsPtr->DfiTimings.Twck_en_wr;
		MemDdrcRegWrite(DENALI_PI(413), Channel, ddrpi_413.value);
		ddrpi_414.value = MemDdrcRegRead(DENALI_PI(414), Channel);
		ddrpi_414.fields.PI_TWCKENL_RD_ADJ_F1 = MemFsp1TimingsPtr->DfiTimings.Twck_en_rd;
		ddrpi_414.fields.PI_TWCKPRE_STATIC_F1 = MemFsp1TimingsPtr->DfiTimings.TwckpreStatic;
		ddrpi_414.fields.PI_TWCKPRE_TOGGLE_WR_F1 = MemFsp1TimingsPtr->DfiTimings.TwckpreToggleWr;
		ddrpi_414.fields.PI_TWCKPRE_TOGGLE_RD_F1 = MemFsp1TimingsPtr->DfiTimings.TwckpreToggleRd;
		MemDdrcRegWrite(DENALI_PI(414), Channel, ddrpi_414.value);
		ddrpi_415.value = MemDdrcRegRead(DENALI_PI(415), Channel);
		ddrpi_415.fields.PI_CASLAT_F1 = MemFsp1TimingsPtr->JedecTimings.RL;
		ddrpi_415.fields.PI_TWCKENL_FS_ADJ_F1 = MemFsp1TimingsPtr->DfiTimings.Twck_en_fs;
		ddrpi_415.fields.PI_WCK_ACTIVE_RD_F1 = MemFsp1PiTimings.PI_WCK_ACTIVE_RD;
		ddrpi_415.fields.PI_WCK_ACTIVE_WR_F1 = MemFsp1PiTimings.PI_WCK_ACTIVE_WR;
		MemDdrcRegWrite(DENALI_PI(415), Channel, ddrpi_415.value);
		ddrpi_416.value = MemDdrcRegRead(DENALI_PI(416), Channel);
		ddrpi_416.fields.PI_WRLAT_F2 = MemFsp2TimingsPtr->JedecTimings.WL;
		ddrpi_416.fields.PI_TWCKPRE_STATIC_F2 = MemFsp2TimingsPtr->DfiTimings.TwckpreStatic;
		ddrpi_416.fields.PI_TWCKENL_RD_ADJ_F2 = MemFsp2TimingsPtr->DfiTimings.Twck_en_rd;
		ddrpi_416.fields.PI_TWCKENL_WR_ADJ_F2 = MemFsp2TimingsPtr->DfiTimings.Twck_en_wr;
		MemDdrcRegWrite(DENALI_PI(416), Channel, ddrpi_416.value);
		ddrpi_417.value = MemDdrcRegRead(DENALI_PI(417), Channel);
		ddrpi_417.fields.PI_TWCKPRE_TOGGLE_WR_F2 = MemFsp2TimingsPtr->DfiTimings.TwckpreToggleWr;
		ddrpi_417.fields.PI_TWCKPRE_TOGGLE_RD_F2 = MemFsp2TimingsPtr->DfiTimings.TwckpreToggleRd;
		ddrpi_417.fields.PI_WCK_ACTIVE_RD_F2 = MemFsp2PiTimings.PI_WCK_ACTIVE_RD;
		ddrpi_417.fields.PI_WCK_ACTIVE_WR_F2 = MemFsp2PiTimings.PI_WCK_ACTIVE_WR;
		MemDdrcRegWrite(DENALI_PI(417), Channel, ddrpi_417.value);
		ddrpi_418.value = MemDdrcRegRead(DENALI_PI(418), Channel);
		ddrpi_418.fields.PI_CASLAT_F2 = MemFsp2TimingsPtr->JedecTimings.RL;
		ddrpi_418.fields.PI_TWCKENL_FS_ADJ_F2 = MemFsp2TimingsPtr->DfiTimings.Twck_en_fs;
		MemDdrcRegWrite(DENALI_PI(418), Channel, ddrpi_418.value);
		ddrpi_420.value = MemDdrcRegRead(DENALI_PI(420), Channel);
		ddrpi_420.fields.PI_TRFC_F1 = MemFsp1TimingsPtr->JedecTimings.TrfcAb;
		MemDdrcRegWrite(DENALI_PI(420), Channel, ddrpi_420.value);
		ddrpi_421.value = MemDdrcRegRead(DENALI_PI(421), Channel);
		ddrpi_421.fields.PI_TREF_F1 = MemFsp1TimingsPtr->JedecTimings.Tref;
		MemDdrcRegWrite(DENALI_PI(421), Channel, ddrpi_421.value);
		ddrpi_422.value = MemDdrcRegRead(DENALI_PI(422), Channel);
		ddrpi_422.fields.PI_TRFC_F2 = MemFsp2TimingsPtr->JedecTimings.TrfcAb;
		MemDdrcRegWrite(DENALI_PI(422), Channel, ddrpi_422.value);
		ddrpi_423.value = MemDdrcRegRead(DENALI_PI(423), Channel);
		ddrpi_423.fields.PI_TREF_F2 = MemFsp2TimingsPtr->JedecTimings.Tref;
		MemDdrcRegWrite(DENALI_PI(423), Channel, ddrpi_423.value);
		ddrpi_426.value = MemDdrcRegRead(DENALI_PI(426), Channel);
		ddrpi_426.fields.PI_WRLVL_WCKOFF_F1 = MemFsp1TimingsPtr->JedecTimings.Twrlvl_wckoff;
		ddrpi_426.fields.PI_TDFI_WRLVL_WW_F1 = MemFsp1TimingsPtr->DfiTimings.Twrlvl_ww;
		MemDdrcRegWrite(DENALI_PI(426), Channel, ddrpi_426.value);
		ddrpi_427.value = MemDdrcRegRead(DENALI_PI(427), Channel);
		ddrpi_427.fields.PI_WRLVL_WCKOFF_F2 = MemFsp2TimingsPtr->JedecTimings.Twrlvl_wckoff;
		ddrpi_427.fields.PI_TDFI_WRLVL_WW_F2 = MemFsp2TimingsPtr->DfiTimings.Twrlvl_ww;
		MemDdrcRegWrite(DENALI_PI(427), Channel, ddrpi_427.value);
		ddrpi_428.value = MemDdrcRegRead(DENALI_PI(428), Channel);
		ddrpi_428.fields.PI_TODTL_2CMD_F0 = MemFsp0TimingsPtr->JedecTimings.Todtl_2cmd;
		ddrpi_428.fields.PI_TODTL_2CMD_F1 = MemFsp1TimingsPtr->JedecTimings.Todtl_2cmd;
		MemDdrcRegWrite(DENALI_PI(428), Channel, ddrpi_428.value);
		ddrpi_429.value = MemDdrcRegRead(DENALI_PI(429), Channel);
		ddrpi_429.fields.PI_TODTL_2CMD_F2 = MemFsp2TimingsPtr->JedecTimings.Todtl_2cmd;
		MemDdrcRegWrite(DENALI_PI(429), Channel, ddrpi_429.value);
		ddrpi_430.value = MemDdrcRegRead(DENALI_PI(430), Channel);
		ddrpi_430.fields.PI_TODTON_MIN_F1 = MemFsp1TimingsPtr->JedecTimings.Todton_min;
		ddrpi_430.fields.PI_TODTON_MIN_F2 = MemFsp2TimingsPtr->JedecTimings.Todton_min;
		MemDdrcRegWrite(DENALI_PI(430), Channel, ddrpi_430.value);
		ddrpi_437.value = MemDdrcRegRead(DENALI_PI(437), Channel);
		ddrpi_437.fields.PI_RDLAT_ADJ_F1 = MemFsp1PiTimings.PI_RDLAT_ADJ;
		MemDdrcRegWrite(DENALI_PI(437), Channel, ddrpi_437.value);
		ddrpi_438.value = MemDdrcRegRead(DENALI_PI(438), Channel);
		ddrpi_438.fields.PI_RDLAT_ADJ_F2 = MemFsp2PiTimings.PI_RDLAT_ADJ;
		ddrpi_438.fields.PI_WRLAT_ADJ_F1 = MemFsp1PiTimings.PI_WRLAT_ADJ;
		MemDdrcRegWrite(DENALI_PI(438), Channel, ddrpi_438.value);
		ddrpi_439.value = MemDdrcRegRead(DENALI_PI(439), Channel);
		ddrpi_439.fields.PI_WRLAT_ADJ_F2 = MemFsp2PiTimings.PI_WRLAT_ADJ;
		MemDdrcRegWrite(DENALI_PI(439), Channel, ddrpi_439.value);
		ddrpi_441.value = MemDdrcRegRead(DENALI_PI(441), Channel);
		ddrpi_441.fields.PI_TDFI_CALVL_CC_F1 = MemFsp1TimingsPtr->DfiTimings.Tcalvl_cc;
		ddrpi_441.fields.PI_TDFI_CALVL_CAPTURE_F1 = MemFsp1TimingsPtr->DfiTimings.Tcalvl_capture;
		MemDdrcRegWrite(DENALI_PI(441), Channel, ddrpi_441.value);
		ddrpi_442.value = MemDdrcRegRead(DENALI_PI(442), Channel);
		ddrpi_442.fields.PI_TDFI_CALVL_CC_F2 = MemFsp2TimingsPtr->DfiTimings.Tcalvl_cc;
		ddrpi_442.fields.PI_TDFI_CALVL_CAPTURE_F2 = MemFsp2TimingsPtr->DfiTimings.Tcalvl_capture;
		MemDdrcRegWrite(DENALI_PI(442), Channel, ddrpi_442.value);
		ddrpi_444.value = MemDdrcRegRead(DENALI_PI(444), Channel);
		ddrpi_444.fields.PI_TMRZ_F1 = MemFsp1TimingsPtr->JedecTimings.Tmrz;
		MemDdrcRegWrite(DENALI_PI(444), Channel, ddrpi_444.value);
		ddrpi_445.value = MemDdrcRegRead(DENALI_PI(445), Channel);
		ddrpi_445.fields.PI_TCAENT_F1 = MemFsp1TimingsPtr->JedecTimings.Tcaent;
		ddrpi_445.fields.PI_TMRZ_F2 = MemFsp2TimingsPtr->JedecTimings.Tmrz;
		MemDdrcRegWrite(DENALI_PI(445), Channel, ddrpi_445.value);
		ddrpi_446.value = MemDdrcRegRead(DENALI_PI(446), Channel);
		ddrpi_446.fields.PI_TCAENT_F2 = MemFsp2TimingsPtr->JedecTimings.Tcaent;
		MemDdrcRegWrite(DENALI_PI(446), Channel, ddrpi_446.value);
		ddrpi_449.value = MemDdrcRegRead(DENALI_PI(449), Channel);
		ddrpi_449.fields.PI_TVREF_SHORT_F1 = MemFsp1TimingsPtr->JedecTimings.TvrefS;
		MemDdrcRegWrite(DENALI_PI(449), Channel, ddrpi_449.value);
		ddrpi_450.value = MemDdrcRegRead(DENALI_PI(450), Channel);
		ddrpi_450.fields.PI_TVRCG_ENABLE_F1 = MemFsp1TimingsPtr->JedecTimings.TvrcgEn;
		ddrpi_450.fields.PI_TVREF_LONG_F1 = MemFsp1TimingsPtr->JedecTimings.TvrefL;
		MemDdrcRegWrite(DENALI_PI(450), Channel, ddrpi_450.value);
		ddrpi_451.value = MemDdrcRegRead(DENALI_PI(451), Channel);
		ddrpi_451.fields.PI_TVRCG_DISABLE_F1 = MemFsp1TimingsPtr->JedecTimings.TvrcgDis;
		MemDdrcRegWrite(DENALI_PI(451), Channel, ddrpi_451.value);
		ddrpi_452.value = MemDdrcRegRead(DENALI_PI(452), Channel);
		ddrpi_452.fields.PI_TVREF_LONG_F2 = MemFsp2TimingsPtr->JedecTimings.TvrefL;
		ddrpi_452.fields.PI_TVREF_SHORT_F2 = MemFsp2TimingsPtr->JedecTimings.TvrefS;
		MemDdrcRegWrite(DENALI_PI(452), Channel, ddrpi_452.value);
		ddrpi_453.value = MemDdrcRegRead(DENALI_PI(453), Channel);
		ddrpi_453.fields.PI_TVRCG_DISABLE_F2 = MemFsp2TimingsPtr->JedecTimings.TvrcgDis;
		ddrpi_453.fields.PI_TVRCG_ENABLE_F2 = MemFsp2TimingsPtr->JedecTimings.TvrcgEn;
		MemDdrcRegWrite(DENALI_PI(453), Channel, ddrpi_453.value);
		ddrpi_457.value = MemDdrcRegRead(DENALI_PI(457), Channel);
		ddrpi_457.fields.PI_TXP_F1 = MemFsp1TimingsPtr->JedecTimings.Txp;
		ddrpi_457.fields.PI_TMRWCKEL_F1 = MemFsp1PiTimings.PI_TMRWCKEL;
		ddrpi_457.fields.PI_TDFI_CALVL_STROBE_F1 = MemFsp1TimingsPtr->DfiTimings.Tcalvl_strobe;
		MemDdrcRegWrite(DENALI_PI(457), Channel, ddrpi_457.value);
		ddrpi_458.value = MemDdrcRegRead(DENALI_PI(458), Channel);
		ddrpi_458.fields.PI_TXP_F2 = MemFsp2TimingsPtr->JedecTimings.Txp;
		ddrpi_458.fields.PI_TMRWCKEL_F2 = MemFsp2PiTimings.PI_TMRWCKEL;
		ddrpi_458.fields.PI_TDFI_CALVL_STROBE_F2 = MemFsp2TimingsPtr->DfiTimings.Tcalvl_strobe;
		MemDdrcRegWrite(DENALI_PI(458), Channel, ddrpi_458.value);
		ddrpi_465.value = MemDdrcRegRead(DENALI_PI(465), Channel);
		ddrpi_465.fields.PI_TCKEHDQS_F1 = MemFsp1PiTimings.PI_TCKEHDQS;
		MemDdrcRegWrite(DENALI_PI(465), Channel, ddrpi_465.value);
		ddrpi_466.value = MemDdrcRegRead(DENALI_PI(466), Channel);
		ddrpi_466.fields.PI_TFC_F1 = MemFsp1TimingsPtr->JedecTimings.Tfc;
		ddrpi_466.fields.PI_TCKEHDQS_F2 = MemFsp2PiTimings.PI_TCKEHDQS;
		MemDdrcRegWrite(DENALI_PI(466), Channel, ddrpi_466.value);
		ddrpi_467.value = MemDdrcRegRead(DENALI_PI(467), Channel);
		ddrpi_467.fields.PI_TFC_F2 = MemFsp2TimingsPtr->JedecTimings.Tfc;
		MemDdrcRegWrite(DENALI_PI(467), Channel, ddrpi_467.value);
		ddrpi_472.value = 0;
		ddrpi_472.fields.PI_TDFI_WDQLVL_WR_F1 = MemFsp1TimingsPtr->DfiTimings.TwdqlvlWr;
		ddrpi_472.fields.PI_TDFI_WDQLVL_RW_F1 = MemFsp1TimingsPtr->DfiTimings.TwdqlvlRw;
		MemDdrcRegWrite(DENALI_PI(472), Channel, ddrpi_472.value);
		ddrpi_474.value = MemDdrcRegRead(DENALI_PI(474), Channel);
		ddrpi_474.fields.PI_MBIST_TWCKENL_RD_ADJ_F1 = MemFsp1TimingsPtr->DfiTimings.Mbist_twckenl_rd_adj;
		ddrpi_474.fields.PI_MBIST_RDLAT_ADJ_F1 = MemFsp1PiTimings.PI_MBIST_RDLAT_ADJ;
		MemDdrcRegWrite(DENALI_PI(474), Channel, ddrpi_474.value);
		ddrpi_475.value = 0;
		ddrpi_475.fields.PI_TDFI_WDQLVL_WR_F2 = MemFsp2TimingsPtr->DfiTimings.TwdqlvlWr;
		ddrpi_475.fields.PI_TDFI_WDQLVL_RW_F2 = MemFsp2TimingsPtr->DfiTimings.TwdqlvlRw;
		MemDdrcRegWrite(DENALI_PI(475), Channel, ddrpi_475.value);
		ddrpi_477.value = MemDdrcRegRead(DENALI_PI(477), Channel);
		ddrpi_477.fields.PI_MBIST_TWCKENL_RD_ADJ_F2 = MemFsp2TimingsPtr->DfiTimings.Mbist_twckenl_rd_adj;
		ddrpi_477.fields.PI_MBIST_RDLAT_ADJ_F2 = MemFsp2PiTimings.PI_MBIST_RDLAT_ADJ;
		MemDdrcRegWrite(DENALI_PI(477), Channel, ddrpi_477.value);
		ddrpi_484.value = MemDdrcRegRead(DENALI_PI(484), Channel);
		ddrpi_484.fields.PI_TRCD_F1 = MemFsp1TimingsPtr->JedecTimings.Trcd;
		ddrpi_484.fields.PI_TRP_F1 = MemFsp1TimingsPtr->JedecTimings.TrpAb;
		ddrpi_484.fields.PI_TRTP_F1 = MemFsp1TimingsPtr->JedecTimings.Trtp;
		MemDdrcRegWrite(DENALI_PI(484), Channel, ddrpi_484.value);
		ddrpi_485.value = MemDdrcRegRead(DENALI_PI(485), Channel);
		ddrpi_485.fields.PI_TCCD_L_F1 = MemFsp1PiTimings.PI_TCCD_L;
		ddrpi_485.fields.PI_TCCD_S_F1 = MemFsp1PiTimings.PI_TCCD_S;
		ddrpi_485.fields.PI_TWTR_L_F1 = MemFsp1TimingsPtr->JedecTimings.TwtrL;
		ddrpi_485.fields.PI_TWTR_S_F1 = MemFsp1TimingsPtr->JedecTimings.TwtrL;
		MemDdrcRegWrite(DENALI_PI(485), Channel, ddrpi_485.value);
		ddrpi_486.value = MemDdrcRegRead(DENALI_PI(486), Channel);
		ddrpi_486.fields.PI_TRAS_MAX_F1 = MemFsp1TimingsPtr->JedecTimings.TrasMax * 9 / 10;
		ddrpi_486.fields.PI_TWR_F1 = MemFsp1TimingsPtr->JedecTimings.Twr;
		ddrpi_486.fields.PI_TWTR_F1 = MemFsp1TimingsPtr->JedecTimings.TwtrL;
		MemDdrcRegWrite(DENALI_PI(486), Channel, ddrpi_486.value);
		ddrpi_487.value = MemDdrcRegRead(DENALI_PI(487), Channel);
		ddrpi_487.fields.PI_TCCDMW_F1 = MemFsp1PiTimings.PI_TCCDMW;
		ddrpi_487.fields.PI_TDQSCK_MAX_F1 = MemFsp1TimingsPtr->JedecTimings.Tdqsckmax;
		ddrpi_487.fields.PI_TRAS_MIN_F1 = MemFsp1TimingsPtr->JedecTimings.TrasMin;
		MemDdrcRegWrite(DENALI_PI(487), Channel, ddrpi_487.value);
		ddrpi_488.value = MemDdrcRegRead(DENALI_PI(488), Channel);
		ddrpi_488.fields.PI_TMRD_F1 = MemFsp1TimingsPtr->JedecTimings.Tmrd + 1;
		ddrpi_488.fields.PI_TRTP_F2 = MemFsp2TimingsPtr->JedecTimings.Trtp;
		ddrpi_488.fields.PI_TSR_F1 = MemFsp1TimingsPtr->JedecTimings.Tsr;
		ddrpi_488.fields.PI_TMRW_F1 = MemFsp1PiTimings.PI_TMRW;
		MemDdrcRegWrite(DENALI_PI(488), Channel, ddrpi_488.value);
		ddrpi_489.value = MemDdrcRegRead(DENALI_PI(489), Channel);
		ddrpi_489.fields.PI_TCCD_L_F2 = MemFsp2PiTimings.PI_TCCD_L;
		ddrpi_489.fields.PI_TRCD_F2 = MemFsp2TimingsPtr->JedecTimings.Trcd;
		ddrpi_489.fields.PI_TRP_F2 = MemFsp2TimingsPtr->JedecTimings.TrpAb;
		ddrpi_489.fields.PI_TWTR_S_F2 = MemFsp2TimingsPtr->JedecTimings.TwtrL;
		MemDdrcRegWrite(DENALI_PI(489), Channel, ddrpi_489.value);
		ddrpi_490.value = MemDdrcRegRead(DENALI_PI(490), Channel);
		ddrpi_490.fields.PI_TCCD_S_F2 = MemFsp2PiTimings.PI_TCCD_S;
		ddrpi_490.fields.PI_TWR_F2 = MemFsp2TimingsPtr->JedecTimings.Twr;
		ddrpi_490.fields.PI_TWTR_F2 = MemFsp2TimingsPtr->JedecTimings.TwtrL;
		ddrpi_490.fields.PI_TWTR_L_F2 = MemFsp2TimingsPtr->JedecTimings.TwtrL;
		MemDdrcRegWrite(DENALI_PI(490), Channel, ddrpi_490.value);
		ddrpi_491.value = MemDdrcRegRead(DENALI_PI(491), Channel);
		ddrpi_491.fields.PI_TRAS_MAX_F2 = MemFsp2TimingsPtr->JedecTimings.TrasMax * 9 / 10;
		ddrpi_491.fields.PI_TRAS_MIN_F2 = MemFsp2TimingsPtr->JedecTimings.TrasMin;
		MemDdrcRegWrite(DENALI_PI(491), Channel, ddrpi_491.value);
		ddrpi_492.value = MemDdrcRegRead(DENALI_PI(492), Channel);
		ddrpi_492.fields.PI_TCCDMW_F2 = MemFsp2PiTimings.PI_TCCDMW;
		ddrpi_492.fields.PI_TDQSCK_MAX_F2 = MemFsp2TimingsPtr->JedecTimings.Tdqsckmax;
		ddrpi_492.fields.PI_TMRD_F2 = MemFsp2TimingsPtr->JedecTimings.Tmrd + 1;
		ddrpi_492.fields.PI_TSR_F2 = MemFsp2TimingsPtr->JedecTimings.Tsr;
		MemDdrcRegWrite(DENALI_PI(492), Channel, ddrpi_492.value);
		ddrpi_493.value = MemDdrcRegRead(DENALI_PI(493), Channel);
		ddrpi_493.fields.PI_TMRW_F2 = MemFsp2PiTimings.PI_TMRW;
		MemDdrcRegWrite(DENALI_PI(493), Channel, ddrpi_493.value);
		MemDdrcRegWrite(DENALI_PI(495), Channel, MemFsp1TimingsPtr->DfiTimings.Tctrlupd_max);
		MemDdrcRegWrite(DENALI_PI(496), Channel, MemFsp1TimingsPtr->DfiTimings.Tctrlupd_interval);
		MemDdrcRegWrite(DENALI_PI(497), Channel, MemFsp2TimingsPtr->DfiTimings.Tctrlupd_max);
		MemDdrcRegWrite(DENALI_PI(498), Channel, MemFsp2TimingsPtr->DfiTimings.Tctrlupd_interval);
		ddrpi_499.value = MemDdrcRegRead(DENALI_PI(499), Channel);
		ddrpi_499.fields.PI_TXSR_F1 = MemFsp1TimingsPtr->JedecTimings.Txsr;
		MemDdrcRegWrite(DENALI_PI(499), Channel, ddrpi_499.value);
		ddrpi_500.value = MemDdrcRegRead(DENALI_PI(500), Channel);
		ddrpi_500.fields.PI_TXSR_F2 = MemFsp2TimingsPtr->JedecTimings.Txsr;
		ddrpi_500.fields.PI_TEXCKE_F1 = MemFsp1TimingsPtr->JedecTimings.Texcke;
		MemDdrcRegWrite(DENALI_PI(500), Channel, ddrpi_500.value);
		ddrpi_501.value = MemDdrcRegRead(DENALI_PI(501), Channel);
		ddrpi_501.fields.PI_TEXCKE_F2 = MemFsp2TimingsPtr->JedecTimings.Texcke;
		MemDdrcRegWrite(DENALI_PI(501), Channel, ddrpi_501.value);
		MemDdrcRegWrite(DENALI_PI(507), Channel, MemFsp1TimingsPtr->JedecTimings.Tinit0);
		MemDdrcRegWrite(DENALI_PI(508), Channel, MemFsp1TimingsPtr->JedecTimings.Tinit1);
		MemDdrcRegWrite(DENALI_PI(509), Channel, MemFsp1TimingsPtr->JedecTimings.Tinit3);
		MemDdrcRegWrite(DENALI_PI(510), Channel, MemFsp1TimingsPtr->JedecTimings.Tinit4);
		MemDdrcRegWrite(DENALI_PI(511), Channel, MemFsp1TimingsPtr->JedecTimings.Tinit5);
		ddrpi_512.value = MemDdrcRegRead(DENALI_PI(512), Channel);
		ddrpi_512.fields.PI_TXSNR_F1 = MemFsp1TimingsPtr->JedecTimings.Txsnr;
		MemDdrcRegWrite(DENALI_PI(512), Channel, ddrpi_512.value);
		MemDdrcRegWrite(DENALI_PI(513), Channel, MemFsp2TimingsPtr->JedecTimings.Tinit0);
		MemDdrcRegWrite(DENALI_PI(514), Channel, MemFsp2TimingsPtr->JedecTimings.Tinit1);
		MemDdrcRegWrite(DENALI_PI(515), Channel, MemFsp2TimingsPtr->JedecTimings.Tinit3);
		MemDdrcRegWrite(DENALI_PI(516), Channel, MemFsp2TimingsPtr->JedecTimings.Tinit4);
		MemDdrcRegWrite(DENALI_PI(517), Channel, MemFsp2TimingsPtr->JedecTimings.Tinit5);
		ddrpi_518.value = MemDdrcRegRead(DENALI_PI(518), Channel);
		ddrpi_518.fields.PI_TXSNR_F2 = MemFsp2TimingsPtr->JedecTimings.Txsnr;
		MemDdrcRegWrite(DENALI_PI(518), Channel, ddrpi_518.value);
		ddrpi_521.value = MemDdrcRegRead(DENALI_PI(521), Channel);
		ddrpi_521.fields.PI_TZQCAL_F1 = MemFsp1TimingsPtr->JedecTimings.Tzqcal;
		MemDdrcRegWrite(DENALI_PI(521), Channel, ddrpi_521.value);
		ddrpi_522.value = MemDdrcRegRead(DENALI_PI(522), Channel);
		ddrpi_522.fields.PI_TZQLAT_F1 = MemFsp1TimingsPtr->JedecTimings.Tzqlat;
		MemDdrcRegWrite(DENALI_PI(522), Channel, ddrpi_522.value);
		ddrpi_523.value = MemDdrcRegRead(DENALI_PI(523), Channel);
		ddrpi_523.fields.PI_TZQCAL_F2 = MemFsp2TimingsPtr->JedecTimings.Tzqcal;
		MemDdrcRegWrite(DENALI_PI(523), Channel, ddrpi_523.value);
		ddrpi_524.value = MemDdrcRegRead(DENALI_PI(524), Channel);
		ddrpi_524.fields.PI_TZQLAT_F2 = MemFsp2TimingsPtr->JedecTimings.Tzqlat;
		MemDdrcRegWrite(DENALI_PI(524), Channel, ddrpi_524.value);
		ddrpi_525.value = MemDdrcRegRead(DENALI_PI(525), Channel);
		ddrpi_525.fields.PI_RESERVED43 = MemFsp1PiTimings.PI_525_RSV;
		ddrpi_525.fields.PI_RESERVED44 = MemFsp2PiTimings.PI_525_RSV;
		MemDdrcRegWrite(DENALI_PI(525), Channel, ddrpi_525.value);
		ddrpi_528.value = MemDdrcRegRead(DENALI_PI(528), Channel);
		ddrpi_528.fields.PI_TDQ7HWCK_F1 = MemFsp1TimingsPtr->JedecTimings.Tdq7hwck;
		ddrpi_528.fields.PI_TDQ7LWCK_F1 = MemFsp1TimingsPtr->JedecTimings.Tdq7lwck;
		ddrpi_528.fields.PI_TWCK2DQ7H_F1 = MemFsp1TimingsPtr->JedecTimings.Twck2dq7h;
		MemDdrcRegWrite(DENALI_PI(528), Channel, ddrpi_528.value);
		ddrpi_529.value = MemDdrcRegRead(DENALI_PI(529), Channel);
		ddrpi_529.fields.PI_TCBTRTW_F1 = MemFsp1TimingsPtr->JedecTimings.Tcbtrtw;
		ddrpi_529.fields.PI_TDQ72DQ_F1 = MemFsp1TimingsPtr->JedecTimings.Tdq72dq;
		ddrpi_529.fields.PI_TWCK2DQ7H_F2 = MemFsp2TimingsPtr->JedecTimings.Twck2dq7h;
		MemDdrcRegWrite(DENALI_PI(529), Channel, ddrpi_529.value);
		ddrpi_530.value = MemDdrcRegRead(DENALI_PI(530), Channel);
		ddrpi_530.fields.PI_TDQ72DQ_F2 = MemFsp2TimingsPtr->JedecTimings.Tdq72dq;
		ddrpi_530.fields.PI_TDQ7HWCK_F2 = MemFsp2TimingsPtr->JedecTimings.Tdq7hwck;
		ddrpi_530.fields.PI_TDQ7LWCK_F2 = MemFsp2TimingsPtr->JedecTimings.Tdq7lwck;
		MemDdrcRegWrite(DENALI_PI(530), Channel, ddrpi_530.value);
		ddrpi_531.value = MemDdrcRegRead(DENALI_PI(531), Channel);
		ddrpi_531.fields.PI_TCBTRTW_F2 = MemFsp2TimingsPtr->JedecTimings.Tcbtrtw;
		MemDdrcRegWrite(DENALI_PI(531), Channel, ddrpi_531.value);
		ddrpi_533.value = MemDdrcRegRead(DENALI_PI(533), Channel);
		ddrpi_533.fields.PI_MC_TRFC_F1 = MemFsp1TimingsPtr->JedecTimings.TrfcAb;
		MemDdrcRegWrite(DENALI_PI(533), Channel, ddrpi_533.value);
		ddrpi_534.value = MemDdrcRegRead(DENALI_PI(534), Channel);
		ddrpi_534.fields.PI_MC_TRFC_F2 = MemFsp2TimingsPtr->JedecTimings.TrfcAb;
		MemDdrcRegWrite(DENALI_PI(534), Channel, ddrpi_534.value);
		ddrpi_535.value = MemDdrcRegRead(DENALI_PI(535), Channel);
		ddrpi_535.fields.PI_PHYMSTR_REQ_EXTEND_NUM_F1 = GetMemFspCfgPtr(1)->FreqRatio == 4 ? 0 : 5;
		ddrpi_535.fields.PI_PHYMSTR_REQ_EXTEND_NUM_F2 = GetMemFspCfgPtr(2)->FreqRatio == 4 ? 0 : 5;
		MemDdrcRegWrite(DENALI_PI(535), Channel, ddrpi_535.value);
		ddrpi_536.value = MemDdrcRegRead(DENALI_PI(536), Channel);
		ddrpi_536.fields.PI_MC_DATA_FREQ_RATIO_F1 = GetMemFspCfgPtr(1)->FreqRatio == 4 ? 2 : 1;
		ddrpi_536.fields.PI_MC_DATA_FREQ_RATIO_F2 = GetMemFspCfgPtr(2)->FreqRatio == 4 ? 2 : 1;
		MemDdrcRegWrite(DENALI_PI(536), Channel, ddrpi_536.value);
		ddrpi_539.value = MemDdrcRegRead(DENALI_PI(539), Channel);
		ddrpi_539.fields.PI_TDFI_WCK_STOP_F1 = MemFsp1PiTimings.PI_TDFI_WCK_STOP;
		ddrpi_539.fields.PI_TDFI_WCK_STOP_F2 = MemFsp2PiTimings.PI_TDFI_WCK_STOP;
		MemDdrcRegWrite(DENALI_PI(539), Channel, ddrpi_539.value);
		MemDdrcRegWrite(DENALI_PI(1235), Channel, 10000);
		MemDdrcRegWrite(DENALI_PI(1236), Channel, 100000);
		MemDdrcRegWrite(DENALI_PI(1237), Channel, 100);
		for (fsp = 0; fsp < 3; fsp++) {
			MemGetTechFuncPtr()->MemGetSeqWait(Channel, fsp, PiSeqWaitList);
			for (index = 0; index < 20; index++)
				MemDdrcRegWrite(DENALI_PI(1251 + fsp + index * 3), Channel, PiSeqWaitList[index]);
		}
		/* the nine gaps of the PI timings (PI_WP_GAP ... PI_RR_GAP_L) of FSPs
		 * 0-2 as 16-bit halves, four times over (registers 1311-1364, no
		 * field types in CIX's headers) */
		MemDdrcRegWrite(DENALI_PI(1311), Channel, MemFsp0PiTimings.PI_WP_GAP |
				(uint32_t)MemFsp1PiTimings.PI_WP_GAP << 16);
		MemDdrcRegWrite(DENALI_PI(1312), Channel, MemFsp2PiTimings.PI_WP_GAP |
				(uint32_t)MemFsp0PiTimings.PI_RP_GAP << 16);
		MemDdrcRegWrite(DENALI_PI(1313), Channel, MemFsp1PiTimings.PI_RP_GAP |
				(uint32_t)MemFsp2PiTimings.PI_RP_GAP << 16);
		MemDdrcRegWrite(DENALI_PI(1314), Channel, MemFsp0PiTimings.PI_WR_GAP_S |
				(uint32_t)MemFsp1PiTimings.PI_WR_GAP_S << 16);
		MemDdrcRegWrite(DENALI_PI(1315), Channel, MemFsp2PiTimings.PI_WR_GAP_S |
				(uint32_t)MemFsp0PiTimings.PI_WR_GAP_L << 16);
		MemDdrcRegWrite(DENALI_PI(1316), Channel, MemFsp1PiTimings.PI_WR_GAP_L |
				(uint32_t)MemFsp2PiTimings.PI_WR_GAP_L << 16);
		MemDdrcRegWrite(DENALI_PI(1317), Channel, MemFsp0PiTimings.PI_RW_GAP |
				(uint32_t)MemFsp1PiTimings.PI_RW_GAP << 16);
		MemDdrcRegWrite(DENALI_PI(1318), Channel, MemFsp2PiTimings.PI_RW_GAP |
				(uint32_t)MemFsp0PiTimings.PI_WW_GAP_S << 16);
		MemDdrcRegWrite(DENALI_PI(1319), Channel, MemFsp1PiTimings.PI_WW_GAP_S |
				(uint32_t)MemFsp2PiTimings.PI_WW_GAP_S << 16);
		MemDdrcRegWrite(DENALI_PI(1320), Channel, MemFsp0PiTimings.PI_WW_GAP_L |
				(uint32_t)MemFsp1PiTimings.PI_WW_GAP_L << 16);
		MemDdrcRegWrite(DENALI_PI(1321), Channel, MemFsp2PiTimings.PI_WW_GAP_L |
				(uint32_t)MemFsp0PiTimings.PI_RR_GAP_S << 16);
		MemDdrcRegWrite(DENALI_PI(1322), Channel, MemFsp1PiTimings.PI_RR_GAP_S |
				(uint32_t)MemFsp2PiTimings.PI_RR_GAP_S << 16);
		MemDdrcRegWrite(DENALI_PI(1323), Channel, MemFsp0PiTimings.PI_RR_GAP_L |
				(uint32_t)MemFsp1PiTimings.PI_RR_GAP_L << 16);
		MemDdrcRegWrite(DENALI_PI(1324), Channel, MemFsp2PiTimings.PI_RR_GAP_L |
				(uint32_t)MemFsp0PiTimings.PI_WP_GAP << 16);
		MemDdrcRegWrite(DENALI_PI(1325), Channel, MemFsp1PiTimings.PI_WP_GAP |
				(uint32_t)MemFsp2PiTimings.PI_WP_GAP << 16);
		MemDdrcRegWrite(DENALI_PI(1326), Channel, MemFsp0PiTimings.PI_RP_GAP |
				(uint32_t)MemFsp1PiTimings.PI_RP_GAP << 16);
		MemDdrcRegWrite(DENALI_PI(1327), Channel, MemFsp2PiTimings.PI_RP_GAP |
				(uint32_t)MemFsp0PiTimings.PI_WR_GAP_S << 16);
		MemDdrcRegWrite(DENALI_PI(1328), Channel, MemFsp1PiTimings.PI_WR_GAP_S |
				(uint32_t)MemFsp2PiTimings.PI_WR_GAP_S << 16);
		MemDdrcRegWrite(DENALI_PI(1329), Channel, MemFsp0PiTimings.PI_WR_GAP_L |
				(uint32_t)MemFsp1PiTimings.PI_WR_GAP_L << 16);
		MemDdrcRegWrite(DENALI_PI(1330), Channel, MemFsp2PiTimings.PI_WR_GAP_L |
				(uint32_t)MemFsp0PiTimings.PI_RW_GAP << 16);
		MemDdrcRegWrite(DENALI_PI(1331), Channel, MemFsp1PiTimings.PI_RW_GAP |
				(uint32_t)MemFsp2PiTimings.PI_RW_GAP << 16);
		MemDdrcRegWrite(DENALI_PI(1332), Channel, MemFsp0PiTimings.PI_WW_GAP_S |
				(uint32_t)MemFsp1PiTimings.PI_WW_GAP_S << 16);
		MemDdrcRegWrite(DENALI_PI(1333), Channel, MemFsp2PiTimings.PI_WW_GAP_S |
				(uint32_t)MemFsp0PiTimings.PI_WW_GAP_L << 16);
		MemDdrcRegWrite(DENALI_PI(1334), Channel, MemFsp1PiTimings.PI_WW_GAP_L |
				(uint32_t)MemFsp2PiTimings.PI_WW_GAP_L << 16);
		MemDdrcRegWrite(DENALI_PI(1335), Channel, MemFsp0PiTimings.PI_RR_GAP_S |
				(uint32_t)MemFsp1PiTimings.PI_RR_GAP_S << 16);
		MemDdrcRegWrite(DENALI_PI(1336), Channel, MemFsp2PiTimings.PI_RR_GAP_S |
				(uint32_t)MemFsp0PiTimings.PI_RR_GAP_L << 16);
		MemDdrcRegWrite(DENALI_PI(1337), Channel, MemFsp1PiTimings.PI_RR_GAP_L |
				(uint32_t)MemFsp2PiTimings.PI_RR_GAP_L << 16);
		MemDdrcRegWrite(DENALI_PI(1338), Channel, MemFsp0PiTimings.PI_WP_GAP |
				(uint32_t)MemFsp1PiTimings.PI_WP_GAP << 16);
		MemDdrcRegWrite(DENALI_PI(1339), Channel, MemFsp2PiTimings.PI_WP_GAP |
				(uint32_t)MemFsp0PiTimings.PI_RP_GAP << 16);
		MemDdrcRegWrite(DENALI_PI(1340), Channel, MemFsp1PiTimings.PI_RP_GAP |
				(uint32_t)MemFsp2PiTimings.PI_RP_GAP << 16);
		MemDdrcRegWrite(DENALI_PI(1341), Channel, MemFsp0PiTimings.PI_WR_GAP_S |
				(uint32_t)MemFsp1PiTimings.PI_WR_GAP_S << 16);
		MemDdrcRegWrite(DENALI_PI(1342), Channel, MemFsp2PiTimings.PI_WR_GAP_S |
				(uint32_t)MemFsp0PiTimings.PI_WR_GAP_L << 16);
		MemDdrcRegWrite(DENALI_PI(1343), Channel, MemFsp1PiTimings.PI_WR_GAP_L |
				(uint32_t)MemFsp2PiTimings.PI_WR_GAP_L << 16);
		MemDdrcRegWrite(DENALI_PI(1344), Channel, MemFsp0PiTimings.PI_RW_GAP |
				(uint32_t)MemFsp1PiTimings.PI_RW_GAP << 16);
		MemDdrcRegWrite(DENALI_PI(1345), Channel, MemFsp2PiTimings.PI_RW_GAP |
				(uint32_t)MemFsp0PiTimings.PI_WW_GAP_S << 16);
		MemDdrcRegWrite(DENALI_PI(1346), Channel, MemFsp1PiTimings.PI_WW_GAP_S |
				(uint32_t)MemFsp2PiTimings.PI_WW_GAP_S << 16);
		MemDdrcRegWrite(DENALI_PI(1347), Channel, MemFsp0PiTimings.PI_WW_GAP_L |
				(uint32_t)MemFsp1PiTimings.PI_WW_GAP_L << 16);
		MemDdrcRegWrite(DENALI_PI(1348), Channel, MemFsp2PiTimings.PI_WW_GAP_L |
				(uint32_t)MemFsp0PiTimings.PI_RR_GAP_S << 16);
		MemDdrcRegWrite(DENALI_PI(1349), Channel, MemFsp1PiTimings.PI_RR_GAP_S |
				(uint32_t)MemFsp2PiTimings.PI_RR_GAP_S << 16);
		MemDdrcRegWrite(DENALI_PI(1350), Channel, MemFsp0PiTimings.PI_RR_GAP_L |
				(uint32_t)MemFsp1PiTimings.PI_RR_GAP_L << 16);
		MemDdrcRegWrite(DENALI_PI(1351), Channel, MemFsp2PiTimings.PI_RR_GAP_L |
				(uint32_t)MemFsp0PiTimings.PI_WP_GAP << 16);
		MemDdrcRegWrite(DENALI_PI(1352), Channel, MemFsp1PiTimings.PI_WP_GAP |
				(uint32_t)MemFsp2PiTimings.PI_WP_GAP << 16);
		MemDdrcRegWrite(DENALI_PI(1353), Channel, MemFsp0PiTimings.PI_RP_GAP |
				(uint32_t)MemFsp1PiTimings.PI_RP_GAP << 16);
		MemDdrcRegWrite(DENALI_PI(1354), Channel, MemFsp2PiTimings.PI_RP_GAP |
				(uint32_t)MemFsp0PiTimings.PI_WR_GAP_S << 16);
		MemDdrcRegWrite(DENALI_PI(1355), Channel, MemFsp1PiTimings.PI_WR_GAP_S |
				(uint32_t)MemFsp2PiTimings.PI_WR_GAP_S << 16);
		MemDdrcRegWrite(DENALI_PI(1356), Channel, MemFsp0PiTimings.PI_WR_GAP_L |
				(uint32_t)MemFsp1PiTimings.PI_WR_GAP_L << 16);
		MemDdrcRegWrite(DENALI_PI(1357), Channel, MemFsp2PiTimings.PI_WR_GAP_L |
				(uint32_t)MemFsp0PiTimings.PI_RW_GAP << 16);
		MemDdrcRegWrite(DENALI_PI(1358), Channel, MemFsp1PiTimings.PI_RW_GAP |
				(uint32_t)MemFsp2PiTimings.PI_RW_GAP << 16);
		MemDdrcRegWrite(DENALI_PI(1359), Channel, MemFsp0PiTimings.PI_WW_GAP_S |
				(uint32_t)MemFsp1PiTimings.PI_WW_GAP_S << 16);
		MemDdrcRegWrite(DENALI_PI(1360), Channel, MemFsp2PiTimings.PI_WW_GAP_S |
				(uint32_t)MemFsp0PiTimings.PI_WW_GAP_L << 16);
		MemDdrcRegWrite(DENALI_PI(1361), Channel, MemFsp1PiTimings.PI_WW_GAP_L |
				(uint32_t)MemFsp2PiTimings.PI_WW_GAP_L << 16);
		MemDdrcRegWrite(DENALI_PI(1362), Channel, MemFsp0PiTimings.PI_RR_GAP_S |
				(uint32_t)MemFsp1PiTimings.PI_RR_GAP_S << 16);
		MemDdrcRegWrite(DENALI_PI(1363), Channel, MemFsp2PiTimings.PI_RR_GAP_S |
				(uint32_t)MemFsp0PiTimings.PI_RR_GAP_L << 16);
		MemDdrcRegWrite(DENALI_PI(1364), Channel, MemFsp1PiTimings.PI_RR_GAP_L |
				(uint32_t)MemFsp2PiTimings.PI_RR_GAP_L << 16);
	}
}

/* MemDdrcTimingCfg: the technology's timings computed first, then the
 * controller's timings of FSP 1 and 2, its turnaround delays and its DFI
 * timings */
void MemDdrcTimingCfg(void)
{
	MEMORY_INIT_S *MemInitPtr;
	LPDDR_TIMING_S *MemFsp1TimingsPtr, *MemFsp2TimingsPtr;
	TURNAROUND_TIMING_S MemFsp1TurnaroundTimings, MemFsp2TurnaroundTimings;
	uint8_t Channel;
	DENALI_CTL_19 ddrc_19;
	DENALI_CTL_20 ddrc_20;
	DENALI_CTL_29 ddrc_29;
	DENALI_CTL_30 ddrc_30;
	DENALI_CTL_31 ddrc_31;
	DENALI_CTL_34 ddrc_34;
	DENALI_CTL_35 ddrc_35;
	DENALI_CTL_36 ddrc_36;
	DENALI_CTL_37 ddrc_37;
	DENALI_CTL_38 ddrc_38;
	DENALI_CTL_39 ddrc_39;
	DENALI_CTL_40 ddrc_40;
	DENALI_CTL_41 ddrc_41;
	DENALI_CTL_42 ddrc_42;
	DENALI_CTL_45 ddrc_45;
	DENALI_CTL_46 ddrc_46;
	DENALI_CTL_47 ddrc_47;
	DENALI_CTL_48 ddrc_48;
	DENALI_CTL_49 ddrc_49;
	DENALI_CTL_50 ddrc_50;
	DENALI_CTL_52 ddrc_52;
	DENALI_CTL_53 ddrc_53;
	DENALI_CTL_54 ddrc_54;
	DENALI_CTL_55 ddrc_55;
	DENALI_CTL_56 ddrc_56;
	DENALI_CTL_57 ddrc_57;
	DENALI_CTL_60 ddrc_60;
	DENALI_CTL_62 ddrc_62;
	DENALI_CTL_63 ddrc_63;
	DENALI_CTL_64 ddrc_64;
	DENALI_CTL_65 ddrc_65;
	DENALI_CTL_66 ddrc_66;
	DENALI_CTL_68 ddrc_68;
	DENALI_CTL_69 ddrc_69;
	DENALI_CTL_70 ddrc_70;
	DENALI_CTL_71 ddrc_71;
	DENALI_CTL_73 ddrc_73;
	DENALI_CTL_74 ddrc_74;
	DENALI_CTL_75 ddrc_75;
	DENALI_CTL_77 ddrc_77;
	DENALI_CTL_78 ddrc_78;
	DENALI_CTL_79 ddrc_79;
	DENALI_CTL_80 ddrc_80;
	DENALI_CTL_81 ddrc_81;
	DENALI_CTL_82 ddrc_82;
	DENALI_CTL_83 ddrc_83;
	DENALI_CTL_85 ddrc_85;
	DENALI_CTL_86 ddrc_86;
	DENALI_CTL_89 ddrc_89;
	DENALI_CTL_90 ddrc_90;
	DENALI_CTL_91 ddrc_91;
	DENALI_CTL_92 ddrc_92;
	DENALI_CTL_93 ddrc_93;
	DENALI_CTL_101 ddrc_101;
	DENALI_CTL_102 ddrc_102;
	DENALI_CTL_103 ddrc_103;
	DENALI_CTL_104 ddrc_104;
	DENALI_CTL_105 ddrc_105;
	DENALI_CTL_106 ddrc_106;
	DENALI_CTL_109 ddrc_109;
	DENALI_CTL_110 ddrc_110;
	DENALI_CTL_111 ddrc_111;
	DENALI_CTL_112 ddrc_112;
	DENALI_CTL_116 ddrc_116;
	DENALI_CTL_117 ddrc_117;
	DENALI_CTL_138 ddrc_138;
	DENALI_CTL_174 ddrc_174;
	DENALI_CTL_175 ddrc_175;
	DENALI_CTL_318 ddrc_318;
	DENALI_CTL_319 ddrc_319;
	DENALI_CTL_321 ddrc_321;
	DENALI_CTL_402 ddrc_402;
	DENALI_CTL_403 ddrc_403;
	DENALI_CTL_404 ddrc_404;
	DENALI_CTL_405 ddrc_405;
	DENALI_CTL_408 ddrc_408;
	DENALI_CTL_409 ddrc_409;
	DENALI_CTL_410 ddrc_410;
	DENALI_CTL_411 ddrc_411;
	DENALI_CTL_412 ddrc_412;
	DENALI_CTL_413 ddrc_413;
	DENALI_CTL_414 ddrc_414;
	DENALI_CTL_415 ddrc_415;
	DENALI_CTL_521 ddrc_521;
	DENALI_CTL_529 ddrc_529;
	DENALI_CTL_530 ddrc_530;
	DENALI_CTL_531 ddrc_531;
	DENALI_CTL_532 ddrc_532;
	DENALI_CTL_540 ddrc_540;
	DENALI_CTL_541 ddrc_541;
	DENALI_CTL_542 ddrc_542;
	DENALI_CTL_553 ddrc_553;
	DENALI_CTL_554 ddrc_554;
	DENALI_CTL_616 ddrc_616;
	DENALI_CTL_617 ddrc_617;

	MemGetTechFuncPtr()->MemTimingInit();
	MemInitPtr = GetMemInitPtr();
	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel))
			continue;
		MemFsp1TimingsPtr = GetMemTimingsPtr(Channel, 1);
		MemFsp2TimingsPtr = GetMemTimingsPtr(Channel, 2);
		ddrc_19.value = MemDdrcRegRead(DENALI_CTL(19), Channel);
		ddrc_19.fields.TOSCO_F1 = MemFsp1TimingsPtr->JedecTimings.Tosco;
		MemDdrcRegWrite(DENALI_CTL(19), Channel, ddrc_19.value);
		ddrc_20.value = MemDdrcRegRead(DENALI_CTL(20), Channel);
		ddrc_20.fields.TOSCO_F2 = MemFsp2TimingsPtr->JedecTimings.Tosco;
		MemDdrcRegWrite(DENALI_CTL(20), Channel, ddrc_20.value);
		ddrc_29.value = MemDdrcRegRead(DENALI_CTL(29), Channel);
		ddrc_29.fields.CASLAT_LIN_F1 = MemFsp1TimingsPtr->JedecTimings.RL * 2;
		MemDdrcRegWrite(DENALI_CTL(29), Channel, ddrc_29.value);
		ddrc_30.value = MemDdrcRegRead(DENALI_CTL(30), Channel);
		ddrc_30.fields.CASLAT_LIN_F2 = MemFsp2TimingsPtr->JedecTimings.RL * 2;
		ddrc_30.fields.WRLAT_F1 = MemFsp1TimingsPtr->JedecTimings.WL;
		ddrc_30.fields.WRLAT_F2 = MemFsp2TimingsPtr->JedecTimings.WL;
		ddrc_30.fields.TBST_INT_INTERVAL = MemInitPtr->GlobalTimings.TBST_INT_INTERVAL;
		MemDdrcRegWrite(DENALI_CTL(30), Channel, ddrc_30.value);
		ddrc_31.value = MemDdrcRegRead(DENALI_CTL(31), Channel);
		ddrc_31.fields.TCCD = MemInitPtr->GlobalTimings.TCCD;
		MemDdrcRegWrite(DENALI_CTL(31), Channel, ddrc_31.value);
		ddrc_34.value = MemDdrcRegRead(DENALI_CTL(34), Channel);
		ddrc_34.fields.TCCD_L_F1 = MemFsp1TimingsPtr->JedecTimings.TccdL;
		MemDdrcRegWrite(DENALI_CTL(34), Channel, ddrc_34.value);
		ddrc_35.value = MemDdrcRegRead(DENALI_CTL(35), Channel);
		ddrc_35.fields.TRRD_L_F1 = MemFsp1TimingsPtr->JedecTimings.TrrdL;
		ddrc_35.fields.TRRD_F1 = MemFsp1TimingsPtr->JedecTimings.Trrd;
		ddrc_35.fields.TRC_F1 = MemFsp1TimingsPtr->JedecTimings.Trc;
		MemDdrcRegWrite(DENALI_CTL(35), Channel, ddrc_35.value);
		ddrc_36.value = MemDdrcRegRead(DENALI_CTL(36), Channel);
		ddrc_36.fields.TWTR_L_F1 = MemFsp1TimingsPtr->JedecTimings.TwtrL;
		ddrc_36.fields.TWTR_F1 = MemFsp1TimingsPtr->JedecTimings.Twtr;
		ddrc_36.fields.TRAS_MIN_F1 = MemFsp1TimingsPtr->JedecTimings.TrasMin;
		MemDdrcRegWrite(DENALI_CTL(36), Channel, ddrc_36.value);
		ddrc_37.value = MemDdrcRegRead(DENALI_CTL(37), Channel);
		ddrc_37.fields.TWTR_L_AP_F1 = MemFsp1TimingsPtr->JedecTimings.TwtrLAP;
		ddrc_37.fields.TWTR_AP_F1 = MemFsp1TimingsPtr->JedecTimings.TwtrAP;
		ddrc_37.fields.TRP_F1 = MemFsp1TimingsPtr->JedecTimings.TrpPb;
		MemDdrcRegWrite(DENALI_CTL(37), Channel, ddrc_37.value);
		ddrc_38.value = MemDdrcRegRead(DENALI_CTL(38), Channel);
		ddrc_38.fields.TRRD_F2 = MemFsp2TimingsPtr->JedecTimings.Trrd;
		ddrc_38.fields.TFAW_F1 = MemFsp1TimingsPtr->JedecTimings.Tfaw;
		ddrc_38.fields.TCCD_L_F2 = MemFsp2TimingsPtr->JedecTimings.TccdL;
		MemDdrcRegWrite(DENALI_CTL(38), Channel, ddrc_38.value);
		ddrc_39.value = MemDdrcRegRead(DENALI_CTL(39), Channel);
		ddrc_39.fields.TRRD_L_F2 = MemFsp2TimingsPtr->JedecTimings.TrrdL;
		ddrc_39.fields.TRC_F2 = MemFsp2TimingsPtr->JedecTimings.Trc;
		MemDdrcRegWrite(DENALI_CTL(39), Channel, ddrc_39.value);
		ddrc_40.value = MemDdrcRegRead(DENALI_CTL(40), Channel);
		ddrc_40.fields.TWTR_L_F2 = MemFsp2TimingsPtr->JedecTimings.TwtrL;
		ddrc_40.fields.TWTR_F2 = MemFsp2TimingsPtr->JedecTimings.Twtr;
		ddrc_40.fields.TRAS_MIN_F2 = MemFsp2TimingsPtr->JedecTimings.TrasMin;
		MemDdrcRegWrite(DENALI_CTL(40), Channel, ddrc_40.value);
		ddrc_41.value = MemDdrcRegRead(DENALI_CTL(41), Channel);
		ddrc_41.fields.TWTR_L_AP_F2 = MemFsp2TimingsPtr->JedecTimings.TwtrLAP;
		ddrc_41.fields.TWTR_AP_F2 = MemFsp2TimingsPtr->JedecTimings.TwtrAP;
		ddrc_41.fields.TRP_F2 = MemFsp2TimingsPtr->JedecTimings.TrpPb;
		MemDdrcRegWrite(DENALI_CTL(41), Channel, ddrc_41.value);
		ddrc_42.value = MemDdrcRegRead(DENALI_CTL(42), Channel);
		ddrc_42.fields.TFAW_F2 = MemFsp2TimingsPtr->JedecTimings.Tfaw;
		MemDdrcRegWrite(DENALI_CTL(42), Channel, ddrc_42.value);
		ddrc_45.value = MemDdrcRegRead(DENALI_CTL(45), Channel);
		ddrc_45.fields.TRTP_F1 = MemFsp1TimingsPtr->JedecTimings.Trtp;
		ddrc_45.fields.TMRD_F1 = MemFsp1TimingsPtr->JedecTimings.Tmrd;
		ddrc_45.fields.TMOD_F1 = MemFsp1TimingsPtr->JedecTimings.Tmod;
		MemDdrcRegWrite(DENALI_CTL(45), Channel, ddrc_45.value);
		ddrc_46.value = MemDdrcRegRead(DENALI_CTL(46), Channel);
		ddrc_46.fields.TRAS_MAX_F1 = MemFsp1TimingsPtr->JedecTimings.TrasMax;
		ddrc_46.fields.TCKESR_F1 = MemFsp1TimingsPtr->JedecTimings.Tckesr;
		ddrc_46.fields.TCKE_F1 = MemFsp1TimingsPtr->JedecTimings.Tcke;
		MemDdrcRegWrite(DENALI_CTL(46), Channel, ddrc_46.value);
		ddrc_47.value = MemDdrcRegRead(DENALI_CTL(47), Channel);
		ddrc_47.fields.TRTP_F2 = MemFsp2TimingsPtr->JedecTimings.Trtp;
		ddrc_47.fields.TCSH_F1 = MemFsp1TimingsPtr->JedecTimings.Tcsh;
		ddrc_47.fields.TCCDMW_F1 = MemFsp1TimingsPtr->JedecTimings.Tccdmw;
		ddrc_47.fields.TCACSH_F1 = MemFsp1TimingsPtr->JedecTimings.Tcacsh;
		MemDdrcRegWrite(DENALI_CTL(47), Channel, ddrc_47.value);
		ddrc_48.value = MemDdrcRegRead(DENALI_CTL(48), Channel);
		ddrc_48.fields.TRAS_MAX_F2 = MemFsp2TimingsPtr->JedecTimings.TrasMax;
		ddrc_48.fields.TMRD_F2 = MemFsp2TimingsPtr->JedecTimings.Tmrd;
		ddrc_48.fields.TMOD_F2 = MemFsp2TimingsPtr->JedecTimings.Tmod;
		MemDdrcRegWrite(DENALI_CTL(48), Channel, ddrc_48.value);
		ddrc_49.value = MemDdrcRegRead(DENALI_CTL(49), Channel);
		ddrc_49.fields.TCSH_F2 = MemFsp2TimingsPtr->JedecTimings.Tcsh;
		ddrc_49.fields.TCKESR_F2 = MemFsp2TimingsPtr->JedecTimings.Tckesr;
		ddrc_49.fields.TCKE_F2 = MemFsp2TimingsPtr->JedecTimings.Tcke;
		ddrc_49.fields.TCCDMW_F2 = MemFsp2TimingsPtr->JedecTimings.Tccdmw;
		MemDdrcRegWrite(DENALI_CTL(49), Channel, ddrc_49.value);
		ddrc_50.value = MemDdrcRegRead(DENALI_CTL(50), Channel);
		ddrc_50.fields.TPPD_F1 = MemFsp1TimingsPtr->JedecTimings.Tppd;
		ddrc_50.fields.TPPD_F2 = MemFsp2TimingsPtr->JedecTimings.Tppd;
		ddrc_50.fields.TCACSH_F2 = MemFsp2TimingsPtr->JedecTimings.Tcacsh;
		MemDdrcRegWrite(DENALI_CTL(50), Channel, ddrc_50.value);
		ddrc_52.value = MemDdrcRegRead(DENALI_CTL(52), Channel);
		ddrc_52.fields.TRCD_F1 = MemFsp1TimingsPtr->JedecTimings.Trcd;
		ddrc_52.fields.TRCD_WR_F1 = MemFsp1TimingsPtr->JedecTimings.TrcdL;
		MemDdrcRegWrite(DENALI_CTL(52), Channel, ddrc_52.value);
		ddrc_53.value = MemDdrcRegRead(DENALI_CTL(53), Channel);
		ddrc_53.fields.TRCD_F2 = MemFsp2TimingsPtr->JedecTimings.Trcd;
		ddrc_53.fields.TRCD_WR_F2 = MemFsp2TimingsPtr->JedecTimings.TrcdL;
		ddrc_53.fields.TWR_F1 = MemFsp1TimingsPtr->JedecTimings.Twr;
		ddrc_53.fields.TWR_F2 = MemFsp2TimingsPtr->JedecTimings.Twr;
		MemDdrcRegWrite(DENALI_CTL(53), Channel, ddrc_53.value);
		ddrc_54.value = MemDdrcRegRead(DENALI_CTL(54), Channel);
		ddrc_54.fields.TMRR = MemInitPtr->GlobalTimings.TMRR;
		ddrc_54.fields.TRAS_LOCKOUT = MemInitPtr->GlobalTimings.TRAS_LOCKOUT;
		MemDdrcRegWrite(DENALI_CTL(54), Channel, ddrc_54.value);
		ddrc_55.value = MemDdrcRegRead(DENALI_CTL(55), Channel);
		ddrc_55.fields.TDAL_F1 = MemFsp1TimingsPtr->JedecTimings.Tdal;
		ddrc_55.fields.TDAL_F2 = MemFsp2TimingsPtr->JedecTimings.Tdal;
		MemDdrcRegWrite(DENALI_CTL(55), Channel, ddrc_55.value);
		ddrc_56.value = MemDdrcRegRead(DENALI_CTL(56), Channel);
		ddrc_56.fields.TRP_AB_F1_0 = MemFsp1TimingsPtr->JedecTimings.TrpAb;
		ddrc_56.fields.TRP_AB_F2_0 = MemFsp2TimingsPtr->JedecTimings.TrpAb;
		MemDdrcRegWrite(DENALI_CTL(56), Channel, ddrc_56.value);
		ddrc_57.value = MemDdrcRegRead(DENALI_CTL(57), Channel);
		ddrc_57.fields.TRP_AB_F1_1 = MemFsp1TimingsPtr->JedecTimings.TrpAb;
		ddrc_57.fields.TRP_AB_F2_1 = MemFsp2TimingsPtr->JedecTimings.TrpAb;
		MemDdrcRegWrite(DENALI_CTL(57), Channel, ddrc_57.value);
		ddrc_60.value = MemDdrcRegRead(DENALI_CTL(60), Channel);
		ddrc_60.fields.TRFC_OPT_THRESHOLD = MemInitPtr->GlobalTimings.TRFC_OPT_THRESHOLD;
		MemDdrcRegWrite(DENALI_CTL(60), Channel, ddrc_60.value);
		ddrc_62.value = MemDdrcRegRead(DENALI_CTL(62), Channel);
		ddrc_62.fields.TRFC_F1 = MemFsp1TimingsPtr->JedecTimings.TrfcAb;
		MemDdrcRegWrite(DENALI_CTL(62), Channel, ddrc_62.value);
		ddrc_63.value = MemDdrcRegRead(DENALI_CTL(63), Channel);
		ddrc_63.fields.TREF_F1 = MemFsp1TimingsPtr->JedecTimings.Tref;
		MemDdrcRegWrite(DENALI_CTL(63), Channel, ddrc_63.value);
		ddrc_64.value = MemDdrcRegRead(DENALI_CTL(64), Channel);
		ddrc_64.fields.TRFC_F2 = MemFsp2TimingsPtr->JedecTimings.TrfcAb;
		MemDdrcRegWrite(DENALI_CTL(64), Channel, ddrc_64.value);
		ddrc_65.value = MemDdrcRegRead(DENALI_CTL(65), Channel);
		ddrc_65.fields.TREF_F2 = MemFsp2TimingsPtr->JedecTimings.Tref;
		MemDdrcRegWrite(DENALI_CTL(65), Channel, ddrc_65.value);
		ddrc_66.value = MemDdrcRegRead(DENALI_CTL(66), Channel);
		ddrc_66.fields.TREF_INTERVAL = MemInitPtr->GlobalTimings.TREF_INTERVAL;
		MemDdrcRegWrite(DENALI_CTL(66), Channel, ddrc_66.value);
		ddrc_68.value = MemDdrcRegRead(DENALI_CTL(68), Channel);
		ddrc_68.fields.TRFC_PB_F1_0 = MemFsp1TimingsPtr->JedecTimings.TrfcPb;
		MemDdrcRegWrite(DENALI_CTL(68), Channel, ddrc_68.value);
		ddrc_69.value = MemDdrcRegRead(DENALI_CTL(69), Channel);
		ddrc_69.fields.TPBR2ACT_F1_0 = MemFsp1TimingsPtr->JedecTimings.Tpbr2act;
		ddrc_69.fields.TPBR2PBR_F1_0 = MemFsp1TimingsPtr->JedecTimings.Tpbr2pbr;
		MemDdrcRegWrite(DENALI_CTL(69), Channel, ddrc_69.value);
		ddrc_70.value = MemDdrcRegRead(DENALI_CTL(70), Channel);
		ddrc_70.fields.TPBR2ACT_F2_0 = MemFsp2TimingsPtr->JedecTimings.Tpbr2act;
		ddrc_70.fields.TRFC_PB_F2_0 = MemFsp2TimingsPtr->JedecTimings.TrfcPb;
		MemDdrcRegWrite(DENALI_CTL(70), Channel, ddrc_70.value);
		ddrc_71.value = MemDdrcRegRead(DENALI_CTL(71), Channel);
		ddrc_71.fields.TPBR2PBR_F2_0 = MemFsp2TimingsPtr->JedecTimings.Tpbr2pbr;
		MemDdrcRegWrite(DENALI_CTL(71), Channel, ddrc_71.value);
		ddrc_73.value = MemDdrcRegRead(DENALI_CTL(73), Channel);
		ddrc_73.fields.TPBR2ACT_F1_1 = MemFsp1TimingsPtr->JedecTimings.Tpbr2act;
		ddrc_73.fields.TRFC_PB_F1_1 = MemFsp1TimingsPtr->JedecTimings.TrfcPb;
		MemDdrcRegWrite(DENALI_CTL(73), Channel, ddrc_73.value);
		ddrc_74.value = MemDdrcRegRead(DENALI_CTL(74), Channel);
		ddrc_74.fields.TPBR2PBR_F1_1 = MemFsp1TimingsPtr->JedecTimings.Tpbr2pbr;
		ddrc_74.fields.TRFC_PB_F2_1 = MemFsp2TimingsPtr->JedecTimings.TrfcPb;
		MemDdrcRegWrite(DENALI_CTL(74), Channel, ddrc_74.value);
		ddrc_75.value = MemDdrcRegRead(DENALI_CTL(75), Channel);
		ddrc_75.fields.TPBR2ACT_F2_1 = MemFsp2TimingsPtr->JedecTimings.Tpbr2act;
		ddrc_75.fields.TPBR2PBR_F2_1 = MemFsp2TimingsPtr->JedecTimings.Tpbr2pbr;
		MemDdrcRegWrite(DENALI_CTL(75), Channel, ddrc_75.value);
		ddrc_77.value = MemDdrcRegRead(DENALI_CTL(77), Channel);
		ddrc_77.fields.TPDEX_F1 = MemFsp1TimingsPtr->JedecTimings.Tpdex;
		MemDdrcRegWrite(DENALI_CTL(77), Channel, ddrc_77.value);
		ddrc_78.value = MemDdrcRegRead(DENALI_CTL(78), Channel);
		ddrc_78.fields.TMRRI_F1 = MemFsp1TimingsPtr->JedecTimings.Tmrri;
		ddrc_78.fields.TPDEX_F2 = MemFsp2TimingsPtr->JedecTimings.Tpdex;
		MemDdrcRegWrite(DENALI_CTL(78), Channel, ddrc_78.value);
		ddrc_79.value = MemDdrcRegRead(DENALI_CTL(79), Channel);
		ddrc_79.fields.TMRRI_F2 = MemFsp2TimingsPtr->JedecTimings.Tmrri;
		MemDdrcRegWrite(DENALI_CTL(79), Channel, ddrc_79.value);
		ddrc_80.value = MemDdrcRegRead(DENALI_CTL(80), Channel);
		ddrc_80.fields.TCKEHCS_F1 = MemFsp1TimingsPtr->JedecTimings.Tckehcs;
		ddrc_80.fields.TCKELCS_F1 = MemFsp1TimingsPtr->JedecTimings.Tckelcs;
		ddrc_80.fields.TMRWCKEL_F1 = MemFsp1TimingsPtr->JedecTimings.Tmrwckel;
		MemDdrcRegWrite(DENALI_CTL(80), Channel, ddrc_80.value);
		ddrc_81.value = MemDdrcRegRead(DENALI_CTL(81), Channel);
		ddrc_81.fields.TCKEHCS_F2 = MemFsp2TimingsPtr->JedecTimings.Tckehcs;
		ddrc_81.fields.TCKELCS_F2 = MemFsp2TimingsPtr->JedecTimings.Tckelcs;
		ddrc_81.fields.TMRWCKEL_F2 = MemFsp2TimingsPtr->JedecTimings.Tmrwckel;
		ddrc_81.fields.TZQCKE_F1 = MemFsp1TimingsPtr->JedecTimings.Tzqcke;
		MemDdrcRegWrite(DENALI_CTL(81), Channel, ddrc_81.value);
		ddrc_82.value = MemDdrcRegRead(DENALI_CTL(82), Channel);
		ddrc_82.fields.TCSCKE_F1 = MemFsp1TimingsPtr->JedecTimings.Tcscke;
		ddrc_82.fields.TZQCKE_F2 = MemFsp2TimingsPtr->JedecTimings.Tzqcke;
		MemDdrcRegWrite(DENALI_CTL(82), Channel, ddrc_82.value);
		ddrc_83.value = MemDdrcRegRead(DENALI_CTL(83), Channel);
		ddrc_83.fields.TCSCKE_F2 = MemFsp2TimingsPtr->JedecTimings.Tcscke;
		MemDdrcRegWrite(DENALI_CTL(83), Channel, ddrc_83.value);
		ddrc_85.value = MemDdrcRegRead(DENALI_CTL(85), Channel);
		ddrc_85.fields.TXSR_F1 = MemFsp1TimingsPtr->JedecTimings.Txsr;
		ddrc_85.fields.TXSNR_F1 = MemFsp1TimingsPtr->JedecTimings.Txsnr;
		MemDdrcRegWrite(DENALI_CTL(85), Channel, ddrc_85.value);
		ddrc_86.value = MemDdrcRegRead(DENALI_CTL(86), Channel);
		ddrc_86.fields.TXSR_F2 = MemFsp2TimingsPtr->JedecTimings.Txsr;
		ddrc_86.fields.TXSNR_F2 = MemFsp2TimingsPtr->JedecTimings.Txsnr;
		MemDdrcRegWrite(DENALI_CTL(86), Channel, ddrc_86.value);
		ddrc_89.value = MemDdrcRegRead(DENALI_CTL(89), Channel);
		ddrc_89.fields.TCKELCMD_F1 = MemFsp1TimingsPtr->JedecTimings.Tckelcmd;
		ddrc_89.fields.TCSCKEH_F1 = MemFsp1TimingsPtr->JedecTimings.Tcsckeh;
		ddrc_89.fields.TESCKE_F1 = MemFsp1TimingsPtr->JedecTimings.Tescke;
		ddrc_89.fields.TSR_F1 = MemFsp1TimingsPtr->JedecTimings.Tsr;
		MemDdrcRegWrite(DENALI_CTL(89), Channel, ddrc_89.value);
		ddrc_90.value = MemDdrcRegRead(DENALI_CTL(90), Channel);
		ddrc_90.fields.TCKCKEL_F1 = MemFsp1TimingsPtr->JedecTimings.Tckckel;
		ddrc_90.fields.TCKEHCMD_F1 = MemFsp1TimingsPtr->JedecTimings.Tckehcmd;
		ddrc_90.fields.TCKELPD_F1 = MemFsp1TimingsPtr->JedecTimings.Tckelpd;
		MemDdrcRegWrite(DENALI_CTL(90), Channel, ddrc_90.value);
		ddrc_91.value = MemDdrcRegRead(DENALI_CTL(91), Channel);
		ddrc_91.fields.TCKELCMD_F2 = MemFsp2TimingsPtr->JedecTimings.Tckelcmd;
		ddrc_91.fields.TCSCKEH_F2 = MemFsp2TimingsPtr->JedecTimings.Tcsckeh;
		ddrc_91.fields.TESCKE_F2 = MemFsp2TimingsPtr->JedecTimings.Tescke;
		ddrc_91.fields.TSR_F2 = MemFsp2TimingsPtr->JedecTimings.Tsr;
		MemDdrcRegWrite(DENALI_CTL(91), Channel, ddrc_91.value);
		ddrc_92.value = MemDdrcRegRead(DENALI_CTL(92), Channel);
		ddrc_92.fields.TCKCKEL_F2 = MemFsp2TimingsPtr->JedecTimings.Tckckel;
		ddrc_92.fields.TCKEHCMD_F2 = MemFsp2TimingsPtr->JedecTimings.Tckehcmd;
		ddrc_92.fields.TCKELPD_F2 = MemFsp2TimingsPtr->JedecTimings.Tckelpd;
		MemDdrcRegWrite(DENALI_CTL(92), Channel, ddrc_92.value);
		ddrc_93.value = MemDdrcRegRead(DENALI_CTL(93), Channel);
		ddrc_93.fields.TCMDCKE_F1 = MemFsp1TimingsPtr->JedecTimings.Tcmdcke;
		ddrc_93.fields.TCMDCKE_F2 = MemFsp2TimingsPtr->JedecTimings.Tcmdcke;
		MemDdrcRegWrite(DENALI_CTL(93), Channel, ddrc_93.value);
		ddrc_116.value = MemDdrcRegRead(DENALI_CTL(116), Channel);
		ddrc_116.fields.RD_TO_WCK_SYNC_LOST_F1 = MemFsp1TimingsPtr->JedecTimings.Rd2WckSyncLost;
		ddrc_116.fields.WR_TO_WCK_SYNC_LOST_F1 = MemFsp1TimingsPtr->JedecTimings.Wr2WckSyncLost;
		MemDdrcRegWrite(DENALI_CTL(116), Channel, ddrc_116.value);
		ddrc_117.value = MemDdrcRegRead(DENALI_CTL(117), Channel);
		ddrc_117.fields.RD_TO_WCK_SYNC_LOST_F2 = MemFsp2TimingsPtr->JedecTimings.Rd2WckSyncLost;
		ddrc_117.fields.WR_TO_WCK_SYNC_LOST_F2 = MemFsp2TimingsPtr->JedecTimings.Wr2WckSyncLost;
		MemDdrcRegWrite(DENALI_CTL(117), Channel, ddrc_117.value);
		ddrc_174.value = MemDdrcRegRead(DENALI_CTL(174), Channel);
		ddrc_174.fields.CKSRE_F1 = MemFsp1TimingsPtr->JedecTimings.CKSRE;
		ddrc_174.fields.CKSRE_F2 = MemFsp2TimingsPtr->JedecTimings.CKSRE;
		ddrc_174.fields.CKSRX_F1 = MemFsp1TimingsPtr->JedecTimings.CKSRX;
		MemDdrcRegWrite(DENALI_CTL(174), Channel, ddrc_174.value);
		ddrc_175.value = MemDdrcRegRead(DENALI_CTL(175), Channel);
		ddrc_175.fields.CKSRX_F2 = MemFsp2TimingsPtr->JedecTimings.CKSRX;
		MemDdrcRegWrite(DENALI_CTL(175), Channel, ddrc_175.value);
		ddrc_318.value = MemDdrcRegRead(DENALI_CTL(318), Channel);
		ddrc_318.fields.TZQCAL_F1 = MemFsp1TimingsPtr->JedecTimings.Tzqcal;
		MemDdrcRegWrite(DENALI_CTL(318), Channel, ddrc_318.value);
		ddrc_319.value = MemDdrcRegRead(DENALI_CTL(319), Channel);
		ddrc_319.fields.TZQLAT_F1 = MemFsp1TimingsPtr->JedecTimings.Tzqlat;
		MemDdrcRegWrite(DENALI_CTL(319), Channel, ddrc_319.value);
		ddrc_321.value = MemDdrcRegRead(DENALI_CTL(321), Channel);
		ddrc_321.fields.TZQCAL_F2 = MemFsp2TimingsPtr->JedecTimings.Tzqcal;
		ddrc_321.fields.TZQLAT_F2 = MemFsp2TimingsPtr->JedecTimings.Tzqlat;
		MemDdrcRegWrite(DENALI_CTL(321), Channel, ddrc_321.value);
		MemDdrcRegWrite(DENALI_CTL(323), Channel, MemFsp1TimingsPtr->JedecTimings.Tzqreset |
				(uint32_t)MemFsp2TimingsPtr->JedecTimings.Tzqreset << 16);
		ddrc_402.value = MemDdrcRegRead(DENALI_CTL(402), Channel);
		ddrc_402.fields.TODTUP_F1 = MemFsp1TimingsPtr->JedecTimings.Todtup;
		ddrc_402.fields.TODTUP_F2 = MemFsp2TimingsPtr->JedecTimings.Todtup;
		MemDdrcRegWrite(DENALI_CTL(402), Channel, ddrc_402.value);
		ddrc_403.value = MemDdrcRegRead(DENALI_CTL(403), Channel);
		ddrc_403.fields.TODTL_2CMD_F1 = MemFsp1TimingsPtr->JedecTimings.Todtl_2cmd;
		MemDdrcRegWrite(DENALI_CTL(403), Channel, ddrc_403.value);
		ddrc_404.value = MemDdrcRegRead(DENALI_CTL(404), Channel);
		ddrc_404.fields.TODTH_RD_F1 = MemFsp1TimingsPtr->JedecTimings.Todthrd;
		ddrc_404.fields.TODTH_WR_F1 = MemFsp1TimingsPtr->JedecTimings.Todthwr;
		ddrc_404.fields.TODTH_WR_F2 = MemFsp2TimingsPtr->JedecTimings.Todthwr;
		ddrc_404.fields.TODTL_2CMD_F2 = MemFsp2TimingsPtr->JedecTimings.Todtl_2cmd;
		MemDdrcRegWrite(DENALI_CTL(404), Channel, ddrc_404.value);
		ddrc_405.value = MemDdrcRegRead(DENALI_CTL(405), Channel);
		ddrc_405.fields.TODTH_RD_F2 = MemFsp2TimingsPtr->JedecTimings.Todthrd;
		MemDdrcRegWrite(DENALI_CTL(405), Channel, ddrc_405.value);
		ddrc_408.value = MemDdrcRegRead(DENALI_CTL(408), Channel);
		ddrc_408.fields.RD_TO_ODTH_F1 = MemFsp1TimingsPtr->JedecTimings.Rd2Odth;
		ddrc_408.fields.RD_TO_ODTH_F2 = MemFsp2TimingsPtr->JedecTimings.Rd2Odth;
		MemDdrcRegWrite(DENALI_CTL(408), Channel, ddrc_408.value);
		ddrc_414.value = MemDdrcRegRead(DENALI_CTL(414), Channel);
		ddrc_414.fields.TDQSCK_MAX_F1 = MemFsp1TimingsPtr->JedecTimings.Tdqsckmax;
		ddrc_414.fields.TDQSCK_MIN_F1 = MemFsp1TimingsPtr->JedecTimings.Tdqsckmin;
		MemDdrcRegWrite(DENALI_CTL(414), Channel, ddrc_414.value);
		ddrc_415.value = MemDdrcRegRead(DENALI_CTL(415), Channel);
		ddrc_415.fields.TDQSCK_MAX_F2 = MemFsp2TimingsPtr->JedecTimings.Tdqsckmax;
		ddrc_415.fields.TDQSCK_MIN_F2 = MemFsp2TimingsPtr->JedecTimings.Tdqsckmin;
		MemDdrcRegWrite(DENALI_CTL(415), Channel, ddrc_415.value);
		ddrc_553.value = MemDdrcRegRead(DENALI_CTL(553), Channel);
		ddrc_553.fields.TAAD = MemInitPtr->GlobalTimings.TAAD;
		MemDdrcRegWrite(DENALI_CTL(553), Channel, ddrc_553.value);
		ddrc_554.value = MemDdrcRegRead(DENALI_CTL(554), Channel);
		ddrc_554.fields.NWR_F1 = MemFsp1TimingsPtr->JedecTimings.NWR;
		ddrc_554.fields.NWR_F2 = MemFsp2TimingsPtr->JedecTimings.NWR;
		MemDdrcRegWrite(DENALI_CTL(554), Channel, ddrc_554.value);
		ddrc_616.value = 0;
		ddrc_616.fields.TREFIE_F1 = MemFsp1TimingsPtr->JedecTimings.Tref;
		MemDdrcRegWrite(DENALI_CTL(616), Channel, ddrc_616.value);
		ddrc_617.value = 0;
		ddrc_617.fields.TREFIE_F2 = MemFsp2TimingsPtr->JedecTimings.Tref;
		MemDdrcRegWrite(DENALI_CTL(617), Channel, ddrc_617.value);
	}
	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel))
			continue;
		MemGetTechFuncPtr()->MemGetTurnaroundTiming(Channel, 1, &MemFsp1TurnaroundTimings);
		MemGetTechFuncPtr()->MemGetTurnaroundTiming(Channel, 2, &MemFsp2TurnaroundTimings);
		ddrc_409.value = MemDdrcRegRead(DENALI_CTL(409), Channel);
		ddrc_409.fields.RW2MRW_DLY_F1 = MemFsp1TurnaroundTimings.RW2MRW_DLY;
		ddrc_409.fields.RW2MRW_DLY_F2 = MemFsp2TurnaroundTimings.RW2MRW_DLY;
		MemDdrcRegWrite(DENALI_CTL(409), Channel, ddrc_409.value);
		ddrc_410.value = MemDdrcRegRead(DENALI_CTL(410), Channel);
		ddrc_410.fields.R2R_DIFFCS_DLY_F1 = MemFsp1TurnaroundTimings.R2R_DIFFCS_DLY;
		ddrc_410.fields.R2W_DIFFCS_DLY_F1 = MemFsp1TurnaroundTimings.R2W_DIFFCS_DLY;
		MemDdrcRegWrite(DENALI_CTL(410), Channel, ddrc_410.value);
		ddrc_411.value = MemDdrcRegRead(DENALI_CTL(411), Channel);
		ddrc_411.fields.R2R_DIFFCS_DLY_F2 = MemFsp2TurnaroundTimings.R2R_DIFFCS_DLY;
		ddrc_411.fields.R2W_DIFFCS_DLY_F2 = MemFsp2TurnaroundTimings.R2W_DIFFCS_DLY;
		ddrc_411.fields.W2R_DIFFCS_DLY_F1 = MemFsp1TurnaroundTimings.W2R_DIFFCS_DLY;
		ddrc_411.fields.W2W_DIFFCS_DLY_F1 = MemFsp1TurnaroundTimings.W2W_DIFFCS_DLY;
		MemDdrcRegWrite(DENALI_CTL(411), Channel, ddrc_411.value);
		ddrc_412.value = MemDdrcRegRead(DENALI_CTL(412), Channel);
		ddrc_412.fields.R2W_SAMECS_DLY_F1 = MemFsp1TurnaroundTimings.R2W_SAMECS_DLY;
		ddrc_412.fields.W2R_DIFFCS_DLY_F2 = MemFsp2TurnaroundTimings.W2R_DIFFCS_DLY;
		ddrc_412.fields.W2W_DIFFCS_DLY_F2 = MemFsp2TurnaroundTimings.W2W_DIFFCS_DLY;
		MemDdrcRegWrite(DENALI_CTL(412), Channel, ddrc_412.value);
		ddrc_413.value = MemDdrcRegRead(DENALI_CTL(413), Channel);
		ddrc_413.fields.R2W_SAMECS_DLY_F2 = MemFsp2TurnaroundTimings.R2W_SAMECS_DLY;
		MemDdrcRegWrite(DENALI_CTL(413), Channel, ddrc_413.value);
	}
	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel))
			continue;
		MemFsp1TimingsPtr = GetMemTimingsPtr(Channel, 1);
		MemFsp2TimingsPtr = GetMemTimingsPtr(Channel, 2);
		ddrc_101.value = 0;
		ddrc_101.fields.UPD_CTRLUPD_NORM_THRESHOLD_F1 = MemFsp1TimingsPtr->DfiTimings.Upd_ctrlupd_norm_threshold;
		MemDdrcRegWrite(DENALI_CTL(101), Channel, ddrc_101.value);
		ddrc_102.value = 0;
		ddrc_102.fields.UPD_CTRLUPD_HIGH_THRESHOLD_F1 = MemFsp1TimingsPtr->DfiTimings.Upd_ctrlupd_high_threshold;
		ddrc_102.fields.UPD_CTRLUPD_TIMEOUT_F1 = MemFsp1TimingsPtr->DfiTimings.Upd_ctrlupd_timeout;
		MemDdrcRegWrite(DENALI_CTL(102), Channel, ddrc_102.value);
		ddrc_103.value = 0;
		ddrc_103.fields.UPD_CTRLUPD_SW_PROMOTE_THRESHOLD_F1 = MemFsp1TimingsPtr->DfiTimings.Upd_ctrlupd_sw_promote_threshold;
		ddrc_103.fields.UPD_PHYUPD_DFI_PROMOTE_THRESHOLD_F1 = MemFsp1TimingsPtr->DfiTimings.Upd_phyupd_dfi_promote_threshold;
		MemDdrcRegWrite(DENALI_CTL(103), Channel, ddrc_103.value);
		ddrc_104.value = 0;
		ddrc_104.fields.UPD_CTRLUPD_HIGH_THRESHOLD_F2 = MemFsp2TimingsPtr->DfiTimings.Upd_ctrlupd_high_threshold;
		ddrc_104.fields.UPD_CTRLUPD_NORM_THRESHOLD_F2 = MemFsp2TimingsPtr->DfiTimings.Upd_ctrlupd_norm_threshold;
		MemDdrcRegWrite(DENALI_CTL(104), Channel, ddrc_104.value);
		ddrc_105.value = 0;
		ddrc_105.fields.UPD_CTRLUPD_SW_PROMOTE_THRESHOLD_F2 = MemFsp2TimingsPtr->DfiTimings.Upd_ctrlupd_sw_promote_threshold;
		ddrc_105.fields.UPD_CTRLUPD_TIMEOUT_F2 = MemFsp2TimingsPtr->DfiTimings.Upd_ctrlupd_timeout;
		MemDdrcRegWrite(DENALI_CTL(105), Channel, ddrc_105.value);
		ddrc_106.value = MemDdrcRegRead(DENALI_CTL(106), Channel);
		ddrc_106.fields.UPD_PHYUPD_DFI_PROMOTE_THRESHOLD_F2 = MemFsp2TimingsPtr->DfiTimings.Upd_phyupd_dfi_promote_threshold;
		MemDdrcRegWrite(DENALI_CTL(106), Channel, ddrc_106.value);
		ddrc_109.value = MemDdrcRegRead(DENALI_CTL(109), Channel);
		ddrc_109.fields.TDFI_WCK_EN_FS_F1 = MemFsp1TimingsPtr->DfiTimings.Twck_en_fs;
		ddrc_109.fields.TDFI_WCK_EN_RD_F1 = MemFsp1TimingsPtr->DfiTimings.Twck_en_rd;
		ddrc_109.fields.TDFI_WCK_EN_WR_F1 = MemFsp1TimingsPtr->DfiTimings.Twck_en_wr;
		ddrc_109.fields.TDFI_WCK_TOGGLE_F1 = MemFsp1TimingsPtr->DfiTimings.Twck_toggle;
		MemDdrcRegWrite(DENALI_CTL(109), Channel, ddrc_109.value);
		ddrc_110.value = MemDdrcRegRead(DENALI_CTL(110), Channel);
		ddrc_110.fields.TDFI_WCK_DIS_F1 = MemFsp1TimingsPtr->DfiTimings.Twck_dis;
		ddrc_110.fields.TDFI_WCK_EN_RD_F2 = MemFsp2TimingsPtr->DfiTimings.Twck_en_rd;
		ddrc_110.fields.TDFI_WCK_FAST_TOGGLE_F1 = MemFsp1TimingsPtr->DfiTimings.Twck_fast_toggle;
		ddrc_110.fields.TDFI_WCK_STOP_F1 = MemFsp1TimingsPtr->DfiTimings.Twck_stop;
		MemDdrcRegWrite(DENALI_CTL(110), Channel, ddrc_110.value);
		ddrc_111.value = MemDdrcRegRead(DENALI_CTL(111), Channel);
		ddrc_111.fields.TDFI_WCK_EN_FS_F2 = MemFsp2TimingsPtr->DfiTimings.Twck_en_fs;
		ddrc_111.fields.TDFI_WCK_EN_WR_F2 = MemFsp2TimingsPtr->DfiTimings.Twck_en_wr;
		ddrc_111.fields.TDFI_WCK_FAST_TOGGLE_F2 = MemFsp2TimingsPtr->DfiTimings.Twck_fast_toggle;
		ddrc_111.fields.TDFI_WCK_TOGGLE_F2 = MemFsp2TimingsPtr->DfiTimings.Twck_toggle;
		MemDdrcRegWrite(DENALI_CTL(111), Channel, ddrc_111.value);
		ddrc_112.value = MemDdrcRegRead(DENALI_CTL(112), Channel);
		ddrc_112.fields.TDFI_WCK_DIS_F2 = MemFsp2TimingsPtr->DfiTimings.Twck_dis;
		ddrc_112.fields.TDFI_WCK_STOP_F2 = MemFsp2TimingsPtr->DfiTimings.Twck_stop;
		MemDdrcRegWrite(DENALI_CTL(112), Channel, ddrc_112.value);
		MemDdrcRegWrite(DENALI_CTL(125), Channel, MemFsp1TimingsPtr->DfiTimings.Tphymstr_max);
		MemDdrcRegWrite(DENALI_CTL(126), Channel, MemFsp1TimingsPtr->DfiTimings.Tphymstr_type0);
		MemDdrcRegWrite(DENALI_CTL(127), Channel, MemFsp1TimingsPtr->DfiTimings.Tphymstr_type1);
		MemDdrcRegWrite(DENALI_CTL(128), Channel, MemFsp1TimingsPtr->DfiTimings.Tphymstr_type2);
		MemDdrcRegWrite(DENALI_CTL(129), Channel, MemFsp1TimingsPtr->DfiTimings.Tphymstr_type3);
		MemDdrcRegWrite(DENALI_CTL(131), Channel, MemFsp1TimingsPtr->DfiTimings.Tphymstr_resp);
		MemDdrcRegWrite(DENALI_CTL(132), Channel, MemFsp2TimingsPtr->DfiTimings.Tphymstr_max);
		MemDdrcRegWrite(DENALI_CTL(133), Channel, MemFsp2TimingsPtr->DfiTimings.Tphymstr_type0);
		MemDdrcRegWrite(DENALI_CTL(134), Channel, MemFsp2TimingsPtr->DfiTimings.Tphymstr_type1);
		MemDdrcRegWrite(DENALI_CTL(135), Channel, MemFsp2TimingsPtr->DfiTimings.Tphymstr_type2);
		MemDdrcRegWrite(DENALI_CTL(136), Channel, MemFsp2TimingsPtr->DfiTimings.Tphymstr_type3);
		ddrc_138.value = MemDdrcRegRead(DENALI_CTL(138), Channel);
		ddrc_138.fields.TDFI_PHYMSTR_RESP_F2 = MemFsp2TimingsPtr->DfiTimings.Tphymstr_resp;
		MemDdrcRegWrite(DENALI_CTL(138), Channel, ddrc_138.value);
		MemDdrcRegWrite(DENALI_CTL(522), Channel, MemFsp1TimingsPtr->DfiTimings.Tctrlupd_max);
		MemDdrcRegWrite(DENALI_CTL(523), Channel, MemFsp1TimingsPtr->DfiTimings.Tphyupd_type0);
		MemDdrcRegWrite(DENALI_CTL(524), Channel, MemFsp1TimingsPtr->DfiTimings.Tphyupd_type1);
		MemDdrcRegWrite(DENALI_CTL(525), Channel, MemFsp1TimingsPtr->DfiTimings.Tphyupd_type2);
		MemDdrcRegWrite(DENALI_CTL(526), Channel, MemFsp1TimingsPtr->DfiTimings.Tphyupd_type3);
		MemDdrcRegWrite(DENALI_CTL(527), Channel, MemFsp1TimingsPtr->DfiTimings.Tphyupd_resp);
		MemDdrcRegWrite(DENALI_CTL(528), Channel, MemFsp1TimingsPtr->DfiTimings.Tctrlupd_interval);
		MemDdrcRegWrite(DENALI_CTL(533), Channel, MemFsp2TimingsPtr->DfiTimings.Tctrlupd_max);
		MemDdrcRegWrite(DENALI_CTL(534), Channel, MemFsp2TimingsPtr->DfiTimings.Tphyupd_type0);
		MemDdrcRegWrite(DENALI_CTL(535), Channel, MemFsp2TimingsPtr->DfiTimings.Tphyupd_type1);
		MemDdrcRegWrite(DENALI_CTL(536), Channel, MemFsp2TimingsPtr->DfiTimings.Tphyupd_type2);
		MemDdrcRegWrite(DENALI_CTL(537), Channel, MemFsp2TimingsPtr->DfiTimings.Tphyupd_type3);
		MemDdrcRegWrite(DENALI_CTL(538), Channel, MemFsp2TimingsPtr->DfiTimings.Tphyupd_resp);
		MemDdrcRegWrite(DENALI_CTL(539), Channel, MemFsp2TimingsPtr->DfiTimings.Tctrlupd_interval);
		ddrc_521.value = 0;
		ddrc_521.fields.TDFI_PHY_RDLAT_F1 = MemFsp1TimingsPtr->DfiTimings.Tphy_rdlat;
		MemDdrcRegWrite(DENALI_CTL(521), Channel, ddrc_521.value);
		ddrc_532.value = 0;
		ddrc_532.fields.TDFI_PHY_RDLAT_F2 = MemFsp2TimingsPtr->DfiTimings.Tphy_rdlat;
		MemDdrcRegWrite(DENALI_CTL(532), Channel, ddrc_532.value);
		ddrc_529.value = 0;
		ddrc_529.fields.TDFI_RDCSLAT_F1 = MemFsp1TimingsPtr->DfiTimings.Tphy_rdcslat;
		ddrc_529.fields.TDFI_PHY_WRDATA_F1 = MemFsp1TimingsPtr->DfiTimings.Tphy_wrdata;
		ddrc_529.fields.TDFI_CTRL_DELAY_F1 = MemFsp1TimingsPtr->DfiTimings.Tctrl_delay;
		MemDdrcRegWrite(DENALI_CTL(529), Channel, ddrc_529.value);
		ddrc_530.value = 0;
		ddrc_530.fields.TDFI_WRCSLAT_F1 = MemFsp1TimingsPtr->DfiTimings.Tphy_wrcslat;
		ddrc_530.fields.TDFI_RDDATA_EN_F1 = MemFsp1TimingsPtr->DfiTimings.Trddata_en;
		MemDdrcRegWrite(DENALI_CTL(530), Channel, ddrc_530.value);
		ddrc_531.value = 0;
		ddrc_531.fields.TDFI_CTRLMSG_RESP_F1 = MemFsp1TimingsPtr->DfiTimings.Tctrlmsg_resp;
		ddrc_531.fields.TDFI_PHY_WRLAT_F1 = MemFsp1TimingsPtr->DfiTimings.Tphy_wrlat;
		MemDdrcRegWrite(DENALI_CTL(531), Channel, ddrc_531.value);
		ddrc_540.value = 0;
		ddrc_540.fields.TDFI_RDCSLAT_F2 = MemFsp2TimingsPtr->DfiTimings.Tphy_rdcslat;
		ddrc_540.fields.TDFI_PHY_WRDATA_F2 = MemFsp2TimingsPtr->DfiTimings.Tphy_wrdata;
		ddrc_540.fields.TDFI_CTRL_DELAY_F2 = MemFsp2TimingsPtr->DfiTimings.Tctrl_delay;
		MemDdrcRegWrite(DENALI_CTL(540), Channel, ddrc_540.value);
		ddrc_541.value = 0;
		ddrc_541.fields.TDFI_WRCSLAT_F2 = MemFsp2TimingsPtr->DfiTimings.Tphy_wrcslat;
		ddrc_541.fields.TDFI_RDDATA_EN_F2 = MemFsp2TimingsPtr->DfiTimings.Trddata_en;
		MemDdrcRegWrite(DENALI_CTL(541), Channel, ddrc_541.value);
		ddrc_542.value = 0;
		ddrc_542.fields.TDFI_CTRLMSG_RESP_F2 = MemFsp2TimingsPtr->DfiTimings.Tctrlmsg_resp;
		ddrc_542.fields.TDFI_PHY_WRLAT_F2 = MemFsp2TimingsPtr->DfiTimings.Tphy_wrlat;
		MemDdrcRegWrite(DENALI_CTL(542), Channel, ddrc_542.value);
	}
}

/* MemPhyTimingCfg: the PHY's timings of FSP 1 and 2 (frequency set index
 * 0 and 1), per data slice and for the address/command slices */
void MemPhyTimingCfg(void)
{
	MEMORY_INIT_S *MemInitPtr = GetMemInitPtr();
	PHY_TIMING_S PhyTiming;
	DENALI_PHY_31 ddrp_31;
	DENALI_PHY_67 ddrp_67;
	DENALI_PHY_70 ddrp_70;
	DENALI_PHY_78 ddrp_78;
	DENALI_PHY_83 ddrp_83;
	DENALI_PHY_84 ddrp_84;
	DENALI_PHY_90 ddrp_90;
	DENALI_PHY_93 ddrp_93;
	DENALI_PHY_94 ddrp_94;
	DENALI_PHY_104 ddrp_104;
	DENALI_PHY_112 ddrp_112;
	DENALI_PHY_122 ddrp_122;
	DENALI_PHY_123 ddrp_123;
	DENALI_PHY_124 ddrp_124;
	DENALI_PHY_1027 ddrp_1027;
	DENALI_PHY_1030 ddrp_1030;
	DENALI_PHY_1043 ddrp_1043;
	DENALI_PHY_1281 ddrp_1281;
	DENALI_PHY_1316 ddrp_1316;
	DENALI_PHY_1361 ddrp_1361;
	DENALI_PHY_1364 ddrp_1364;
	uint8_t Channel, DataSlice, fsp;
	uint32_t val32;

	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel))
			continue;
		ddrp_1281.value = MemDdrcRegRead(DENALI_PHY(1281), Channel);
		ddrp_1281.fields.PHY_FREQ_SEL_MULTICAST_EN = 0;
		for (fsp = 0; fsp < 2; fsp++) {
			MemGetTechFuncPtr()->MemGetPhyTiming(Channel, fsp + 1, &PhyTiming);
			ddrp_1281.fields.PHY_FREQ_SEL_INDEX = fsp;
			MemDdrcRegWrite(DENALI_PHY(1281), Channel, ddrp_1281.value);
			for (DataSlice = 0; DataSlice < 4; DataSlice++) {
				ddrp_31.value = MemDdrcRegRead(DENALI_PHY(31) + PHY_SLICE(DataSlice), Channel);
				ddrp_31.fields.PHY_WRLVL_PER_START_0 = PhyTiming.PHY_WRLVL_PER_START;
				MemDdrcRegWrite(DENALI_PHY(31) + PHY_SLICE(DataSlice), Channel, ddrp_31.value);
				ddrp_67.value = MemDdrcRegRead(DENALI_PHY(67) + PHY_SLICE(DataSlice), Channel);
				ddrp_67.fields.PHY_REGULATOR_EN_CNT_0 = PhyTiming.PHY_REGULATOR_EN_CNT;
				MemDdrcRegWrite(DENALI_PHY(67) + PHY_SLICE(DataSlice), Channel, ddrp_67.value);
				ddrp_70.value = MemDdrcRegRead(DENALI_PHY(70) + PHY_SLICE(DataSlice), Channel);
				ddrp_70.fields.PHY_PST_AMBLE_0 = PhyTiming.PHY_PST_AMBLE;
				MemDdrcRegWrite(DENALI_PHY(70) + PHY_SLICE(DataSlice), Channel, ddrp_70.value);
				ddrp_78.value = MemDdrcRegRead(DENALI_PHY(78) + PHY_SLICE(DataSlice), Channel);
				ddrp_78.fields.PHY_PAD_DSLICE_IO_CFG_0 = PhyTiming.PHY_PAD_DSLICE_IO_CFG;
				MemDdrcRegWrite(DENALI_PHY(78) + PHY_SLICE(DataSlice), Channel, ddrp_78.value);
				ddrp_83.value = MemDdrcRegRead(DENALI_PHY(83) + PHY_SLICE(DataSlice), Channel);
				ddrp_83.fields.PHY_RX_CAL_ALL_DLY_0 = PhyTiming.PHY_RX_CAL_ALL_DLY;
				MemDdrcRegWrite(DENALI_PHY(83) + PHY_SLICE(DataSlice), Channel, ddrp_83.value);
				ddrp_84.value = MemDdrcRegRead(DENALI_PHY(84) + PHY_SLICE(DataSlice), Channel);
				ddrp_84.fields.PHY_RX_PCLK_CLK_SEL_0 = PhyTiming.PHY_RX_PCLK_CLK_SEL;
				MemDdrcRegWrite(DENALI_PHY(84) + PHY_SLICE(DataSlice), Channel, ddrp_84.value);
				ddrp_90.value = MemDdrcRegRead(DENALI_PHY(90) + PHY_SLICE(DataSlice), Channel);
				ddrp_90.fields.PHY_RDDATA_EN_OE_DLY_0 = PhyTiming.PHY_RDDATA_EN_OE_DLY;
				ddrp_90.fields.PHY_RDDATA_EN_TSEL_DLY_0 = PhyTiming.PHY_RDDATA_EN_TSEL_DLY;
				MemDdrcRegWrite(DENALI_PHY(90) + PHY_SLICE(DataSlice), Channel, ddrp_90.value);
				ddrp_93.value = MemDdrcRegRead(DENALI_PHY(93) + PHY_SLICE(DataSlice), Channel);
				ddrp_93.fields.PHY_RPTR_UPDATE_0 = PhyTiming.PHY_RPTR_UPDATE;
				MemDdrcRegWrite(DENALI_PHY(93) + PHY_SLICE(DataSlice), Channel, ddrp_93.value);
				ddrp_104.value = MemDdrcRegRead(DENALI_PHY(104) + PHY_SLICE(DataSlice), Channel);
				ddrp_104.fields.PHY_RDDATA_EN_DLY_0 = PhyTiming.PHY_RDDATA_EN_DLY;
				MemDdrcRegWrite(DENALI_PHY(104) + PHY_SLICE(DataSlice), Channel, ddrp_104.value);
				/* read DQS delays of DQ0-7 and DM, rising and falling edge
				 * (registers 113-121, no field types) */
				val32 = PhyTiming.PHY_RDDQS_DQx_RISE_FALL_SLAVE_DELAY |
					(uint32_t)PhyTiming.PHY_RDDQS_DQx_RISE_FALL_SLAVE_DELAY << 16;
				MemDdrcRegWrite(DENALI_PHY(113) + PHY_SLICE(DataSlice), Channel, val32);
				MemDdrcRegWrite(DENALI_PHY(114) + PHY_SLICE(DataSlice), Channel, val32);
				MemDdrcRegWrite(DENALI_PHY(115) + PHY_SLICE(DataSlice), Channel, val32);
				MemDdrcRegWrite(DENALI_PHY(116) + PHY_SLICE(DataSlice), Channel, val32);
				MemDdrcRegWrite(DENALI_PHY(117) + PHY_SLICE(DataSlice), Channel, val32);
				MemDdrcRegWrite(DENALI_PHY(118) + PHY_SLICE(DataSlice), Channel, val32);
				MemDdrcRegWrite(DENALI_PHY(119) + PHY_SLICE(DataSlice), Channel, val32);
				MemDdrcRegWrite(DENALI_PHY(120) + PHY_SLICE(DataSlice), Channel, val32);
				MemDdrcRegWrite(DENALI_PHY(121) + PHY_SLICE(DataSlice), Channel, val32);
				ddrp_94.value = MemDdrcRegRead(DENALI_PHY(94) + PHY_SLICE(DataSlice), Channel);
				ddrp_94.fields.PHY_WRLVL_RESP_WAIT_CNT_0 = PhyTiming.PHY_WRLVL_RESP_WAIT_CNT;
				MemDdrcRegWrite(DENALI_PHY(94) + PHY_SLICE(DataSlice), Channel, ddrp_94.value);
				MemDdrcRegWrite(DENALI_PHY(98) + PHY_SLICE(DataSlice), Channel, PhyTiming.PHY_RDLVL_MAX_EDGE);
				/* write DQ delays (registers 107-111, no field types) */
				val32 = PhyTiming.PHY_CLK_WRDQx_SLAVE_DELAY | (uint32_t)PhyTiming.PHY_CLK_WRDQx_SLAVE_DELAY << 16;
				MemDdrcRegWrite(DENALI_PHY(107) + PHY_SLICE(DataSlice), Channel, val32);
				MemDdrcRegWrite(DENALI_PHY(108) + PHY_SLICE(DataSlice), Channel, val32);
				MemDdrcRegWrite(DENALI_PHY(109) + PHY_SLICE(DataSlice), Channel, val32);
				MemDdrcRegWrite(DENALI_PHY(110) + PHY_SLICE(DataSlice), Channel, val32);
				MemDdrcRegWrite(DENALI_PHY(111) + PHY_SLICE(DataSlice), Channel, val32);
				ddrp_112.value = MemDdrcRegRead(DENALI_PHY(112) + PHY_SLICE(DataSlice), Channel);
				ddrp_112.fields.PHY_WRITE_PATH_LAT_DEC_0 = PhyTiming.PHY_WRITE_PATH_LAT_DEC[DataSlice];
				MemDdrcRegWrite(DENALI_PHY(112) + PHY_SLICE(DataSlice), Channel, ddrp_112.value);
				ddrp_122.value = MemDdrcRegRead(DENALI_PHY(122) + PHY_SLICE(DataSlice), Channel);
				ddrp_122.fields.PHY_RDDQS_LATENCY_ADJUST_0 = PhyTiming.PHY_RDDQS_LATENCY_ADJUST[DataSlice];
				ddrp_122.fields.PHY_RDDQS_GATE_SLAVE_DELAY_0 = PhyTiming.PHY_RDDQS_GATE_SLAVE_DELAY[DataSlice];
				ddrp_122.fields.PHY_WRITE_PATH_LAT_ADD_0 = PhyTiming.PHY_WRITE_PATH_LAT_ADD[DataSlice];
				MemDdrcRegWrite(DENALI_PHY(122) + PHY_SLICE(DataSlice), Channel, ddrp_122.value);
				ddrp_123.value = MemDdrcRegRead(DENALI_PHY(123) + PHY_SLICE(DataSlice), Channel);
				ddrp_123.fields.PHY_WRITE_PATH_LAT_FRAC_0 = PhyTiming.PHY_WRITE_PATH_LAT_FRAC[DataSlice];
				MemDdrcRegWrite(DENALI_PHY(123) + PHY_SLICE(DataSlice), Channel, ddrp_123.value);
				ddrp_124.value = MemDdrcRegRead(DENALI_PHY(124) + PHY_SLICE(DataSlice), Channel);
				ddrp_124.fields.PHY_GTLVL_LAT_ADJ_START_0 = PhyTiming.PHY_GTLVL_LAT_ADJ_START[DataSlice];
				MemDdrcRegWrite(DENALI_PHY(124) + PHY_SLICE(DataSlice), Channel, ddrp_124.value);
			}
			ddrp_1027.value = MemDdrcRegRead(DENALI_PHY(1027), Channel);
			ddrp_1027.fields.PHY_ADR_MEAS_DLY_STEP_ENABLE = PhyTiming.PHY_ADR_MEAS_DLY_STEP_ENABLE;
			MemDdrcRegWrite(DENALI_PHY(1027), Channel, ddrp_1027.value);
			ddrp_1043.value = MemDdrcRegRead(DENALI_PHY(1043), Channel);
			ddrp_1043.fields.PHY_ADR_CALVL_DLY_STEP = PhyTiming.PHY_ADR_CALVL_DLY_STEP;
			MemDdrcRegWrite(DENALI_PHY(1043), Channel, ddrp_1043.value);
			ddrp_1030.value = MemDdrcRegRead(DENALI_PHY(1030), Channel);
			ddrp_1030.fields.PHY_ADR_CALVL_RESP_WAIT_CNT = MemInitPtr->GlobalTimings.PHY_ADR_CALVL_RESP_WAIT_CNT;
			MemDdrcRegWrite(DENALI_PHY(1030), Channel, ddrp_1030.value);
			ddrp_1316.value = MemDdrcRegRead(DENALI_PHY(1316), Channel);
			ddrp_1316.fields.PHY_TCKSRE_WAIT = PhyTiming.PHY_TCKSRE_WAIT;
			MemDdrcRegWrite(DENALI_PHY(1316), Channel, ddrp_1316.value);
			ddrp_1361.value = MemDdrcRegRead(DENALI_PHY(1361), Channel);
			ddrp_1361.fields.PHY_PAD_ACS_IO_CFG = PhyTiming.PHY_PAD_ACS_IO_CFG;
			ddrp_1361.fields.PHY_PAD_ACS_RX_PCLK_CLK_SEL = PhyTiming.PHY_PAD_ACS_RX_PCLK_CLK_SEL;
			MemDdrcRegWrite(DENALI_PHY(1361), Channel, ddrp_1361.value);
			ddrp_1364.value = MemDdrcRegRead(DENALI_PHY(1364), Channel);
			ddrp_1364.fields.PHY_PLL_CTRL = PhyTiming.PHY_PLL_CTRL;
			ddrp_1364.fields.PHY_PLL_CTRL_8X = PhyTiming.PHY_PLL_CTRL_8X;
			MemDdrcRegWrite(DENALI_PHY(1364), Channel, ddrp_1364.value);
		}
		ddrp_1281.fields.PHY_FREQ_SEL_MULTICAST_EN = 1;
		MemDdrcRegWrite(DENALI_PHY(1281), Channel, ddrp_1281.value);
	}
}
