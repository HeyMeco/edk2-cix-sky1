/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Types, functions and variables of CIX's DDR driver (driver/ddr), from the
 * DWARF of CIX's SE firmware debug build (scripts/open/se_ddr_header.py),
 * for the files here that translate it.
 */
#ifndef MEM_TYPES_H
#define MEM_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef unsigned int uint;

typedef struct {
	uint8_t WckAlwaysOn;	/* +0x0 */
	uint8_t DataMask;	/* +0x1 */
	uint8_t AutoPrechargeEn;	/* +0x2 */
	uint8_t PbrEn;	/* +0x3 */
	uint8_t RfmEn;	/* +0x4 */
	uint8_t SelBankInQ;	/* +0x5 */
	uint8_t PortPriority;	/* +0x6 */
	uint8_t BdwP0;	/* +0x7 */
	uint8_t BdwP1;	/* +0x8 */
	uint8_t AddressMode;	/* +0x9 */
	uint8_t MbistEn;	/* +0xa */
	uint8_t MbistMode;	/* +0xb */
	uint8_t EyeScanEn;	/* +0xc */
	uint8_t BpEn;	/* +0xd */
	uint8_t WrLEcc;	/* +0xe */
	uint8_t RdLEcc;	/* +0xf */
	uint8_t RPriorityP0;	/* +0x10 */
	uint8_t WPriorityP0;	/* +0x11 */
	uint8_t BdwOvflowP0;	/* +0x12 */
	uint8_t RPriorityP1;	/* +0x13 */
	uint8_t WPriorityP1;	/* +0x14 */
	uint8_t BdwOvflowP1;	/* +0x15 */
	uint8_t TrainMode;	/* +0x16 */
	uint8_t IEcc;	/* +0x17 */
	uint8_t PeriodicTrain;	/* +0x18 */
	uint8_t SSC;	/* +0x19 */
	uint8_t DfsEn;	/* +0x1a */
	uint8_t rsvd;	/* +0x1b */
} MEM_CONFIG_FEATURE;

struct _LPDDR_JEDEC_TIMING_S {
	uint8_t WL;	/* +0x0 */
	uint8_t RL;	/* +0x1 */
	uint8_t NWR;	/* +0x2 */
	uint8_t Tosco;	/* +0x3 */
	uint16_t TrasMin;	/* +0x4 */
	uint16_t TrasMax;	/* +0x6 */
	uint8_t TrpPb;	/* +0x8 */
	uint8_t TrpAb;	/* +0x9 */
	uint8_t Trcd;	/* +0xa */
	uint8_t TrcdL;	/* +0xb */
	uint8_t Trtp;	/* +0xc */
	uint16_t Tfaw;	/* +0xe */
	uint8_t Trrd;	/* +0x10 */
	uint8_t TrrdL;	/* +0x11 */
	uint16_t Trc;	/* +0x12 */
	uint8_t Twtr;	/* +0x14 */
	uint8_t TwtrL;	/* +0x15 */
	uint8_t TwtrAP;	/* +0x16 */
	uint8_t TwtrLAP;	/* +0x17 */
	uint8_t Twr;	/* +0x18 */
	uint8_t Tppd;	/* +0x19 */
	uint8_t Tcke;	/* +0x1a */
	uint8_t Tckesr;	/* +0x1b */
	uint8_t Tcacsh;	/* +0x1c */
	uint8_t Tcsh;	/* +0x1d */
	uint16_t Tpdex;	/* +0x1e */
	uint16_t Txsr;	/* +0x20 */
	uint16_t Txsnr;	/* +0x22 */
	uint16_t TrfcAb;	/* +0x24 */
	uint16_t TrfcPb;	/* +0x26 */
	uint16_t Tzqcal;	/* +0x28 */
	uint8_t Tzqlat;	/* +0x2a */
	uint8_t Tzqcke;	/* +0x2b */
	uint8_t Tmrri;	/* +0x2c */
	uint8_t Tmrd;	/* +0x2d */
	uint8_t TccdL;	/* +0x2e */
	uint8_t Tccdmw;	/* +0x2f */
	uint32_t Tref;	/* +0x30 */
	uint8_t Tmod;	/* +0x34 */
	uint8_t Tsr;	/* +0x35 */
	uint16_t Todtup;	/* +0x36 */
	uint16_t Tpbr2pbr;	/* +0x38 */
	uint8_t Tpbr2act;	/* +0x3a */
	uint8_t Tdal;	/* +0x3b */
	uint8_t Tescke;	/* +0x3c */
	uint8_t Tcscke;	/* +0x3d */
	uint8_t Tcsckeh;	/* +0x3e */
	uint8_t Tcmdcke;	/* +0x3f */
	uint8_t Tdqsckmin;	/* +0x40 */
	uint8_t Tdqsckmax;	/* +0x41 */
	uint16_t Tcaent;	/* +0x42 */
	uint8_t Tcbtrtw;	/* +0x44 */
	uint16_t TvrefL;	/* +0x46 */
	uint16_t TvrefS;	/* +0x48 */
	uint16_t TvrcgDis;	/* +0x4a */
	uint16_t TvrcgEn;	/* +0x4c */
	uint8_t Tmrwckel;	/* +0x4e */
	uint8_t Twrlvl_wckoff;	/* +0x4f */
	uint16_t Tfc;	/* +0x50 */
	uint8_t Tckehcs;	/* +0x52 */
	uint8_t Tckelcs;	/* +0x53 */
	uint8_t Tckelpd;	/* +0x54 */
	uint16_t Tckckel;	/* +0x56 */
	uint8_t Tckehcmd;	/* +0x58 */
	uint8_t Tckelcmd;	/* +0x59 */
	uint8_t Todtl_2cmd;	/* +0x5a */
	uint8_t Todton_min;	/* +0x5b */
	uint8_t Todthrd;	/* +0x5c */
	uint8_t Todthwr;	/* +0x5d */
	uint16_t Trfm;	/* +0x5e */
	uint8_t Trfm2act;	/* +0x60 */
	uint16_t Trfmsb;	/* +0x62 */
	uint16_t Trfmth;	/* +0x64 */
	uint8_t Texcke;	/* +0x66 */
	uint8_t Tmrw;	/* +0x67 */
	uint8_t Tmrz;	/* +0x68 */
	uint8_t Tsdo;	/* +0x69 */
	uint8_t Txp;	/* +0x6a */
	uint8_t Tdq7hwck;	/* +0x6b */
	uint8_t Tdq7lwck;	/* +0x6c */
	uint16_t Tdq72dq;	/* +0x6e */
	uint8_t Twck2dq7h;	/* +0x70 */
	uint32_t Tinit0;	/* +0x74 */
	uint32_t Tinit1;	/* +0x78 */
	uint32_t Tinit3;	/* +0x7c */
	uint32_t Tinit4;	/* +0x80 */
	uint32_t Tinit5;	/* +0x84 */
	uint8_t Wr2WckSyncLost;	/* +0x88 */
	uint8_t Rd2WckSyncLost;	/* +0x89 */
	uint8_t Rd2Odth;	/* +0x8a */
	uint16_t Tzqreset;	/* +0x8c */
	uint8_t CKSRE;	/* +0x8e */
	uint8_t CKSRX;	/* +0x8f */
};

typedef struct _LPDDR_JEDEC_TIMING_S JEDEC_TIMING_S;

struct _DFI_TIMING_S {
	uint32_t Tctrlupd_max;	/* +0x0 */
	uint32_t Tctrlupd_interval;	/* +0x4 */
	uint16_t Tcalvl_capture;	/* +0x8 */
	uint16_t Tcalvl_cc;	/* +0xa */
	uint8_t Tcalvl_strobe;	/* +0xc */
	uint32_t Twck_en_fs;	/* +0x10 */
	uint32_t Twck_en_wr;	/* +0x14 */
	uint32_t Twck_en_rd;	/* +0x18 */
	uint8_t Mbist_twckenl_rd_adj;	/* +0x1c */
	uint32_t Twck_stop;	/* +0x20 */
	uint32_t Twck_dis;	/* +0x24 */
	uint32_t TwckpreStatic;	/* +0x28 */
	uint32_t TwckpreToggleWr;	/* +0x2c */
	uint32_t TwckpreToggleRd;	/* +0x30 */
	uint32_t Twck_fast_toggle;	/* +0x34 */
	uint32_t Twck_toggle;	/* +0x38 */
	uint32_t Tphymstr_max;	/* +0x3c */
	uint32_t Tphymstr_type0;	/* +0x40 */
	uint32_t Tphymstr_type1;	/* +0x44 */
	uint32_t Tphymstr_type2;	/* +0x48 */
	uint32_t Tphymstr_type3;	/* +0x4c */
	uint32_t Tphymstr_resp;	/* +0x50 */
	uint32_t Tinit_start;	/* +0x54 */
	uint32_t Tinit_complete;	/* +0x58 */
	uint32_t Tphy_rdlat;	/* +0x5c */
	uint32_t Tphyupd_type0;	/* +0x60 */
	uint32_t Tphyupd_type1;	/* +0x64 */
	uint32_t Tphyupd_type2;	/* +0x68 */
	uint32_t Tphyupd_type3;	/* +0x6c */
	uint32_t Tphyupd_resp;	/* +0x70 */
	uint32_t Tphy_rdcslat;	/* +0x74 */
	uint32_t Tphy_wrcslat;	/* +0x78 */
	uint32_t Tphy_wrdata;	/* +0x7c */
	uint32_t Tctrl_delay;	/* +0x80 */
	uint32_t Trddata_en;	/* +0x84 */
	uint32_t Trptr_update;	/* +0x88 */
	uint32_t Trddqs_latency_adjust;	/* +0x8c */
	uint32_t Tctrlmsg_resp;	/* +0x90 */
	uint32_t Tphy_wrlat;	/* +0x94 */
	uint16_t TwdqlvlRw;	/* +0x98 */
	uint16_t TwdqlvlWr;	/* +0x9a */
	uint16_t Twrlvl_ww;	/* +0x9c */
	uint32_t Upd_ctrlupd_high_threshold;	/* +0xa0 */
	uint32_t Upd_ctrlupd_norm_threshold;	/* +0xa4 */
	uint32_t Upd_ctrlupd_sw_promote_threshold;	/* +0xa8 */
	uint32_t Upd_ctrlupd_timeout;	/* +0xac */
	uint32_t Upd_phyupd_dfi_promote_threshold;	/* +0xb0 */
};

typedef struct _DFI_TIMING_S DFI_TIMING_S;

struct _LPDDR_TIMING_S {
	JEDEC_TIMING_S JedecTimings;	/* +0x0 */
	DFI_TIMING_S DfiTimings;	/* +0x90 */
};

typedef struct _LPDDR_TIMING_S LPDDR_TIMING_S;

struct _MEM_BUSCFG_ENTRY_S {
	uint8_t CA_ODT;	/* +0x0 */
	uint8_t CK_ODT;	/* +0x1 */
	uint8_t CS_ODT;	/* +0x2 */
	uint8_t DQ_ODT;	/* +0x3 */
	uint8_t WCK_ODT;	/* +0x4 */
	uint8_t SOC_ODT;	/* +0x5 */
	uint8_t NTDQ_ODT;	/* +0x6 */
	uint8_t ODT_PDDS;	/* +0x7 */
	uint16_t DqVref;	/* +0x8 */
	uint16_t CaVref;	/* +0xa */
	uint8_t WLSet;	/* +0xc */
	uint8_t RLSet;	/* +0xd */
};

typedef struct _MEM_BUSCFG_ENTRY_S MEM_BUSCFG_ENTRY_S;

struct _MEM_PHY_PAD_CFG_ENTRY_S {
	uint8_t FdbkDrv;	/* +0x0 */
	uint8_t FdbkOdt;	/* +0x1 */
	uint8_t RstDrv;	/* +0x2 */
	uint8_t RstTxFFE;	/* +0x3 */
	uint8_t DqDrv;	/* +0x4 */
	uint8_t DqOdt;	/* +0x5 */
	uint8_t DqTxFFE;	/* +0x6 */
	uint8_t DqRxDFE;	/* +0x7 */
	uint8_t DqsDrv;	/* +0x8 */
	uint8_t DqsOdt;	/* +0x9 */
	uint8_t DqsTxFFE;	/* +0xa */
	uint8_t CkDrv;	/* +0xb */
	uint8_t CkTxFFE;	/* +0xc */
	uint8_t CkeDrv;	/* +0xd */
	uint8_t CkeTxFFE;	/* +0xe */
	uint8_t CaDrv;	/* +0xf */
	uint8_t CaTxFFE;	/* +0x10 */
	uint8_t CsDrv;	/* +0x11 */
	uint8_t CsTxFFE;	/* +0x12 */
	uint8_t CTLE;	/* +0x13 */
};

typedef struct _MEM_PHY_PAD_CFG_ENTRY_S MEM_PHY_PAD_CFG_ENTRY_S;

struct _MEM_CHANNEL_S {
	uint8_t Enabled;	/* +0x0 */
	LPDDR_TIMING_S Timings[3];	/* +0x4 */
};

typedef struct _MEM_CHANNEL_S MEM_CHANNEL_S;

struct _MEM_FSP_CONFIG_S {
	uint16_t MemFreq;	/* +0x0 */
	uint8_t FreqRatio;	/* +0x2 */
	uint8_t BankArchMode;	/* +0x3 */
	MEM_BUSCFG_ENTRY_S BusCfg;	/* +0x4 */
	MEM_PHY_PAD_CFG_ENTRY_S PhyPadCfg;	/* +0x12 */
};

typedef struct _MEM_FSP_CONFIG_S MEM_FSP_CONFIG_S;

struct _MEM_GLOBAL_TIMING_S {
	uint8_t WLSet;	/* +0x0 */
	uint8_t RLSet;	/* +0x1 */
	uint32_t TSREF2PHYMSTR;	/* +0x4 */
	uint32_t TBST_INT_INTERVAL;	/* +0x8 */
	uint32_t TCCD;	/* +0xc */
	uint32_t TMRR;	/* +0x10 */
	uint32_t TRFC_OPT_THRESHOLD;	/* +0x14 */
	uint32_t TREF_INTERVAL;	/* +0x18 */
	uint32_t TAAD;	/* +0x1c */
	uint32_t TRAS_LOCKOUT;	/* +0x20 */
	uint32_t Tlp_resp;	/* +0x24 */
	uint32_t Tdram_clk_disable;	/* +0x28 */
	uint32_t Tdram_clk_enable;	/* +0x2c */
	uint32_t Tctrlupd_min;	/* +0x30 */
	uint32_t Twrdata_delay;	/* +0x34 */
	uint8_t PHY_ADR_CALVL_RESP_WAIT_CNT;	/* +0x38 */
};

typedef struct _MEM_GLOBAL_TIMING_S MEM_GLOBAL_TIMING_S;

struct _MEM_SPD_TIMING_S {
	uint32_t WckAvg;	/* +0x0 */
	uint32_t Taa;	/* +0x4 */
	uint32_t Trcdrd;	/* +0x8 */
	uint32_t Trcdwr;	/* +0xc */
	uint32_t TrpAb;	/* +0x10 */
	uint32_t TrpPb;	/* +0x14 */
	uint32_t TrfcAb;	/* +0x18 */
	uint32_t TrfcPb;	/* +0x1c */
	uint32_t Tpbr2pbr;	/* +0x20 */
	uint32_t Tzqcal;	/* +0x24 */
};

typedef struct _MEM_SPD_TIMING_S MEM_SPD_TIMING_S;

typedef MEM_CONFIG_FEATURE MEM_FEATURE_S;

struct _MEMORY_INIT_S {
	uint8_t DdrType;	/* +0x0 */
	uint8_t ChannelCount;	/* +0x1 */
	uint8_t ChannelMask;	/* +0x2 */
	uint8_t RanksPerChannel;	/* +0x3 */
	uint8_t DeviceWidth;	/* +0x4 */
	uint8_t BusWidth;	/* +0x5 */
	uint8_t BaBits;	/* +0x6 */
	uint8_t BgBits;	/* +0x7 */
	uint8_t ColBits;	/* +0x8 */
	uint8_t RowBits;	/* +0x9 */
	uint32_t TrainLvlType;	/* +0xc */
	uint32_t RetrainFilter;	/* +0x10 */
	uint8_t DqVrefSp[3];	/* +0x14 */
	uint8_t DqVrefEp[3];	/* +0x17 */
	uint8_t CaVrefSp[3];	/* +0x1a */
	uint8_t CaVrefEp[3];	/* +0x1d */
	uint8_t Axi2Dfi;	/* +0x20 */
	uint8_t DfsRatio;	/* +0x21 */
	uint8_t ByteMode;	/* +0x22 */
	MEM_FEATURE_S Features;	/* +0x23 */
	MEM_GLOBAL_TIMING_S GlobalTimings;	/* +0x40 */
	MEM_SPD_TIMING_S SpdTimings;	/* +0x7c */
	MEM_FSP_CONFIG_S GlobalFspConfig[3];	/* +0xa4 */
	MEM_CHANNEL_S Channel[4];	/* +0x118 */
	uint8_t Density;	/* +0x1058 */
	uint8_t DramPkgDetect;	/* +0x1059 */
	uint32_t MemorySize;	/* +0x105c */
};

typedef struct _MEMORY_INIT_S MEMORY_INIT_S;

typedef enum {
	MEM_BIST_MOVI1_3N = 0,
	MEM_BIST_LIMIT_MOVI = 1,
	MEM_BIST_SELF_REF_RETENTION = 2,
	MEM_BIST_IDLE_RETENTION = 3,
	MEM_BIST_MEM_INITIAL_PROGRAMMING = 4,
	MEM_BIST_DATA_CHECK_LIMIT = 5,
	MEM_BIST_DDRPI_MOVI1_3N = 0,
	MEM_BIST_DDRPI_MARCH_C = 1,
	MEM_BIST_DDRPI_GALPAT = 2,
	MEM_BIST_DDRPI_PRBS = 3,
	MEM_BIST_DDRPI_PROGRAMMABLE_MARCH = 4,
	MEM_BIST_DDRPI_DATA_CHECK_LIMIT = 5,
	MEM_BIST_DDRPI_BEST_COL = 0,
	MEM_BIST_DDRPI_BEST_BANK = 1,
	MEM_BIST_DDRPI_BEST_ROW = 2,
	MEM_BIST_DDRPI_ADDR_CHECK_LIMIT = 3,
} MEM_BIST_MODE;

typedef union {
	struct {
		uint32_t BIST_GO : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t BIST_RESULT : 1;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t ADDR_SPACE : 6;	/* +0x0 */
		uint32_t RESERVED_23_22 : 2;	/* +0x0 */
		uint32_t BIST_DATA_CHECK : 1;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_243;

typedef union {
	struct {
		uint32_t BIST_START_ADDRESS_0 : 32;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_244;

typedef union {
	struct {
		uint32_t BIST_START_ADDRESS_1 : 8;	/* +0x0 */
		uint32_t RESERVED_31_8 : 24;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_245;

typedef union {
	struct {
		uint32_t BIST_DATA_MASK_0 : 32;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_246;

typedef union {
	struct {
		uint32_t BIST_DATA_MASK_1 : 32;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_247;

typedef union {
	struct {
		uint32_t BIST_DATA_MASK_2 : 32;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_248;

typedef union {
	struct {
		uint32_t BIST_DATA_MASK_3 : 32;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_249;

typedef union {
	struct {
		uint32_t BIST_TEST_MODE : 3;	/* +0x0 */
		uint32_t RESERVED_31_3 : 29;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_250;

typedef union {
	struct {
		uint32_t BIST_RET_STATE : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t BIST_ERR_STOP : 12;	/* +0x0 */
		uint32_t RESERVED_31_20 : 12;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_259;

typedef union {
	struct {
		uint32_t BIST_ERR_COUNT : 12;	/* +0x0 */
		uint32_t RESERVED_15_12 : 4;	/* +0x0 */
		uint32_t BIST_RET_STATE_EXIT : 1;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t ECC_ENABLE : 2;	/* +0x0 */
		uint32_t RESERVED_31_26 : 6;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_260;

typedef union {
	struct {
		uint32_t MEM_DP_REDUCTION : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t WRITE_ADDR_CHAN_PARITY_EN : 1;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t WRITE_DATA_CHAN_PARITY_EN : 2;	/* +0x0 */
		uint32_t RESERVED_23_18 : 6;	/* +0x0 */
		uint32_t WRITE_RESP_CHAN_PARITY_EN : 1;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_336;

typedef union {
	struct {
		uint32_t Q_FULLNESS : 5;	/* +0x0 */
		uint32_t RESERVED_7_5 : 3;	/* +0x0 */
		uint32_t IN_ORDER_ACCEPT : 1;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t WR_ORDER_REQ : 2;	/* +0x0 */
		uint32_t RESERVED_23_18 : 6;	/* +0x0 */
		uint32_t CONTROLLER_BUSY : 1;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_343;

typedef union {
	struct {
		uint32_t PI_BIST_GO : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t PI_BIST_RESULT : 2;	/* +0x0 */
		uint32_t RESERVED_15_10 : 6;	/* +0x0 */
		uint32_t PI_BIST_LFSR_PATTERN_DONE : 1;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t PI_ADDR_SPACE : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_324;

typedef union {
	struct {
		uint32_t PI_BIST_DATA_CHECK : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t PI_BIST_ADDR_CHECK : 1;	/* +0x0 */
		uint32_t RESERVED_31_9 : 23;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_325;

typedef union {
	struct {
		uint32_t PI_BIST_START_ADDRESS_0 : 32;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_326;

typedef union {
	struct {
		uint32_t PI_BIST_START_ADDRESS_1 : 4;	/* +0x0 */
		uint32_t RESERVED_7_4 : 4;	/* +0x0 */
		uint32_t PI_MBIST_INIT_PATTERN : 8;	/* +0x0 */
		uint32_t PI_MBIST_R2W_SCALE : 3;	/* +0x0 */
		uint32_t RESERVED_23_19 : 5;	/* +0x0 */
		uint32_t PI_MBIST_R2R_SCALE : 3;	/* +0x0 */
		uint32_t RESERVED_31_27 : 5;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_327;

typedef union {
	struct {
		uint32_t PI_BIST_ADDR_MASK_9_1 : 5;	/* +0x0 */
		uint32_t RESERVED_7_5 : 3;	/* +0x0 */
		uint32_t PI_BIST_MODE : 3;	/* +0x0 */
		uint32_t RESERVED_15_11 : 5;	/* +0x0 */
		uint32_t PI_BIST_ADDR_MODE : 2;	/* +0x0 */
		uint32_t RESERVED_23_18 : 6;	/* +0x0 */
		uint32_t PI_BIST_PAT_MODE : 2;	/* +0x0 */
		uint32_t RESERVED_31_26 : 6;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_351;

typedef union {
	struct {
		uint32_t PI_BIST_PAT_NUM : 4;	/* +0x0 */
		uint32_t RESERVED_31_4 : 28;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_360;

typedef struct {
	uint32_t Signature;	/* +0x0 */
	uint32_t Reserved0;	/* +0x4 */
	uint16_t BlockSize;	/* +0x8 */
	uint8_t BlockChecksum;	/* +0xa */
	uint8_t CompBit;	/* +0xb */
	uint16_t BoardMask;	/* +0xc */
	uint16_t Reserved2;	/* +0xe */
} MEM_CONFIG_BLOCK_HEADER;

typedef struct {
	uint8_t DQ[8];	/* +0x0 */
	uint8_t DM;	/* +0x8 */
} DQM_MAP_ENTRY;

typedef struct {
	MEM_CONFIG_BLOCK_HEADER Header;	/* +0x0 */
	struct {
		uint8_t Byte0 : 2;	/* +0x0 */
		uint8_t Byte1 : 2;	/* +0x0 */
		uint8_t Byte2 : 2;	/* +0x0 */
		uint8_t Byte3 : 2;	/* +0x0 */
		DQM_MAP_ENTRY DataSlice[4];	/* +0x1 */
	} Channel[4];	/* +0x10 */
} MEM_CONFIG_DQ_SWAP;

typedef union {
	struct {
		uint32_t PHY_SW_WRDQS_SHIFT_0 : 4;	/* +0x0 */
		uint32_t RESERVED_7_4 : 4;	/* +0x0 */
		uint32_t PHY_PER_RANK_CS_MAP_0 : 2;	/* +0x0 */
		uint32_t RESERVED_15_10 : 6;	/* +0x0 */
		uint32_t PHY_PER_CS_TRAINING_MULTICAST_EN_0 : 1;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t PHY_PER_CS_TRAINING_INDEX_0 : 2;	/* +0x0 */
		uint32_t RESERVED_31_26 : 6;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PHY_6;

typedef union {
	struct {
		uint32_t PHY_VREF_SETTING_TIME_0 : 16;	/* +0x0 */
		uint32_t PHY_PAD_VREF_CTRL_DQ_0 : 14;	/* +0x0 */
		uint32_t RESERVED_31_30 : 2;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PHY_87;

typedef union {
	struct {
		uint32_t PHY_PAD_VREF_H_CTRL_DQ_0 : 14;	/* +0x0 */
		uint32_t RESERVED_15_14 : 2;	/* +0x0 */
		uint32_t PHY_PER_CS_TRAINING_EN_0 : 1;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t PHY_DQ_IE_TIMING_0 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PHY_88;

typedef enum {
	MBIST_EYE_RD_DQ = 0,
	MBIST_EYE_WR_DQ = 1,
} MBIST_EYE_TYPE;

typedef union {
	struct {
		uint32_t MRR_LSB_REG : 8;	/* +0x0 */
		uint32_t MRR_MSB_REG : 8;	/* +0x0 */
		uint32_t DQS_OSC_ENABLE : 1;	/* +0x0 */
		uint32_t RESERVED_31_17 : 15;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_12;

typedef union {
	struct {
		uint32_t TMRR : 4;	/* +0x0 */
		uint32_t RESERVED_7_4 : 4;	/* +0x0 */
		uint32_t AP : 1;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t CONCURRENTAP : 1;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t TRAS_LOCKOUT : 1;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_54;

typedef union {
	struct {
		uint32_t SELECT_BANK_IN_Q : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t PBR_NUMERIC_ORDER : 1;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t PBR_CONT_REQ_EN : 1;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t AREF_PBR_CONT_EN_THRESHOLD : 5;	/* +0x0 */
		uint32_t RESERVED_31_29 : 3;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_76;

typedef union {
	struct {
		uint32_t WCK_GROUP : 2;	/* +0x0 */
		uint32_t RESERVED_7_2 : 6;	/* +0x0 */
		uint32_t MC_RESERVED14 : 1;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t WCK_MODE : 1;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t WCK_ON_ADJUST : 4;	/* +0x0 */
		uint32_t RESERVED_31_28 : 4;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_113;

typedef union {
	struct {
		uint32_t MC_RESERVED22 : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t RD_LINK_ECC_EN_F1 : 1;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t WR_LINK_ECC_EN_F1 : 1;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t MC_RESERVED23 : 2;	/* +0x0 */
		uint32_t RESERVED_31_26 : 6;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_154;

typedef union {
	struct {
		uint32_t BANK_ARCH_MODE_F2 : 2;	/* +0x0 */
		uint32_t RESERVED_7_2 : 6;	/* +0x0 */
		uint32_t MC_RESERVED24 : 1;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t RD_LINK_ECC_EN_F2 : 1;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t WR_LINK_ECC_EN_F2 : 1;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_155;

typedef union {
	struct {
		uint32_t ECC_DISABLE_W_UC_ERR : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t ECC_WRITEBACK_EN : 1;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t INLINE_ECC_SAME_PAGE : 1;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t INLINE_ECC_BANK_OFFSET : 4;	/* +0x0 */
		uint32_t RESERVED_31_28 : 4;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_263;

typedef union {
	struct {
		uint32_t INLINE_ECC_BG_OFFSET : 4;	/* +0x0 */
		uint32_t RESERVED_7_4 : 4;	/* +0x0 */
		uint32_t ECC_READ_CACHING_EN : 1;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t ECC_WRITE_COMBINING_EN : 1;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t MC_RESERVED33 : 4;	/* +0x0 */
		uint32_t RESERVED_31_28 : 4;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_264;

typedef union {
	struct {
		uint32_t ECC_SCRUB_LEN : 13;	/* +0x0 */
		uint32_t RESERVED_15_13 : 3;	/* +0x0 */
		uint32_t ECC_SCRUB_MODE : 1;	/* +0x0 */
		uint32_t RESERVED_31_17 : 15;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_293;

typedef union {
	struct {
		uint32_t COMMAND_AGE_COUNT : 8;	/* +0x0 */
		uint32_t MC_RESERVED37 : 1;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t ADDR_COLLISION_MPM_DIS : 1;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t BANK_SPLIT_EN : 1;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_332;

typedef union {
	struct {
		uint32_t PLACEMENT_EN : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t PRIORITY_EN : 1;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t RW_SAME_EN : 1;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t RW_SAME_PAGE_EN : 1;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_333;

typedef union {
	struct {
		uint32_t DISABLE_RD_INTERLEAVE : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t SWAP_EN : 1;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t INHIBIT_DRAM_CMD : 2;	/* +0x0 */
		uint32_t RESERVED_23_18 : 6;	/* +0x0 */
		uint32_t CS_MAP : 2;	/* +0x0 */
		uint32_t RESERVED_31_26 : 6;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_335;

typedef union {
	struct {
		uint32_t CTRLUPD_REQ : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t RD_PREAMBLE_LEN_F0 : 8;	/* +0x0 */
		uint32_t WR_PREAMBLE_LEN_F0 : 8;	/* +0x0 */
		uint32_t RD_PREAMBLE_LEN_F1 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_344;

typedef union {
	struct {
		uint32_t WR_PREAMBLE_LEN_F1 : 8;	/* +0x0 */
		uint32_t RD_PREAMBLE_LEN_F2 : 8;	/* +0x0 */
		uint32_t WR_PREAMBLE_LEN_F2 : 8;	/* +0x0 */
		uint32_t RD_PREAMBLE_TRAINING_EN : 1;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_345;

typedef union {
	struct {
		uint32_t WR_DBI_EN : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t RD_DBI_EN : 1;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t DFI_ERROR : 5;	/* +0x0 */
		uint32_t RESERVED_31_21 : 11;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_346;

typedef union {
	struct {
		uint32_t TDFI_DRAM_CLK_ENABLE : 4;	/* +0x0 */
		uint32_t RESERVED_7_4 : 4;	/* +0x0 */
		uint32_t TDFI_WRDATA_DELAY : 8;	/* +0x0 */
		uint32_t DISABLE_MEMORY_MASKED_WRITE : 1;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t MC_RESERVED53 : 1;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_545;

typedef union {
	struct {
		uint32_t PI_RDWR_B2B_2ND_BANK : 4;	/* +0x0 */
		uint32_t RESERVED_7_4 : 4;	/* +0x0 */
		uint32_t PI_WDQLVL_OSC_EN : 1;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t PI_DQS_OSC_PERIOD_EN : 1;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t PI_PARALLEL_WDQLVL_EN : 1;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_221;

typedef union {
	struct {
		uint32_t TDQSCK_MAX_F2 : 4;	/* +0x0 */
		uint32_t RESERVED_7_4 : 4;	/* +0x0 */
		uint32_t TDQSCK_MIN_F2 : 3;	/* +0x0 */
		uint32_t RESERVED_15_11 : 5;	/* +0x0 */
		uint32_t AXI0_ALL_STROBES_USED_ENABLE : 1;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t AXI0_FIXED_PORT_PRIORITY_ENABLE : 1;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_415;

typedef union {
	struct {
		uint32_t AXI0_R_PRIORITY : 3;	/* +0x0 */
		uint32_t RESERVED_7_3 : 5;	/* +0x0 */
		uint32_t AXI0_W_PRIORITY : 3;	/* +0x0 */
		uint32_t RESERVED_31_11 : 21;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_416;

typedef union {
	struct {
		uint32_t AXI1_ALL_STROBES_USED_ENABLE : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t AXI1_FIXED_PORT_PRIORITY_ENABLE : 1;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t AXI1_R_PRIORITY : 3;	/* +0x0 */
		uint32_t RESERVED_23_19 : 5;	/* +0x0 */
		uint32_t AXI1_W_PRIORITY : 3;	/* +0x0 */
		uint32_t RESERVED_31_27 : 5;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_624;

typedef union {
	struct {
		uint32_t AXI0_BDW : 7;	/* +0x0 */
		uint32_t RESERVED_7_7 : 1;	/* +0x0 */
		uint32_t AXI0_BDW_OVFLOW : 1;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t AXI0_CURRENT_BDW : 7;	/* +0x0 */
		uint32_t RESERVED_23_23 : 1;	/* +0x0 */
		uint32_t AXI1_BDW : 7;	/* +0x0 */
		uint32_t RESERVED_31_31 : 1;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_723;

typedef union {
	struct {
		uint32_t AXI1_BDW_OVFLOW : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t AXI1_CURRENT_BDW : 7;	/* +0x0 */
		uint32_t RESERVED_31_15 : 17;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_724;

typedef union {
	struct {
		uint32_t TPBR2ACT_F2_1 : 8;	/* +0x0 */
		uint32_t TPBR2PBR_F2_1 : 12;	/* +0x0 */
		uint32_t RESERVED_23_20 : 4;	/* +0x0 */
		uint32_t PBR_MODE_EN : 4;	/* +0x0 */
		uint32_t RESERVED_31_28 : 4;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_75;

typedef union {
	struct {
		uint32_t ECC_SCRUB_END_ADDR_1 : 8;	/* +0x0 */
		uint32_t LONG_COUNT_MASK : 5;	/* +0x0 */
		uint32_t RESERVED_15_13 : 3;	/* +0x0 */
		uint32_t AREF_NORM_THRESHOLD : 5;	/* +0x0 */
		uint32_t RESERVED_23_21 : 3;	/* +0x0 */
		uint32_t AREF_HIGH_THRESHOLD : 5;	/* +0x0 */
		uint32_t RESERVED_31_29 : 3;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_298;

typedef union {
	struct {
		uint32_t TRFM_F0 : 12;	/* +0x0 */
		uint32_t RESERVED_15_12 : 4;	/* +0x0 */
		uint32_t TRFM_F1 : 12;	/* +0x0 */
		uint32_t RESERVED_31_28 : 4;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_603;

typedef union {
	struct {
		uint32_t TRFM_F2 : 12;	/* +0x0 */
		uint32_t RESERVED_15_12 : 4;	/* +0x0 */
		uint32_t TRFM_SB_F0_0 : 12;	/* +0x0 */
		uint32_t RESERVED_31_28 : 4;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_604;

typedef union {
	struct {
		uint32_t TRFM2ACT_F0_0 : 8;	/* +0x0 */
		uint32_t TRFM_SB_F1_0 : 12;	/* +0x0 */
		uint32_t RESERVED_23_20 : 4;	/* +0x0 */
		uint32_t TRFM2ACT_F1_0 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_605;

typedef union {
	struct {
		uint32_t TRFM_SB_F2_0 : 12;	/* +0x0 */
		uint32_t RESERVED_15_12 : 4;	/* +0x0 */
		uint32_t TRFM2ACT_F2_0 : 8;	/* +0x0 */
		uint32_t RESERVED_31_24 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_606;

typedef union {
	struct {
		uint32_t TRFM_SB_F0_1 : 12;	/* +0x0 */
		uint32_t RESERVED_15_12 : 4;	/* +0x0 */
		uint32_t TRFM2ACT_F0_1 : 8;	/* +0x0 */
		uint32_t RESERVED_31_24 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_607;

typedef union {
	struct {
		uint32_t TRFM_SB_F1_1 : 12;	/* +0x0 */
		uint32_t RESERVED_15_12 : 4;	/* +0x0 */
		uint32_t TRFM2ACT_F1_1 : 8;	/* +0x0 */
		uint32_t RESERVED_31_24 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_608;

typedef union {
	struct {
		uint32_t TRFM_SB_F2_1 : 12;	/* +0x0 */
		uint32_t RESERVED_15_12 : 4;	/* +0x0 */
		uint32_t TRFM2ACT_F2_1 : 8;	/* +0x0 */
		uint32_t REF_RFM_PRIO : 1;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_609;

typedef union {
	struct {
		uint32_t RAA_CNT_DECR_PER_REF : 11;	/* +0x0 */
		uint32_t RESERVED_15_11 : 5;	/* +0x0 */
		uint32_t RAAIMT : 11;	/* +0x0 */
		uint32_t RESERVED_31_27 : 5;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_610;

typedef union {
	struct {
		uint32_t RAAMMT : 11;	/* +0x0 */
		uint32_t RESERVED_15_11 : 5;	/* +0x0 */
		uint32_t RFM_SB_CONT_EN_THRESHOLD : 11;	/* +0x0 */
		uint32_t RESERVED_31_27 : 5;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_611;

typedef union {
	struct {
		uint32_t RFM_NORM_THRESHOLD : 11;	/* +0x0 */
		uint32_t RESERVED_15_11 : 5;	/* +0x0 */
		uint32_t RFM_HIGH_THRESHOLD : 11;	/* +0x0 */
		uint32_t RESERVED_31_27 : 5;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_612;

typedef union {
	struct {
		uint32_t RFM_EN : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t TRFMTH_F0 : 14;	/* +0x0 */
		uint32_t RESERVED_31_22 : 10;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_613;

typedef union {
	struct {
		uint32_t TRFMTH_F1 : 14;	/* +0x0 */
		uint32_t RESERVED_15_14 : 2;	/* +0x0 */
		uint32_t TRFMTH_F2 : 14;	/* +0x0 */
		uint32_t RESERVED_31_30 : 2;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_614;

typedef struct {
	uint8_t RfmEn;	/* +0x0 */
	uint8_t RAAIMT;	/* +0x1 */
	uint8_t RAAMULT;	/* +0x2 */
} MR_RFM_CFG;

typedef struct {
	uint8_t BoardIdGettingMethod;	/* +0x0 */
	uint8_t BoardId;	/* +0x1 */
	uint8_t BoardIdChanged;	/* +0x2 */
	uint8_t BootEnv;	/* +0x3 */
	uint32_t LogFilter;	/* +0x4 */
	uint8_t vol_major_ver;	/* +0x8 */
	uint8_t dfs_dvfs;	/* +0x9 */
	uint8_t vol_sub_ver;	/* +0xa */
	uint8_t Reserved_0;	/* +0xb */
	uint32_t FeatHash;	/* +0xc */
	uint8_t gpu_opp_patch;	/* +0x10 */
	uint8_t BoardRevId;	/* +0x11 */
	uint8_t BoardRevIdChanged;	/* +0x12 */
	uint8_t Reserved_1[13];	/* +0x13 */
} MEM_QUICK_CONFIG;

typedef struct {
	uint32_t Signature;	/* +0x0 */
	uint32_t Reserved0;	/* +0x4 */
	uint16_t MajorVer;	/* +0x8 */
	uint16_t MinorVer;	/* +0xa */
	uint16_t BlockTbOffset;	/* +0xc */
	uint8_t BlockCount;	/* +0xe */
	uint8_t Reserved1;	/* +0xf */
	uint8_t HeaderSize;	/* +0x10 */
	uint8_t HeaderCheckSum;	/* +0x11 */
	uint16_t TotalSize;	/* +0x12 */
	uint32_t Reserved2[10];	/* +0x14 */
	uint32_t HashValue;	/* +0x3c */
} MEM_CONFIG_HEADER;

typedef struct {
	uint16_t Guid;	/* +0x0 */
	uint16_t Offset;	/* +0x2 */
} MEM_CONFIG_BLOCK_TABLE_ENTRY;

typedef struct {
	MEM_CONFIG_BLOCK_HEADER Header;	/* +0x0 */
	uint8_t GetMethod;	/* +0x10 */
	uint8_t buf[];	/* +0x11 */
} MEM_CONFIG_BDID_GET_METHOD;

typedef struct {
	uint8_t GpioIndex[8];	/* +0x0 */
} BDID_GET_METHOD_GPIO_S;

typedef struct {
	uint16_t PcbId;	/* +0x0 */
	uint16_t Mask;	/* +0x2 */
	uint8_t BoardId;	/* +0x4 */
	uint8_t rsvd;	/* +0x5 */
} BOARD_ID_MAP_ENTRY;

typedef struct {
	MEM_CONFIG_BLOCK_HEADER Header;	/* +0x0 */
	BOARD_ID_MAP_ENTRY BoardIdMap[];	/* +0x10 */
} MEM_CONFIG_BLOCK_BOARDID_MAP;

typedef struct {
	MEM_CONFIG_BLOCK_HEADER Header;	/* +0x0 */
	uint16_t MemFreq;	/* +0x10 */
	uint8_t WckAlwaysOn;	/* +0x12 */
	uint8_t DataMask;	/* +0x13 */
	uint8_t RfmEn;	/* +0x14 */
	uint8_t AutoPrechargeEn;	/* +0x15 */
	uint8_t PbrEn;	/* +0x16 */
	uint8_t PortPriority;	/* +0x17 */
	uint8_t BdwP0;	/* +0x18 */
	uint8_t BdwP1;	/* +0x19 */
	uint8_t MbistEn;	/* +0x1a */
	uint8_t MbistMode;	/* +0x1b */
	uint8_t EyeScanEn;	/* +0x1c */
	uint8_t BpEn;	/* +0x1d */
	uint8_t ChannelMask;	/* +0x1e */
	uint8_t WrLEcc;	/* +0x1f */
	uint8_t RdLEcc;	/* +0x20 */
	uint8_t RPriorityP0;	/* +0x21 */
	uint8_t WPriorityP0;	/* +0x22 */
	uint8_t BdwOvflowP0;	/* +0x23 */
	uint8_t RPriorityP1;	/* +0x24 */
	uint8_t WPriorityP1;	/* +0x25 */
	uint8_t BdwOvflowP1;	/* +0x26 */
	uint8_t WrPostamble;	/* +0x27 */
	uint8_t RdPostamble;	/* +0x28 */
	uint8_t WrDbi;	/* +0x29 */
	uint8_t RdDbi;	/* +0x2a */
	uint8_t IEcc;	/* +0x2b */
	uint8_t DdrIrqPrt;	/* +0x2c */
} MEM_CONFIG_BIOS_SETUP;

struct _MEM_FUSE_INFO_STURCT {
	uint32_t ChannelCfg : 2;	/* +0x0 */
	uint32_t LpddrType : 2;	/* +0x0 */
	uint32_t RankNumber : 2;	/* +0x0 */
	uint32_t BankArchInfo : 3;	/* +0x0 */
	uint32_t MaxFrequency : 16;	/* +0x0 */
	uint32_t Reserved_0 : 7;	/* +0x0 */
	uint32_t Reserved_1;	/* +0x4 */
};

typedef struct _MEM_FUSE_INFO_STURCT MEM_FUSE_INFO_STURCT;

typedef enum {
	IRQ_DDRC_TIMEOUT = 0,
	IRQ_DDRC_ECC = 1,
	IRQ_DDRC_IECC_SBE = 2,
	IRQ_DDRC_IECC_DBE = 3,
	IRQ_DDRC_ECC_SBE_RD = 4,
	IRQ_DDRC_ECC_DBE_RD = 5,
	IRQ_DDRC_ECC_SBE_WR = 6,
	IRQ_DDRC_ECC_DBE_WR = 7,
	IRQ_DDRC_ECC_DBE_RMW = 8,
	IRQ_DDRC_LOWPOWER = 9,
	IRQ_DDRC_TRAINING = 10,
	IRQ_DDRC_USERIF = 11,
	IRQ_DDRC_MISC = 12,
	IRQ_DDRC_BIST_DONE = 13,
	IRQ_DDRC_BIST_RSV = 14,
	IRQ_DDRC_DFI = 15,
	IRQ_DDRC_FREQ = 16,
	IRQ_DDRC_MRR_ERR = 17,
	IRQ_DDRC_MRR_DONE = 18,
	IRQ_DDRC_MRW_DONE = 19,
	IRQ_DDRC_MODE_RSV = 20,
	IRQ_DDRC_PARITY = 21,
	IRQ_DDRC_INIT_RST_VALID = 22,
	IRQ_DDRC_INIT_COMP = 23,
	IRQ_DDRC_INIT_SW_MODE = 24,
	IRQ_DDRP_TRAIN_FAIL = 25,
	IRQ_COUNT = 26,
} DDR_IRQ_TYPE;

typedef struct {
	DDR_IRQ_TYPE irq_type;	/* +0x0 */
	char *irq_str;	/* +0x4 */
	uint32_t status_reg_offset;	/* +0x8 */
	uint32_t ack_reg_offset;	/* +0xc */
	uint32_t irq_mask;	/* +0x10 */
	uint8_t log_en;	/* +0x14 */
} DDR_IRQ_TLB_ENTRY;

typedef struct {
	uint32_t Signature;	/* +0x0 */
	uint16_t MajorVer;	/* +0x4 */
	uint16_t MinorVer;	/* +0x6 */
	uint8_t DdrType;	/* +0x8 */
	uint8_t ChannelMask;	/* +0x9 */
	uint8_t RanksPerChannel;	/* +0xa */
	uint8_t EccType;	/* +0xb */
	uint32_t TotalSize;	/* +0xc */
	uint32_t AvailableSize;	/* +0x10 */
	uint16_t MaxFreq;	/* +0x14 */
	uint8_t Reserved[2];	/* +0x16 */
} MEM_INIT_OUTPUT_BUFFER;

typedef struct {
	MEM_CONFIG_BLOCK_HEADER Header;	/* +0x0 */
	uint16_t MaxFreq;	/* +0x10 */
	uint8_t ChMask;	/* +0x12 */
	uint8_t DdrType;	/* +0x13 */
	uint8_t DeviceDensity;	/* +0x14 */
	uint8_t DeviceWidth;	/* +0x15 */
	uint8_t RankNum;	/* +0x16 */
	uint8_t rsvd;	/* +0x17 */
} MEM_CONFIG_BLOCK_CONFIG;

typedef struct {
	MEM_CONFIG_BLOCK_HEADER Header;	/* +0x0 */
	MEM_CONFIG_FEATURE Feat;	/* +0x10 */
} MEM_CONFIG_BLOCK_FEATURE;

typedef struct {
	uint16_t MaxMemFreq;	/* +0x0 */
	uint8_t RankPerCh;	/* +0x2 */
	uint8_t DevWidth;	/* +0x3 */
	uint8_t DqDrv;	/* +0x4 */
	uint8_t DqOdt;	/* +0x5 */
	uint8_t DqsDrv;	/* +0x6 */
	uint8_t DqsOdt;	/* +0x7 */
	uint8_t CkDrv;	/* +0x8 */
	uint8_t CaDrv;	/* +0x9 */
	uint8_t CsDrv;	/* +0xa */
	uint8_t CkeDrv;	/* +0xb */
	uint8_t FdbkDrv;	/* +0xc */
	uint8_t FdbkOdt;	/* +0xd */
	uint8_t RstDrv;	/* +0xe */
	uint8_t FFE;	/* +0xf */
	uint8_t DFE;	/* +0x10 */
	uint8_t CTLE;	/* +0x11 */
	uint8_t Rsvd[2];	/* +0x12 */
} MEM_CONFIG_PHYPADCFG_ENTRY;

typedef struct {
	MEM_CONFIG_BLOCK_HEADER Header;	/* +0x0 */
	MEM_CONFIG_PHYPADCFG_ENTRY PhyPadCfg[];	/* +0x10 */
} MEM_CONFIG_PHYPADCFG;

typedef union {
	struct {
		uint32_t WRITE_MODEREG : 27;	/* +0x0 */
		uint32_t RESERVED_31_27 : 5;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_205;

typedef union {
	struct {
		uint32_t MRW_STATUS : 8;	/* +0x0 */
		uint32_t READ_MODEREG : 17;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_206;

typedef union {
	struct {
		uint32_t PERIPHERAL_MRR_DATA_0 : 32;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_207;

typedef union {
	struct {
		uint32_t MR1_DATA_F2_1 : 8;	/* +0x0 */
		uint32_t MR2_DATA_F2_1 : 8;	/* +0x0 */
		uint32_t MRSINGLE_DATA_0 : 8;	/* +0x0 */
		uint32_t MRSINGLE_DATA_1 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_217;

typedef union {
	struct {
		uint32_t FBDIV : 12;	/* +0x0 */
		uint32_t rsvd_0 : 20;	/* +0x0 */
	} fields;
	uint32_t value;
} DEV_CGC_REG_CLOCK_TILE7_DDRC_PLL_PARAMETER1;

typedef union {
	struct {
		uint32_t FRAC : 24;	/* +0x0 */
		uint32_t PLLEN : 1;	/* +0x0 */
		uint32_t rsvd_0 : 3;	/* +0x0 */
		uint32_t DSMEN : 1;	/* +0x0 */
		uint32_t rsvd_1 : 3;	/* +0x0 */
	} fields;
	uint32_t value;
} DEV_CGC_REG_CLOCK_TILE7_DDRC_PLL_PARAMETER2;

typedef union {
	struct {
		uint32_t POSTDIV1 : 3;	/* +0x0 */
		uint32_t rsvd_0 : 1;	/* +0x0 */
		uint32_t POSTDIV2 : 3;	/* +0x0 */
		uint32_t rsvd_1 : 1;	/* +0x0 */
		uint32_t REFDIV : 6;	/* +0x0 */
		uint32_t rsvd_2 : 18;	/* +0x0 */
	} fields;
	uint32_t value;
} DEV_CGC_REG_CLOCK_TILE7_DDRC_PLL_PARAMETER3;

typedef union {
	struct {
		uint32_t BYPASS : 1;	/* +0x0 */
		uint32_t DACEN : 1;	/* +0x0 */
		uint32_t FOUT4PHASEEN : 1;	/* +0x0 */
		uint32_t FOUTPOSTDIVEN : 1;	/* +0x0 */
		uint32_t pll_lock_ddrc_pll : 1;	/* +0x0 */
		uint32_t time_out_en : 1;	/* +0x0 */
		uint32_t en_retiming : 1;	/* +0x0 */
		uint32_t rsvd_0 : 25;	/* +0x0 */
	} fields;
	uint32_t value;
} DEV_CGC_REG_CLOCK_TILE7_DDRC_PLL_CTRL;

typedef union {
	struct {
		uint32_t DIVVAL : 6;	/* +0x0 */
		uint32_t rsvd_0 : 2;	/* +0x0 */
		uint32_t SPREAD : 5;	/* +0x0 */
		uint32_t rsvd_1 : 3;	/* +0x0 */
		uint32_t DOWNSPREAD : 1;	/* +0x0 */
		uint32_t rsvd_2 : 3;	/* +0x0 */
		uint32_t RESET : 1;	/* +0x0 */
		uint32_t rsvd_3 : 3;	/* +0x0 */
		uint32_t DISABLE_SSCG : 1;	/* +0x0 */
		uint32_t rsvd_4 : 3;	/* +0x0 */
		uint32_t RESETPTR : 1;	/* +0x0 */
		uint32_t rsvd_5 : 3;	/* +0x0 */
	} fields;
	uint32_t value;
} DEV_CGC_REG_CLOCK_TILE7_DDRC_PLL_SSMOD;

typedef union {
	struct {
		uint32_t pll_bypass_ddr : 1;	/* +0x0 */
		uint32_t rsvd_0 : 31;	/* +0x0 */
	} fields;
	uint32_t value;
} DEV_CGC_REG_CLOCK_TILE7_DDRC_PLL_BYPASS;

typedef union {
	struct {
		uint32_t coef_div_sel_4_8_ddr : 3;	/* +0x0 */
		uint32_t rsvd_0 : 5;	/* +0x0 */
		uint32_t coef_div_cfg_axiclk : 4;	/* +0x0 */
		uint32_t rsvd_1 : 4;	/* +0x0 */
		uint32_t coef_div_cfg_dficlk : 4;	/* +0x0 */
		uint32_t rsvd_2 : 12;	/* +0x0 */
	} fields;
	uint32_t value;
} DEV_CGC_REG_CLOCK_TILE7_DDRC_PLL_DIV;

typedef union {
	struct {
		uint32_t en_gater_ddrc0_aclk : 1;	/* +0x0 */
		uint32_t rsvd_0 : 3;	/* +0x0 */
		uint32_t en_gater_ddrc1_aclk : 1;	/* +0x0 */
		uint32_t rsvd_1 : 3;	/* +0x0 */
		uint32_t en_gater_ddrc2_aclk : 1;	/* +0x0 */
		uint32_t rsvd_2 : 3;	/* +0x0 */
		uint32_t en_gater_ddrc3_aclk : 1;	/* +0x0 */
		uint32_t rsvd_3 : 3;	/* +0x0 */
		uint32_t en_gater_ddrc0_dficlk : 1;	/* +0x0 */
		uint32_t rsvd_4 : 3;	/* +0x0 */
		uint32_t en_gater_ddrc1_dficlk : 1;	/* +0x0 */
		uint32_t rsvd_5 : 3;	/* +0x0 */
		uint32_t en_gater_ddrc2_dficlk : 1;	/* +0x0 */
		uint32_t rsvd_6 : 3;	/* +0x0 */
		uint32_t en_gater_ddrc3_dficlk : 1;	/* +0x0 */
		uint32_t rsvd_7 : 3;	/* +0x0 */
	} fields;
	uint32_t value;
} DEV_CGC_REG_CLOCK_TILE7_DDRC_PLL_GATER1;

typedef union {
	struct {
		uint32_t en_gater_ddrphy0_sync_clk : 1;	/* +0x0 */
		uint32_t rsvd_0 : 3;	/* +0x0 */
		uint32_t en_gater_ddrphy1_sync_clk : 1;	/* +0x0 */
		uint32_t rsvd_1 : 3;	/* +0x0 */
		uint32_t en_gater_ddrphy2_sync_clk : 1;	/* +0x0 */
		uint32_t rsvd_2 : 3;	/* +0x0 */
		uint32_t en_gater_ddrphy3_sync_clk : 1;	/* +0x0 */
		uint32_t rsvd_3 : 3;	/* +0x0 */
		uint32_t en_gater_ddrphy0_bypass_clk : 1;	/* +0x0 */
		uint32_t rsvd_4 : 3;	/* +0x0 */
		uint32_t en_gater_ddrphy1_bypass_clk : 1;	/* +0x0 */
		uint32_t rsvd_5 : 3;	/* +0x0 */
		uint32_t en_gater_ddrphy2_bypass_clk : 1;	/* +0x0 */
		uint32_t rsvd_6 : 3;	/* +0x0 */
		uint32_t en_gater_ddrphy3_bypass_clk : 1;	/* +0x0 */
		uint32_t rsvd_7 : 3;	/* +0x0 */
	} fields;
	uint32_t value;
} DEV_CGC_REG_CLOCK_TILE7_DDRC_PLL_GATER2;

typedef union {
	struct {
		uint8_t Lsb;	/* +0x0 */
		uint8_t Msb;	/* +0x1 */
	} byte;
	uint16_t Value;
} SPD_VALUE16;

typedef union {
	struct {
		uint8_t SpdBytesUsed : 4;	/* +0x0 */
		uint8_t SpdBytesTotal : 3;	/* +0x0 */
		uint8_t Reserved7 : 1;	/* +0x0 */
	} Field;
	uint8_t Value;
} SPD_NUM_BYTES_STRUCT;

typedef union {
	struct {
		uint8_t SpdRevisionAdditions : 4;	/* +0x0 */
		uint8_t SpdRevisionEncodings : 4;	/* +0x0 */
	} Field;
	uint8_t Value;
} SPD_REVISION_STRUCT;

typedef union {
	struct {
		uint8_t ModuleType : 8;	/* +0x0 */
	} Field;
	uint8_t Value;
} SPD_KEY_BYTE_STRUCT;

typedef union {
	struct {
		uint8_t BaseModuleType : 4;	/* +0x0 */
		uint8_t HybridMedia : 3;	/* +0x0 */
		uint8_t Hybrid : 1;	/* +0x0 */
	} Field;
	uint8_t Value;
} SPD_KEY_BYTE2_STRUCT;

typedef union {
	struct {
		uint8_t Capacity : 4;	/* +0x0 */
		uint8_t BankAddressBits : 2;	/* +0x0 */
		uint8_t BankGroupBits : 2;	/* +0x0 */
	} Field;
	uint8_t Value;
} SPD_DENSITY_BANKS_STRUCT;

typedef union {
	struct {
		uint8_t ColumnAddressBits : 3;	/* +0x0 */
		uint8_t RowAddressBits : 3;	/* +0x0 */
		uint8_t Reserved76 : 2;	/* +0x0 */
	} Field;
	uint8_t Value;
} SPD_ADDRESSING_STRUCT;

typedef union {
	struct {
		uint8_t LoadingIndex : 2;	/* +0x0 */
		uint8_t ChannelsPerPkg : 2;	/* +0x0 */
		uint8_t DiePerPkg : 3;	/* +0x0 */
		uint8_t PackageType : 1;	/* +0x0 */
	} Field;
	uint8_t Value;
} SPD_PACKAGE_TYPE_STRUCT;

typedef union {
	struct {
		uint8_t Mac : 4;	/* +0x0 */
		uint8_t Tmaw : 3;	/* +0x0 */
		uint8_t Reserved7 : 1;	/* +0x0 */
	} Field;
	uint8_t Value;
} SPD_OPTIONAL_FEATURES_STRUCT;

typedef union {
	struct {
		uint8_t DevDataWidthPerChannel : 3;	/* +0x0 */
		uint8_t PackageRanksPerChannel : 3;	/* +0x0 */
		uint8_t ByteMode : 1;	/* +0x0 */
		uint8_t Reserved7 : 1;	/* +0x0 */
	} Field;
	uint8_t Value;
} SPD_MODULE_ORG_STRUCT;

typedef union {
	struct {
		uint8_t SystemChannelBusWidth : 3;	/* +0x0 */
		uint8_t BusWidthExtension : 2;	/* +0x0 */
		uint8_t NumberOfSystemChannels : 3;	/* +0x0 */
	} Field;
	uint8_t Value;
} SPD_BUS_WIDTH_STRUCT;

typedef union {
	struct {
		uint8_t Reserved60 : 7;	/* +0x0 */
		uint8_t ThermalSensor : 1;	/* +0x0 */
	} Field;
	uint8_t Value;
} SPD_MODULE_TS_STRUCT;

typedef union {
	struct {
		uint8_t CsLoading : 3;	/* +0x0 */
		uint8_t CACCLoading : 3;	/* +0x0 */
		uint8_t DqDqSMskLoading : 2;	/* +0x0 */
	} Field;
	uint8_t Value;
} SPD_SIGNAL_LOADING_STRUCT;

typedef union {
	struct {
		uint8_t Ftb : 2;	/* +0x0 */
		uint8_t Mtb : 2;	/* +0x0 */
		uint8_t Reserved74 : 4;	/* +0x0 */
	} Field;
	uint8_t Value;
} SPD_TIMEBASES_STRUCT;

typedef union {
	struct {
		uint32_t CasLat_3 : 1;	/* +0x0 */
		uint32_t CasLat_6 : 1;	/* +0x0 */
		uint32_t CasLat_8 : 1;	/* +0x0 */
		uint32_t CasLat_9 : 1;	/* +0x0 */
		uint32_t CasLat_10 : 1;	/* +0x0 */
		uint32_t CasLat_11 : 1;	/* +0x0 */
		uint32_t CasLat_12 : 1;	/* +0x0 */
		uint32_t CasLat_14 : 1;	/* +0x0 */
		uint32_t CasLat_16 : 1;	/* +0x0 */
		uint32_t CasLat_20 : 1;	/* +0x0 */
		uint32_t CasLat_22 : 1;	/* +0x0 */
		uint32_t CasLat_24 : 1;	/* +0x0 */
		uint32_t CasLat_26 : 1;	/* +0x0 */
		uint32_t CasLat_28 : 1;	/* +0x0 */
		uint32_t CasLat_30 : 1;	/* +0x0 */
		uint32_t CasLat_32 : 1;	/* +0x0 */
		uint32_t CasLat_Rsvd16 : 1;	/* +0x0 */
		uint32_t CasLat_36 : 1;	/* +0x0 */
		uint32_t CasLat_Rsvd18 : 1;	/* +0x0 */
		uint32_t CasLat_40 : 1;	/* +0x0 */
		uint32_t CasLat_Rsvd20 : 1;	/* +0x0 */
		uint32_t CasLat_44 : 1;	/* +0x0 */
		uint32_t CasLat_Rsvd22 : 1;	/* +0x0 */
		uint32_t CasLat_Rsvd23 : 1;	/* +0x0 */
		uint32_t CasLat_Rsvd24 : 1;	/* +0x0 */
		uint32_t CasLat_Rsvd25 : 1;	/* +0x0 */
		uint32_t CasLat_Rsvd26 : 1;	/* +0x0 */
		uint32_t CasLat_Rsvd27 : 1;	/* +0x0 */
		uint32_t CasLat_Rsvd28 : 1;	/* +0x0 */
		uint32_t CasLat_Rsvd29 : 1;	/* +0x0 */
		uint32_t CasLat_Rsvd30 : 1;	/* +0x0 */
		uint32_t CasLat_Rsvd31 : 1;	/* +0x0 */
	} Field;
	uint32_t Value;
} SPD_SUPPORTED_CAS_LAT_STRUCT;

typedef union {
	struct {
		uint8_t RlSet : 2;	/* +0x0 */
		uint8_t WlSet : 2;	/* +0x0 */
		uint8_t Reserved74 : 4;	/* +0x0 */
	} Field;
	uint8_t Value;
} SPD_RDWR_LATENCY_SET_STRUCT;

typedef union {
	struct {
		uint8_t MfgYear;	/* +0x0 */
		uint8_t MfgWeek;	/* +0x1 */
	} Fields;
	uint16_t Value;
} SPD_MFG_DATE_STRUCT;

typedef struct {
	SPD_NUM_BYTES_STRUCT NumBytes;	/* +0x0 */
	SPD_REVISION_STRUCT Revision;	/* +0x1 */
	SPD_KEY_BYTE_STRUCT KeyByte1;	/* +0x2 */
	SPD_KEY_BYTE2_STRUCT KeyByte2;	/* +0x3 */
	SPD_DENSITY_BANKS_STRUCT Density;	/* +0x4 */
	SPD_ADDRESSING_STRUCT Addressing;	/* +0x5 */
	SPD_PACKAGE_TYPE_STRUCT PackageType;	/* +0x6 */
	SPD_OPTIONAL_FEATURES_STRUCT OptionalFeatures;	/* +0x7 */
	uint8_t ThermalRefreshOpts;	/* +0x8 */
	uint8_t OtherOptions;	/* +0x9 */
	uint8_t Reserved10;	/* +0xa */
	uint8_t NominalVdd;	/* +0xb */
	SPD_MODULE_ORG_STRUCT ModuleOrg;	/* +0xc */
	SPD_BUS_WIDTH_STRUCT BusWidth;	/* +0xd */
	SPD_MODULE_TS_STRUCT ThermalSensor;	/* +0xe */
	uint8_t ExtModuleType;	/* +0xf */
	SPD_SIGNAL_LOADING_STRUCT SignalLoading;	/* +0x10 */
	SPD_TIMEBASES_STRUCT Timebases;	/* +0x11 */
	uint8_t TckAvgMin;	/* +0x12 */
	uint8_t TckAvgMax;	/* +0x13 */
	SPD_SUPPORTED_CAS_LAT_STRUCT SupportedTcl;	/* +0x14 */
	uint8_t TaaMin;	/* +0x18 */
	SPD_RDWR_LATENCY_SET_STRUCT LatencySet;	/* +0x19 */
	uint8_t TrcdMin;	/* +0x1a */
	uint8_t TrpAbMin;	/* +0x1b */
	uint8_t TrpPbMin;	/* +0x1c */
	uint8_t TrfcAbMinLsb;	/* +0x1d */
	uint8_t TrfcAbMinMsb;	/* +0x1e */
	uint8_t TrfcPbMinLsb;	/* +0x1f */
	uint8_t TrfcPbMinMsb;	/* +0x20 */
	uint8_t Reserved_59_33[27];	/* +0x21 */
	uint8_t ConnectorMapping[18];	/* +0x3c */
	uint8_t Reserved_119_78[42];	/* +0x4e */
	uint8_t TrpPbMinFtb;	/* +0x78 */
	uint8_t TrpAbMinFtb;	/* +0x79 */
	uint8_t TrcdMinFtb;	/* +0x7a */
	uint8_t TaaMinFtb;	/* +0x7b */
	uint8_t TckAvgMaxFtb;	/* +0x7c */
	uint8_t TckAvgMinFtb;	/* +0x7d */
	uint8_t BaseCrcLsb;	/* +0x7e */
	uint8_t BaseCrcMsb;	/* +0x7f */
} SPD_BASE_CONFIG_STRUCT;

typedef struct {
	uint8_t Reserved128_191[64];	/* +0x0 */
} SPD_MODULE_SPECIFIC_STRUCT;

typedef struct {
	uint8_t Reserved255_192[64];	/* +0x0 */
} SPD_HYBRID_MEMORY_STRUCT;

typedef struct {
	uint8_t Reserved63_0[64];	/* +0x0 */
} SPD_RESERVED_STRUCT;

typedef struct {
	SPD_VALUE16 ModuleMfgId;	/* +0x0 */
	uint8_t ModuleMfgLocation;	/* +0x2 */
	SPD_MFG_DATE_STRUCT ModuleMfgDate;	/* +0x3 */
	uint8_t SerialNumber[4];	/* +0x5 */
	uint8_t PartNumber[20];	/* +0x9 */
	uint8_t RevisionCode;	/* +0x1d */
	SPD_VALUE16 DramMfgId;	/* +0x1e */
	uint8_t DramStepping;	/* +0x20 */
	uint8_t ModuleMfgSpecific[29];	/* +0x21 */
	uint8_t Reserved383_382[2];	/* +0x3e */
} SPD_MODULE_DATA;

typedef struct {
	SPD_BASE_CONFIG_STRUCT Base;	/* +0x0 */
	SPD_MODULE_SPECIFIC_STRUCT ModuleSpecific;	/* +0x80 */
	SPD_HYBRID_MEMORY_STRUCT HybridMemory;	/* +0xc0 */
	uint8_t Reserved_256_319[64];	/* +0x100 */
	SPD_MODULE_DATA ModuleData;	/* +0x140 */
	SPD_RESERVED_STRUCT Reserved_511_384[64];	/* +0x180 */
} LPDDR_SPD_STRUCT;

typedef struct {
	uint8_t Density;	/* +0x0 */
	uint8_t RowBits;	/* +0x1 */
	uint16_t TrfcAb;	/* +0x2 */
	uint16_t TrfcPb;	/* +0x4 */
	uint16_t Tpbr2pbr;	/* +0x6 */
} DRAM_REF_PARAM;

typedef uint8_t (*MEM_CALC_MR_VAL_CALLBACK)(uint8_t, uint8_t, uint8_t, uint8_t);

typedef void (*MEM_MODE_REGISTER_OVERRIDE_CALLBACK)(void);

typedef MR_RFM_CFG (*MEM_MR_GET_RFM_CFG)(uint8_t);

typedef struct {
	MEM_CALC_MR_VAL_CALLBACK CalcMR0Val;	/* +0x0 */
	MEM_CALC_MR_VAL_CALLBACK CalcMR1Val;	/* +0x4 */
	MEM_CALC_MR_VAL_CALLBACK CalcMR2Val;	/* +0x8 */
	MEM_CALC_MR_VAL_CALLBACK CalcMR3Val;	/* +0xc */
	MEM_CALC_MR_VAL_CALLBACK CalcMR4Val;	/* +0x10 */
	MEM_CALC_MR_VAL_CALLBACK CalcMR8Val;	/* +0x14 */
	MEM_CALC_MR_VAL_CALLBACK CalcMR10Val;	/* +0x18 */
	MEM_CALC_MR_VAL_CALLBACK CalcMR11Val;	/* +0x1c */
	MEM_CALC_MR_VAL_CALLBACK CalcMR12Val;	/* +0x20 */
	MEM_CALC_MR_VAL_CALLBACK CalcMR13Val;	/* +0x24 */
	MEM_CALC_MR_VAL_CALLBACK CalcMR14Val;	/* +0x28 */
	MEM_CALC_MR_VAL_CALLBACK CalcMR15Val;	/* +0x2c */
	MEM_CALC_MR_VAL_CALLBACK CalcMR16Val;	/* +0x30 */
	MEM_CALC_MR_VAL_CALLBACK CalcMR17Val;	/* +0x34 */
	MEM_CALC_MR_VAL_CALLBACK CalcMR18Val;	/* +0x38 */
	MEM_CALC_MR_VAL_CALLBACK CalcMR19Val;	/* +0x3c */
	MEM_CALC_MR_VAL_CALLBACK CalcMR20Val;	/* +0x40 */
	MEM_CALC_MR_VAL_CALLBACK CalcMR22Val;	/* +0x44 */
	MEM_CALC_MR_VAL_CALLBACK CalcMR23Val;	/* +0x48 */
	MEM_CALC_MR_VAL_CALLBACK CalcMR25Val;	/* +0x4c */
	MEM_CALC_MR_VAL_CALLBACK CalcMR28Val;	/* +0x50 */
	MEM_CALC_MR_VAL_CALLBACK CalcMR30Val;	/* +0x54 */
	MEM_CALC_MR_VAL_CALLBACK CalcMR31Val;	/* +0x58 */
	MEM_CALC_MR_VAL_CALLBACK CalcMR32Val;	/* +0x5c */
	MEM_CALC_MR_VAL_CALLBACK CalcMR33Val;	/* +0x60 */
	MEM_CALC_MR_VAL_CALLBACK CalcMR34Val;	/* +0x64 */
	MEM_CALC_MR_VAL_CALLBACK CalcMR35Val;	/* +0x68 */
	MEM_CALC_MR_VAL_CALLBACK CalcMR37Val;	/* +0x6c */
	MEM_CALC_MR_VAL_CALLBACK CalcMR40Val;	/* +0x70 */
	MEM_CALC_MR_VAL_CALLBACK CalcMR41Val;	/* +0x74 */
	MEM_MR_GET_RFM_CFG GetRfmMrCfg;	/* +0x78 */
	MEM_MODE_REGISTER_OVERRIDE_CALLBACK ModeRegisterOverride;	/* +0x7c */
} MEM_MODE_REGISTER_CALLBACK;

struct _PI_TIMING_S {
	uint8_t PI_TCCD;	/* +0x0 */
	uint8_t PI_TCCD_S;	/* +0x1 */
	uint8_t PI_TCCD_L;	/* +0x2 */
	uint8_t PI_TCCDMW;	/* +0x3 */
	uint8_t PI_TDFI_WCK_STOP;	/* +0x4 */
	uint8_t PI_RDLAT_ADJ;	/* +0x5 */
	uint8_t PI_WRLAT_ADJ;	/* +0x6 */
	uint8_t PI_TMRW;	/* +0x7 */
	uint8_t PI_TMRWCKEL;	/* +0x8 */
	uint8_t PI_TDELAY_RDWR_2_BUS_IDLE;	/* +0x9 */
	uint8_t PI_TCKEHDQS;	/* +0xa */
	uint16_t PI_525_RSV;	/* +0xc */
	uint8_t PI_WCK_ACTIVE_RD;	/* +0xe */
	uint8_t PI_WCK_ACTIVE_WR;	/* +0xf */
	uint8_t PI_MBIST_RDLAT_ADJ;	/* +0x10 */
	uint16_t PI_RP_GAP;	/* +0x12 */
	uint16_t PI_RR_GAP_L;	/* +0x14 */
	uint16_t PI_RR_GAP_S;	/* +0x16 */
	uint16_t PI_RW_GAP;	/* +0x18 */
	uint16_t PI_WP_GAP;	/* +0x1a */
	uint16_t PI_WR_GAP_L;	/* +0x1c */
	uint16_t PI_WR_GAP_S;	/* +0x1e */
	uint16_t PI_WW_GAP_L;	/* +0x20 */
	uint16_t PI_WW_GAP_S;	/* +0x22 */
};

typedef struct _PI_TIMING_S PI_TIMING_S;

struct _PHY_TIMING_S {
	uint8_t PHY_WRLVL_PER_START;	/* +0x0 */
	uint8_t PHY_REGULATOR_EN_CNT;	/* +0x1 */
	uint8_t PHY_PAD_DSLICE_IO_CFG;	/* +0x2 */
	uint8_t PHY_RX_CAL_ALL_DLY;	/* +0x3 */
	uint8_t PHY_RX_PCLK_CLK_SEL;	/* +0x4 */
	uint8_t PHY_RDDATA_EN_OE_DLY;	/* +0x5 */
	uint8_t PHY_RDDATA_EN_TSEL_DLY;	/* +0x6 */
	uint8_t PHY_RPTR_UPDATE;	/* +0x7 */
	uint8_t PHY_RDDATA_EN_DLY;	/* +0x8 */
	uint16_t PHY_VREF_SETTING_TIME;	/* +0xa */
	uint16_t PHY_RDDQS_DQx_RISE_FALL_SLAVE_DELAY;	/* +0xc */
	uint8_t PHY_WRLVL_RESP_WAIT_CNT;	/* +0xe */
	uint16_t PHY_RDLVL_MAX_EDGE;	/* +0x10 */
	uint16_t PHY_CLK_WRDQx_SLAVE_DELAY;	/* +0x12 */
	uint8_t PHY_RDDQS_LATENCY_ADJUST[4];	/* +0x14 */
	uint16_t PHY_RDDQS_GATE_SLAVE_DELAY[4];	/* +0x18 */
	uint8_t PHY_GTLVL_LAT_ADJ_START[4];	/* +0x20 */
	uint8_t PHY_WRITE_PATH_LAT_ADD[4];	/* +0x24 */
	uint8_t PHY_WRITE_PATH_LAT_DEC[4];	/* +0x28 */
	uint8_t PHY_WRITE_PATH_LAT_FRAC[4];	/* +0x2c */
	uint8_t PHY_ADR_MEAS_DLY_STEP_ENABLE;	/* +0x30 */
	uint8_t PHY_ADR_CALVL_DLY_STEP;	/* +0x31 */
	uint8_t PHY_TCKSRE_WAIT;	/* +0x32 */
	uint32_t PHY_PAD_ACS_IO_CFG;	/* +0x34 */
	uint8_t PHY_PAD_ACS_RX_PCLK_CLK_SEL;	/* +0x38 */
	uint16_t PHY_PLL_CTRL;	/* +0x3a */
	uint16_t PHY_PLL_CTRL_8X;	/* +0x3c */
	uint8_t PHY_PST_AMBLE;	/* +0x3e */
};

typedef struct _PHY_TIMING_S PHY_TIMING_S;

struct _TURNAROUND_TIMING_S {
	uint8_t RW2MRW_DLY;	/* +0x0 */
	uint8_t R2W_DIFFCS_DLY;	/* +0x1 */
	uint8_t R2R_DIFFCS_DLY;	/* +0x2 */
	uint8_t W2W_DIFFCS_DLY;	/* +0x3 */
	uint8_t W2R_DIFFCS_DLY;	/* +0x4 */
	uint8_t R2W_SAMECS_DLY;	/* +0x5 */
};

typedef struct _TURNAROUND_TIMING_S TURNAROUND_TIMING_S;

typedef void (*MEM_PLATFORM_LOOKUP_BUS_CFG)(void);

typedef void (*MEM_DDRC_REG_INIT)(void);

typedef void (*MEM_DDRPI_REG_INIT)(void);

typedef void (*MEM_DDRP_REG_INIT)(void);

typedef void (*MEM_TIMING_INIT)(void);

typedef void (*MEM_GET_SEQ_WAIT)(uint8_t, uint8_t, uint32_t *);

typedef void (*MEM_GET_TURNAROUND_TIMING)(uint8_t, uint8_t, TURNAROUND_TIMING_S *);

typedef void (*MEM_GET_PI_TIMING)(uint8_t, uint8_t, PI_TIMING_S *);

typedef void (*MEM_GET_PHY_TIMING)(uint8_t, uint8_t, PHY_TIMING_S *);

typedef struct {
	MEM_PLATFORM_LOOKUP_BUS_CFG MemPlatformLookupBusConfig;	/* +0x0 */
	MEM_TIMING_INIT MemTimingInit;	/* +0x4 */
	MEM_DDRC_REG_INIT MemDdrcRegInit;	/* +0x8 */
	MEM_DDRPI_REG_INIT MemDdrpiRegInit;	/* +0xc */
	MEM_DDRP_REG_INIT MemDdrpRegInit;	/* +0x10 */
	MEM_GET_SEQ_WAIT MemGetSeqWait;	/* +0x14 */
	MEM_MODE_REGISTER_CALLBACK *MemModeRegisterCallback;	/* +0x18 */
	MEM_GET_PI_TIMING MemGetPiTiming;	/* +0x1c */
	MEM_GET_PHY_TIMING MemGetPhyTiming;	/* +0x20 */
	MEM_GET_TURNAROUND_TIMING MemGetTurnaroundTiming;	/* +0x24 */
} MEM_TECH_FUNC_S;

typedef union {
	struct {
		uint32_t WCKO_OSC_PERIOD : 15;	/* +0x0 */
		uint32_t RESERVED_15_15 : 1;	/* +0x0 */
		uint32_t TOSCO_F0 : 8;	/* +0x0 */
		uint32_t TOSCO_F1 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_19;

typedef union {
	struct {
		uint32_t TOSCO_F2 : 8;	/* +0x0 */
		uint32_t DQS_OSC_BASE_VALUE_0_CS0 : 16;	/* +0x0 */
		uint32_t RESERVED_31_24 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_20;

typedef union {
	struct {
		uint32_t DQS_OSC_IN_PROGRESS_STATUS : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t CASLAT_LIN_F0 : 7;	/* +0x0 */
		uint32_t RESERVED_15_15 : 1;	/* +0x0 */
		uint32_t WRLAT_F0 : 7;	/* +0x0 */
		uint32_t RESERVED_23_23 : 1;	/* +0x0 */
		uint32_t CASLAT_LIN_F1 : 7;	/* +0x0 */
		uint32_t RESERVED_31_31 : 1;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_29;

typedef union {
	struct {
		uint32_t WRLAT_F1 : 7;	/* +0x0 */
		uint32_t RESERVED_7_7 : 1;	/* +0x0 */
		uint32_t CASLAT_LIN_F2 : 7;	/* +0x0 */
		uint32_t RESERVED_15_15 : 1;	/* +0x0 */
		uint32_t WRLAT_F2 : 7;	/* +0x0 */
		uint32_t RESERVED_23_23 : 1;	/* +0x0 */
		uint32_t TBST_INT_INTERVAL : 3;	/* +0x0 */
		uint32_t RESERVED_31_27 : 5;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_30;

typedef union {
	struct {
		uint32_t TCCD : 5;	/* +0x0 */
		uint32_t RESERVED_7_5 : 3;	/* +0x0 */
		uint32_t TCCD_L_F0 : 5;	/* +0x0 */
		uint32_t RESERVED_15_13 : 3;	/* +0x0 */
		uint32_t TRRD_F0 : 8;	/* +0x0 */
		uint32_t TRRD_L_F0 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_31;

typedef union {
	struct {
		uint32_t TRP_F0 : 8;	/* +0x0 */
		uint32_t TFAW_F0 : 9;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t TCCD_L_F1 : 5;	/* +0x0 */
		uint32_t RESERVED_31_29 : 3;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_34;

typedef union {
	struct {
		uint32_t TRRD_F1 : 8;	/* +0x0 */
		uint32_t TRRD_L_F1 : 8;	/* +0x0 */
		uint32_t TRC_F1 : 9;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_35;

typedef union {
	struct {
		uint32_t TRAS_MIN_F1 : 9;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t TWTR_F1 : 6;	/* +0x0 */
		uint32_t RESERVED_23_22 : 2;	/* +0x0 */
		uint32_t TWTR_L_F1 : 6;	/* +0x0 */
		uint32_t RESERVED_31_30 : 2;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_36;

typedef union {
	struct {
		uint32_t TWTR_AP_F1 : 8;	/* +0x0 */
		uint32_t TWTR_L_AP_F1 : 8;	/* +0x0 */
		uint32_t TRP_F1 : 8;	/* +0x0 */
		uint32_t RESERVED_31_24 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_37;

typedef union {
	struct {
		uint32_t TFAW_F1 : 9;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t TCCD_L_F2 : 5;	/* +0x0 */
		uint32_t RESERVED_23_21 : 3;	/* +0x0 */
		uint32_t TRRD_F2 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_38;

typedef union {
	struct {
		uint32_t TRRD_L_F2 : 8;	/* +0x0 */
		uint32_t TRC_F2 : 9;	/* +0x0 */
		uint32_t RESERVED_31_17 : 15;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_39;

typedef union {
	struct {
		uint32_t TRAS_MIN_F2 : 9;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t TWTR_F2 : 6;	/* +0x0 */
		uint32_t RESERVED_23_22 : 2;	/* +0x0 */
		uint32_t TWTR_L_F2 : 6;	/* +0x0 */
		uint32_t RESERVED_31_30 : 2;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_40;

typedef union {
	struct {
		uint32_t TWTR_AP_F2 : 8;	/* +0x0 */
		uint32_t TWTR_L_AP_F2 : 8;	/* +0x0 */
		uint32_t TRP_F2 : 8;	/* +0x0 */
		uint32_t RESERVED_31_24 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_41;

typedef union {
	struct {
		uint32_t TFAW_F2 : 9;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t TRTP_F0 : 8;	/* +0x0 */
		uint32_t TMRD_F0 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_42;

typedef union {
	struct {
		uint32_t TRTP_F1 : 8;	/* +0x0 */
		uint32_t TMRD_F1 : 8;	/* +0x0 */
		uint32_t TMOD_F1 : 8;	/* +0x0 */
		uint32_t RESERVED_31_24 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_45;

typedef union {
	struct {
		uint32_t TRAS_MAX_F1 : 16;	/* +0x0 */
		uint32_t TCKE_F1 : 8;	/* +0x0 */
		uint32_t TCKESR_F1 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_46;

typedef union {
	struct {
		uint32_t TCCDMW_F1 : 6;	/* +0x0 */
		uint32_t RESERVED_7_6 : 2;	/* +0x0 */
		uint32_t TCSH_F1 : 8;	/* +0x0 */
		uint32_t TCACSH_F1 : 8;	/* +0x0 */
		uint32_t TRTP_F2 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_47;

typedef union {
	struct {
		uint32_t TMRD_F2 : 8;	/* +0x0 */
		uint32_t TMOD_F2 : 8;	/* +0x0 */
		uint32_t TRAS_MAX_F2 : 16;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_48;

typedef union {
	struct {
		uint32_t TCKE_F2 : 8;	/* +0x0 */
		uint32_t TCKESR_F2 : 8;	/* +0x0 */
		uint32_t TCCDMW_F2 : 6;	/* +0x0 */
		uint32_t RESERVED_23_22 : 2;	/* +0x0 */
		uint32_t TCSH_F2 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_49;

typedef union {
	struct {
		uint32_t TCACSH_F2 : 8;	/* +0x0 */
		uint32_t TPPD_F0 : 3;	/* +0x0 */
		uint32_t RESERVED_15_11 : 5;	/* +0x0 */
		uint32_t TPPD_F1 : 3;	/* +0x0 */
		uint32_t RESERVED_23_19 : 5;	/* +0x0 */
		uint32_t TPPD_F2 : 3;	/* +0x0 */
		uint32_t RESERVED_31_27 : 5;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_50;

typedef union {
	struct {
		uint32_t TRCD_WR_F0 : 8;	/* +0x0 */
		uint32_t TWR_F0 : 8;	/* +0x0 */
		uint32_t TRCD_F1 : 8;	/* +0x0 */
		uint32_t TRCD_WR_F1 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_52;

typedef union {
	struct {
		uint32_t TWR_F1 : 8;	/* +0x0 */
		uint32_t TRCD_F2 : 8;	/* +0x0 */
		uint32_t TRCD_WR_F2 : 8;	/* +0x0 */
		uint32_t TWR_F2 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_53;

typedef union {
	struct {
		uint32_t TDAL_F0 : 8;	/* +0x0 */
		uint32_t TDAL_F1 : 8;	/* +0x0 */
		uint32_t TDAL_F2 : 8;	/* +0x0 */
		uint32_t BSTLEN : 6;	/* +0x0 */
		uint32_t RESERVED_31_30 : 2;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_55;

typedef union {
	struct {
		uint32_t TRP_AB_F0_0 : 8;	/* +0x0 */
		uint32_t TRP_AB_F1_0 : 8;	/* +0x0 */
		uint32_t TRP_AB_F2_0 : 8;	/* +0x0 */
		uint32_t TRP_AB_F0_1 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_56;

typedef union {
	struct {
		uint32_t TRP_AB_F1_1 : 8;	/* +0x0 */
		uint32_t TRP_AB_F2_1 : 8;	/* +0x0 */
		uint32_t REG_DIMM_ENABLE : 1;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t MC_RESERVED2 : 2;	/* +0x0 */
		uint32_t RESERVED_31_26 : 6;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_57;

typedef union {
	struct {
		uint32_t TRFC_OPT_THRESHOLD : 3;	/* +0x0 */
		uint32_t RESERVED_7_3 : 5;	/* +0x0 */
		uint32_t CS_COMPARISON_FOR_REFRESH_DEPTH : 6;	/* +0x0 */
		uint32_t RESERVED_15_14 : 2;	/* +0x0 */
		uint32_t TRFC_F0 : 12;	/* +0x0 */
		uint32_t RESERVED_31_28 : 4;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_60;

typedef union {
	struct {
		uint32_t TRFC_F1 : 12;	/* +0x0 */
		uint32_t RESERVED_31_12 : 20;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_62;

typedef union {
	struct {
		uint32_t TREF_F1 : 20;	/* +0x0 */
		uint32_t RESERVED_31_20 : 12;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_63;

typedef union {
	struct {
		uint32_t TRFC_F2 : 12;	/* +0x0 */
		uint32_t RESERVED_31_12 : 20;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_64;

typedef union {
	struct {
		uint32_t TREF_F2 : 20;	/* +0x0 */
		uint32_t RESERVED_31_20 : 12;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_65;

typedef union {
	struct {
		uint32_t TREF_INTERVAL : 20;	/* +0x0 */
		uint32_t RESERVED_31_20 : 12;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_66;

typedef union {
	struct {
		uint32_t TPBR2PBR_F0_0 : 12;	/* +0x0 */
		uint32_t RESERVED_15_12 : 4;	/* +0x0 */
		uint32_t TRFC_PB_F1_0 : 12;	/* +0x0 */
		uint32_t RESERVED_31_28 : 4;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_68;

typedef union {
	struct {
		uint32_t TPBR2ACT_F1_0 : 8;	/* +0x0 */
		uint32_t TPBR2PBR_F1_0 : 12;	/* +0x0 */
		uint32_t RESERVED_31_20 : 12;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_69;

typedef union {
	struct {
		uint32_t TRFC_PB_F2_0 : 12;	/* +0x0 */
		uint32_t RESERVED_15_12 : 4;	/* +0x0 */
		uint32_t TPBR2ACT_F2_0 : 8;	/* +0x0 */
		uint32_t RESERVED_31_24 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_70;

typedef union {
	struct {
		uint32_t TPBR2PBR_F2_0 : 12;	/* +0x0 */
		uint32_t RESERVED_15_12 : 4;	/* +0x0 */
		uint32_t TRFC_PB_F0_1 : 12;	/* +0x0 */
		uint32_t RESERVED_31_28 : 4;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_71;

typedef union {
	struct {
		uint32_t TRFC_PB_F1_1 : 12;	/* +0x0 */
		uint32_t RESERVED_15_12 : 4;	/* +0x0 */
		uint32_t TPBR2ACT_F1_1 : 8;	/* +0x0 */
		uint32_t RESERVED_31_24 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_73;

typedef union {
	struct {
		uint32_t TPBR2PBR_F1_1 : 12;	/* +0x0 */
		uint32_t RESERVED_15_12 : 4;	/* +0x0 */
		uint32_t TRFC_PB_F2_1 : 12;	/* +0x0 */
		uint32_t RESERVED_31_28 : 4;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_74;

typedef union {
	struct {
		uint32_t TPDEX_F0 : 16;	/* +0x0 */
		uint32_t TPDEX_F1 : 16;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_77;

typedef union {
	struct {
		uint32_t TPDEX_F2 : 16;	/* +0x0 */
		uint32_t TMRRI_F0 : 8;	/* +0x0 */
		uint32_t TMRRI_F1 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_78;

typedef union {
	struct {
		uint32_t TMRRI_F2 : 8;	/* +0x0 */
		uint32_t TCKELCS_F0 : 5;	/* +0x0 */
		uint32_t RESERVED_15_13 : 3;	/* +0x0 */
		uint32_t TCKEHCS_F0 : 5;	/* +0x0 */
		uint32_t RESERVED_23_21 : 3;	/* +0x0 */
		uint32_t TMRWCKEL_F0 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_79;

typedef union {
	struct {
		uint32_t TZQCKE_F0 : 8;	/* +0x0 */
		uint32_t TCKELCS_F1 : 5;	/* +0x0 */
		uint32_t RESERVED_15_13 : 3;	/* +0x0 */
		uint32_t TCKEHCS_F1 : 5;	/* +0x0 */
		uint32_t RESERVED_23_21 : 3;	/* +0x0 */
		uint32_t TMRWCKEL_F1 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_80;

typedef union {
	struct {
		uint32_t TZQCKE_F1 : 8;	/* +0x0 */
		uint32_t TCKELCS_F2 : 5;	/* +0x0 */
		uint32_t RESERVED_15_13 : 3;	/* +0x0 */
		uint32_t TCKEHCS_F2 : 5;	/* +0x0 */
		uint32_t RESERVED_23_21 : 3;	/* +0x0 */
		uint32_t TMRWCKEL_F2 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_81;

typedef union {
	struct {
		uint32_t TZQCKE_F2 : 8;	/* +0x0 */
		uint32_t TCSCKE_F0 : 5;	/* +0x0 */
		uint32_t RESERVED_15_13 : 3;	/* +0x0 */
		uint32_t CA_DEFAULT_VAL_F0 : 1;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t TCSCKE_F1 : 5;	/* +0x0 */
		uint32_t RESERVED_31_29 : 3;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_82;

typedef union {
	struct {
		uint32_t CA_DEFAULT_VAL_F1 : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t TCSCKE_F2 : 5;	/* +0x0 */
		uint32_t RESERVED_15_13 : 3;	/* +0x0 */
		uint32_t CA_DEFAULT_VAL_F2 : 1;	/* +0x0 */
		uint32_t RESERVED_31_17 : 15;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_83;

typedef union {
	struct {
		uint32_t TXSR_F1 : 16;	/* +0x0 */
		uint32_t TXSNR_F1 : 16;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_85;

typedef union {
	struct {
		uint32_t TXSR_F2 : 16;	/* +0x0 */
		uint32_t TXSNR_F2 : 16;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_86;

typedef union {
	struct {
		uint32_t TSR_F1 : 8;	/* +0x0 */
		uint32_t TESCKE_F1 : 8;	/* +0x0 */
		uint32_t TCSCKEH_F1 : 5;	/* +0x0 */
		uint32_t RESERVED_23_21 : 3;	/* +0x0 */
		uint32_t TCKELCMD_F1 : 5;	/* +0x0 */
		uint32_t RESERVED_31_29 : 3;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_89;

typedef union {
	struct {
		uint32_t TCKEHCMD_F1 : 5;	/* +0x0 */
		uint32_t RESERVED_7_5 : 3;	/* +0x0 */
		uint32_t TCKCKEL_F1 : 9;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t TCKELPD_F1 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_90;

typedef union {
	struct {
		uint32_t TSR_F2 : 8;	/* +0x0 */
		uint32_t TESCKE_F2 : 8;	/* +0x0 */
		uint32_t TCSCKEH_F2 : 5;	/* +0x0 */
		uint32_t RESERVED_23_21 : 3;	/* +0x0 */
		uint32_t TCKELCMD_F2 : 5;	/* +0x0 */
		uint32_t RESERVED_31_29 : 3;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_91;

typedef union {
	struct {
		uint32_t TCKEHCMD_F2 : 5;	/* +0x0 */
		uint32_t RESERVED_7_5 : 3;	/* +0x0 */
		uint32_t TCKCKEL_F2 : 9;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t TCKELPD_F2 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_92;

typedef union {
	struct {
		uint32_t TCMDCKE_F0 : 8;	/* +0x0 */
		uint32_t TCMDCKE_F1 : 8;	/* +0x0 */
		uint32_t TCMDCKE_F2 : 8;	/* +0x0 */
		uint32_t PWRUP_SREFRESH_EXIT : 1;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_93;

typedef union {
	struct {
		uint32_t UPD_PHYUPD_DFI_PROMOTE_THRESHOLD_F0 : 16;	/* +0x0 */
		uint32_t UPD_CTRLUPD_NORM_THRESHOLD_F1 : 16;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_101;

typedef union {
	struct {
		uint32_t UPD_CTRLUPD_HIGH_THRESHOLD_F1 : 16;	/* +0x0 */
		uint32_t UPD_CTRLUPD_TIMEOUT_F1 : 16;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_102;

typedef union {
	struct {
		uint32_t UPD_CTRLUPD_SW_PROMOTE_THRESHOLD_F1 : 16;	/* +0x0 */
		uint32_t UPD_PHYUPD_DFI_PROMOTE_THRESHOLD_F1 : 16;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_103;

typedef union {
	struct {
		uint32_t UPD_CTRLUPD_NORM_THRESHOLD_F2 : 16;	/* +0x0 */
		uint32_t UPD_CTRLUPD_HIGH_THRESHOLD_F2 : 16;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_104;

typedef union {
	struct {
		uint32_t UPD_CTRLUPD_TIMEOUT_F2 : 16;	/* +0x0 */
		uint32_t UPD_CTRLUPD_SW_PROMOTE_THRESHOLD_F2 : 16;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_105;

typedef union {
	struct {
		uint32_t UPD_PHYUPD_DFI_PROMOTE_THRESHOLD_F2 : 16;	/* +0x0 */
		uint32_t CORE_VERY_HIGH_PRIORITY_THRESHOLD : 3;	/* +0x0 */
		uint32_t RESERVED_23_19 : 5;	/* +0x0 */
		uint32_t CORE_HIGH_PRIORITY_THRESHOLD : 3;	/* +0x0 */
		uint32_t RESERVED_31_27 : 5;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_106;

typedef union {
	struct {
		uint32_t TDFI_WCK_EN_RD_F1 : 7;	/* +0x0 */
		uint32_t RESERVED_7_7 : 1;	/* +0x0 */
		uint32_t TDFI_WCK_EN_WR_F1 : 7;	/* +0x0 */
		uint32_t RESERVED_15_15 : 1;	/* +0x0 */
		uint32_t TDFI_WCK_EN_FS_F1 : 7;	/* +0x0 */
		uint32_t RESERVED_23_23 : 1;	/* +0x0 */
		uint32_t TDFI_WCK_TOGGLE_F1 : 7;	/* +0x0 */
		uint32_t RESERVED_31_31 : 1;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_109;

typedef union {
	struct {
		uint32_t TDFI_WCK_FAST_TOGGLE_F1 : 4;	/* +0x0 */
		uint32_t RESERVED_7_4 : 4;	/* +0x0 */
		uint32_t TDFI_WCK_DIS_F1 : 8;	/* +0x0 */
		uint32_t TDFI_WCK_STOP_F1 : 8;	/* +0x0 */
		uint32_t TDFI_WCK_EN_RD_F2 : 7;	/* +0x0 */
		uint32_t RESERVED_31_31 : 1;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_110;

typedef union {
	struct {
		uint32_t TDFI_WCK_EN_WR_F2 : 7;	/* +0x0 */
		uint32_t RESERVED_7_7 : 1;	/* +0x0 */
		uint32_t TDFI_WCK_EN_FS_F2 : 7;	/* +0x0 */
		uint32_t RESERVED_15_15 : 1;	/* +0x0 */
		uint32_t TDFI_WCK_TOGGLE_F2 : 7;	/* +0x0 */
		uint32_t RESERVED_23_23 : 1;	/* +0x0 */
		uint32_t TDFI_WCK_FAST_TOGGLE_F2 : 4;	/* +0x0 */
		uint32_t RESERVED_31_28 : 4;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_111;

typedef union {
	struct {
		uint32_t TDFI_WCK_DIS_F2 : 8;	/* +0x0 */
		uint32_t TDFI_WCK_STOP_F2 : 8;	/* +0x0 */
		uint32_t MC_RESERVED12 : 1;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t MC_RESERVED13 : 1;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_112;

typedef union {
	struct {
		uint32_t WCK_OFF_PLUS_ADJUST_F1 : 4;	/* +0x0 */
		uint32_t RESERVED_7_4 : 4;	/* +0x0 */
		uint32_t RD_TO_WCK_SYNC_LOST_F1 : 8;	/* +0x0 */
		uint32_t WR_TO_WCK_SYNC_LOST_F1 : 8;	/* +0x0 */
		uint32_t MC_RESERVED17 : 4;	/* +0x0 */
		uint32_t RESERVED_31_28 : 4;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_116;

typedef union {
	struct {
		uint32_t WCK_OFF_MINUS_ADJUST_F2 : 4;	/* +0x0 */
		uint32_t RESERVED_7_4 : 4;	/* +0x0 */
		uint32_t WCK_OFF_PLUS_ADJUST_F2 : 4;	/* +0x0 */
		uint32_t RESERVED_15_12 : 4;	/* +0x0 */
		uint32_t RD_TO_WCK_SYNC_LOST_F2 : 8;	/* +0x0 */
		uint32_t WR_TO_WCK_SYNC_LOST_F2 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_117;

typedef union {
	struct {
		uint32_t TDFI_PHYMSTR_RESP_F2 : 20;	/* +0x0 */
		uint32_t RESERVED_23_20 : 4;	/* +0x0 */
		uint32_t PHYMSTR_NO_AREF : 1;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_138;

typedef union {
	struct {
		uint32_t CKSRX_F0 : 8;	/* +0x0 */
		uint32_t CKSRE_F1 : 8;	/* +0x0 */
		uint32_t CKSRX_F1 : 8;	/* +0x0 */
		uint32_t CKSRE_F2 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_174;

typedef union {
	struct {
		uint32_t CKSRX_F2 : 8;	/* +0x0 */
		uint32_t LOWPOWER_REFRESH_ENABLE : 2;	/* +0x0 */
		uint32_t RESERVED_15_10 : 6;	/* +0x0 */
		uint32_t LP_CMD : 7;	/* +0x0 */
		uint32_t RESERVED_23_23 : 1;	/* +0x0 */
		uint32_t LPI_IDLE_WAKEUP_F0 : 6;	/* +0x0 */
		uint32_t RESERVED_31_30 : 2;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_175;

typedef union {
	struct {
		uint32_t ZQCS_F1 : 12;	/* +0x0 */
		uint32_t RESERVED_15_12 : 4;	/* +0x0 */
		uint32_t TZQCAL_F1 : 12;	/* +0x0 */
		uint32_t RESERVED_31_28 : 4;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_318;

typedef union {
	struct {
		uint32_t TZQLAT_F1 : 7;	/* +0x0 */
		uint32_t RESERVED_7_7 : 1;	/* +0x0 */
		uint32_t ZQINIT_F2 : 12;	/* +0x0 */
		uint32_t RESERVED_31_20 : 12;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_319;

typedef union {
	struct {
		uint32_t TZQCAL_F2 : 12;	/* +0x0 */
		uint32_t RESERVED_15_12 : 4;	/* +0x0 */
		uint32_t TZQLAT_F2 : 7;	/* +0x0 */
		uint32_t RESERVED_23_23 : 1;	/* +0x0 */
		uint32_t ZQ_SW_REQ_START_LATCH_MAP : 2;	/* +0x0 */
		uint32_t RESERVED_31_26 : 6;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_321;

typedef union {
	struct {
		uint32_t TODTUP_F1 : 16;	/* +0x0 */
		uint32_t TODTUP_F2 : 16;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_402;

typedef union {
	struct {
		uint32_t TODTL_2CMD_F0 : 8;	/* +0x0 */
		uint32_t TODTH_WR_F0 : 4;	/* +0x0 */
		uint32_t RESERVED_15_12 : 4;	/* +0x0 */
		uint32_t TODTH_RD_F0 : 4;	/* +0x0 */
		uint32_t RESERVED_23_20 : 4;	/* +0x0 */
		uint32_t TODTL_2CMD_F1 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_403;

typedef union {
	struct {
		uint32_t TODTH_WR_F1 : 4;	/* +0x0 */
		uint32_t RESERVED_7_4 : 4;	/* +0x0 */
		uint32_t TODTH_RD_F1 : 4;	/* +0x0 */
		uint32_t RESERVED_15_12 : 4;	/* +0x0 */
		uint32_t TODTL_2CMD_F2 : 8;	/* +0x0 */
		uint32_t TODTH_WR_F2 : 4;	/* +0x0 */
		uint32_t RESERVED_31_28 : 4;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_404;

typedef union {
	struct {
		uint32_t TODTH_RD_F2 : 4;	/* +0x0 */
		uint32_t RESERVED_7_4 : 4;	/* +0x0 */
		uint32_t ODT_EN_F0 : 1;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t ODT_EN_F1 : 1;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t ODT_EN_F2 : 1;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_405;

typedef union {
	struct {
		uint32_t RD_TO_ODTH_F0 : 6;	/* +0x0 */
		uint32_t RESERVED_7_6 : 2;	/* +0x0 */
		uint32_t RD_TO_ODTH_F1 : 6;	/* +0x0 */
		uint32_t RESERVED_15_14 : 2;	/* +0x0 */
		uint32_t RD_TO_ODTH_F2 : 6;	/* +0x0 */
		uint32_t RESERVED_23_22 : 2;	/* +0x0 */
		uint32_t RW2MRW_DLY_F0 : 5;	/* +0x0 */
		uint32_t RESERVED_31_29 : 3;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_408;

typedef union {
	struct {
		uint32_t RW2MRW_DLY_F1 : 5;	/* +0x0 */
		uint32_t RESERVED_7_5 : 3;	/* +0x0 */
		uint32_t RW2MRW_DLY_F2 : 5;	/* +0x0 */
		uint32_t RESERVED_15_13 : 3;	/* +0x0 */
		uint32_t R2R_DIFFCS_DLY_F0 : 5;	/* +0x0 */
		uint32_t RESERVED_23_21 : 3;	/* +0x0 */
		uint32_t R2W_DIFFCS_DLY_F0 : 5;	/* +0x0 */
		uint32_t RESERVED_31_29 : 3;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_409;

typedef union {
	struct {
		uint32_t W2R_DIFFCS_DLY_F0 : 5;	/* +0x0 */
		uint32_t RESERVED_7_5 : 3;	/* +0x0 */
		uint32_t W2W_DIFFCS_DLY_F0 : 5;	/* +0x0 */
		uint32_t RESERVED_15_13 : 3;	/* +0x0 */
		uint32_t R2R_DIFFCS_DLY_F1 : 5;	/* +0x0 */
		uint32_t RESERVED_23_21 : 3;	/* +0x0 */
		uint32_t R2W_DIFFCS_DLY_F1 : 5;	/* +0x0 */
		uint32_t RESERVED_31_29 : 3;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_410;

typedef union {
	struct {
		uint32_t W2R_DIFFCS_DLY_F1 : 5;	/* +0x0 */
		uint32_t RESERVED_7_5 : 3;	/* +0x0 */
		uint32_t W2W_DIFFCS_DLY_F1 : 5;	/* +0x0 */
		uint32_t RESERVED_15_13 : 3;	/* +0x0 */
		uint32_t R2R_DIFFCS_DLY_F2 : 5;	/* +0x0 */
		uint32_t RESERVED_23_21 : 3;	/* +0x0 */
		uint32_t R2W_DIFFCS_DLY_F2 : 5;	/* +0x0 */
		uint32_t RESERVED_31_29 : 3;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_411;

typedef union {
	struct {
		uint32_t W2R_DIFFCS_DLY_F2 : 5;	/* +0x0 */
		uint32_t RESERVED_7_5 : 3;	/* +0x0 */
		uint32_t W2W_DIFFCS_DLY_F2 : 5;	/* +0x0 */
		uint32_t RESERVED_15_13 : 3;	/* +0x0 */
		uint32_t R2W_SAMECS_DLY_F0 : 5;	/* +0x0 */
		uint32_t RESERVED_23_21 : 3;	/* +0x0 */
		uint32_t R2W_SAMECS_DLY_F1 : 5;	/* +0x0 */
		uint32_t RESERVED_31_29 : 3;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_412;

typedef union {
	struct {
		uint32_t R2W_SAMECS_DLY_F2 : 5;	/* +0x0 */
		uint32_t RESERVED_7_5 : 3;	/* +0x0 */
		uint32_t R2R_SAMECS_DLY : 5;	/* +0x0 */
		uint32_t RESERVED_15_13 : 3;	/* +0x0 */
		uint32_t W2R_SAMECS_DLY : 5;	/* +0x0 */
		uint32_t RESERVED_23_21 : 3;	/* +0x0 */
		uint32_t W2W_SAMECS_DLY : 5;	/* +0x0 */
		uint32_t RESERVED_31_29 : 3;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_413;

typedef union {
	struct {
		uint32_t TDQSCK_MAX_F0 : 4;	/* +0x0 */
		uint32_t RESERVED_7_4 : 4;	/* +0x0 */
		uint32_t TDQSCK_MIN_F0 : 3;	/* +0x0 */
		uint32_t RESERVED_15_11 : 5;	/* +0x0 */
		uint32_t TDQSCK_MAX_F1 : 4;	/* +0x0 */
		uint32_t RESERVED_23_20 : 4;	/* +0x0 */
		uint32_t TDQSCK_MIN_F1 : 3;	/* +0x0 */
		uint32_t RESERVED_31_27 : 5;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_414;

typedef union {
	struct {
		uint32_t TDFI_CTRL_DELAY_F1 : 4;	/* +0x0 */
		uint32_t RESERVED_7_4 : 4;	/* +0x0 */
		uint32_t TDFI_PHY_WRDATA_F1 : 3;	/* +0x0 */
		uint32_t RESERVED_15_11 : 5;	/* +0x0 */
		uint32_t TDFI_RDCSLAT_F1 : 9;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_529;

typedef union {
	struct {
		uint32_t TDFI_RDDATA_EN_F1 : 9;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t TDFI_WRCSLAT_F1 : 9;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_530;

typedef union {
	struct {
		uint32_t TDFI_PHY_WRLAT_F1 : 9;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t TDFI_CTRLMSG_RESP_F1 : 7;	/* +0x0 */
		uint32_t RESERVED_31_23 : 9;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_531;

typedef union {
	struct {
		uint32_t TDFI_CTRL_DELAY_F2 : 4;	/* +0x0 */
		uint32_t RESERVED_7_4 : 4;	/* +0x0 */
		uint32_t TDFI_PHY_WRDATA_F2 : 3;	/* +0x0 */
		uint32_t RESERVED_15_11 : 5;	/* +0x0 */
		uint32_t TDFI_RDCSLAT_F2 : 9;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_540;

typedef union {
	struct {
		uint32_t TDFI_RDDATA_EN_F2 : 9;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t TDFI_WRCSLAT_F2 : 9;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_541;

typedef union {
	struct {
		uint32_t TDFI_PHY_WRLAT_F2 : 9;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t TDFI_CTRLMSG_RESP_F2 : 7;	/* +0x0 */
		uint32_t RESERVED_31_23 : 9;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_542;

typedef union {
	struct {
		uint32_t MC_RESERVED82 : 4;	/* +0x0 */
		uint32_t RESERVED_7_4 : 4;	/* +0x0 */
		uint32_t MC_RESERVED83 : 4;	/* +0x0 */
		uint32_t RESERVED_15_12 : 4;	/* +0x0 */
		uint32_t TAAD : 8;	/* +0x0 */
		uint32_t ACT1_ENABLE : 1;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_553;

typedef union {
	struct {
		uint32_t MC_RESERVED84 : 3;	/* +0x0 */
		uint32_t RESERVED_7_3 : 5;	/* +0x0 */
		uint32_t NWR_F0 : 8;	/* +0x0 */
		uint32_t NWR_F1 : 8;	/* +0x0 */
		uint32_t NWR_F2 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_554;

typedef union {
	struct {
		uint32_t TREFIE_F1 : 20;	/* +0x0 */
		uint32_t RESERVED_31_20 : 12;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_616;

typedef union {
	struct {
		uint32_t TREFIE_F2 : 20;	/* +0x0 */
		uint32_t RESERVED_31_20 : 12;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_617;

typedef union {
	struct {
		uint32_t PI_TCKCKEH : 4;	/* +0x0 */
		uint32_t RESERVED_7_4 : 4;	/* +0x0 */
		uint32_t PI_TCKCKEL_F0 : 4;	/* +0x0 */
		uint32_t RESERVED_15_12 : 4;	/* +0x0 */
		uint32_t PI_TCKCKEL_F1 : 4;	/* +0x0 */
		uint32_t RESERVED_23_20 : 4;	/* +0x0 */
		uint32_t PI_TCKCKEL_F2 : 4;	/* +0x0 */
		uint32_t RESERVED_31_28 : 4;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_164;

typedef union {
	struct {
		uint32_t PI_TSDO_F1 : 8;	/* +0x0 */
		uint32_t PI_TSDO_F2 : 8;	/* +0x0 */
		uint32_t RESERVED_31_16 : 16;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_406;

typedef union {
	struct {
		uint32_t PI_TDELAY_RDWR_2_BUS_IDLE_F1 : 8;	/* +0x0 */
		uint32_t RESERVED_31_8 : 24;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_408;

typedef union {
	struct {
		uint32_t PI_TDELAY_RDWR_2_BUS_IDLE_F2 : 8;	/* +0x0 */
		uint32_t PI_ZQINIT_F0 : 12;	/* +0x0 */
		uint32_t RESERVED_31_20 : 12;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_409;

typedef union {
	struct {
		uint32_t PI_TWCKENL_FS_ADJ_F0 : 6;	/* +0x0 */
		uint32_t RESERVED_7_6 : 2;	/* +0x0 */
		uint32_t PI_CASLAT_F0 : 7;	/* +0x0 */
		uint32_t RESERVED_15_15 : 1;	/* +0x0 */
		uint32_t PI_WRLAT_F1 : 7;	/* +0x0 */
		uint32_t RESERVED_23_23 : 1;	/* +0x0 */
		uint32_t PI_TWCKENL_WR_ADJ_F1 : 6;	/* +0x0 */
		uint32_t RESERVED_31_30 : 2;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_413;

typedef union {
	struct {
		uint32_t PI_TWCKENL_RD_ADJ_F1 : 6;	/* +0x0 */
		uint32_t RESERVED_7_6 : 2;	/* +0x0 */
		uint32_t PI_TWCKPRE_STATIC_F1 : 6;	/* +0x0 */
		uint32_t RESERVED_15_14 : 2;	/* +0x0 */
		uint32_t PI_TWCKPRE_TOGGLE_WR_F1 : 6;	/* +0x0 */
		uint32_t RESERVED_23_22 : 2;	/* +0x0 */
		uint32_t PI_TWCKPRE_TOGGLE_RD_F1 : 6;	/* +0x0 */
		uint32_t RESERVED_31_30 : 2;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_414;

typedef union {
	struct {
		uint32_t PI_WCK_ACTIVE_WR_F1 : 7;	/* +0x0 */
		uint32_t RESERVED_7_7 : 1;	/* +0x0 */
		uint32_t PI_WCK_ACTIVE_RD_F1 : 7;	/* +0x0 */
		uint32_t RESERVED_15_15 : 1;	/* +0x0 */
		uint32_t PI_TWCKENL_FS_ADJ_F1 : 6;	/* +0x0 */
		uint32_t RESERVED_23_22 : 2;	/* +0x0 */
		uint32_t PI_CASLAT_F1 : 7;	/* +0x0 */
		uint32_t RESERVED_31_31 : 1;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_415;

typedef union {
	struct {
		uint32_t PI_WRLAT_F2 : 7;	/* +0x0 */
		uint32_t RESERVED_7_7 : 1;	/* +0x0 */
		uint32_t PI_TWCKENL_WR_ADJ_F2 : 6;	/* +0x0 */
		uint32_t RESERVED_15_14 : 2;	/* +0x0 */
		uint32_t PI_TWCKENL_RD_ADJ_F2 : 6;	/* +0x0 */
		uint32_t RESERVED_23_22 : 2;	/* +0x0 */
		uint32_t PI_TWCKPRE_STATIC_F2 : 6;	/* +0x0 */
		uint32_t RESERVED_31_30 : 2;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_416;

typedef union {
	struct {
		uint32_t PI_TWCKPRE_TOGGLE_WR_F2 : 6;	/* +0x0 */
		uint32_t RESERVED_7_6 : 2;	/* +0x0 */
		uint32_t PI_TWCKPRE_TOGGLE_RD_F2 : 6;	/* +0x0 */
		uint32_t RESERVED_15_14 : 2;	/* +0x0 */
		uint32_t PI_WCK_ACTIVE_WR_F2 : 7;	/* +0x0 */
		uint32_t RESERVED_23_23 : 1;	/* +0x0 */
		uint32_t PI_WCK_ACTIVE_RD_F2 : 7;	/* +0x0 */
		uint32_t RESERVED_31_31 : 1;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_417;

typedef union {
	struct {
		uint32_t PI_TWCKENL_FS_ADJ_F2 : 6;	/* +0x0 */
		uint32_t RESERVED_7_6 : 2;	/* +0x0 */
		uint32_t PI_CASLAT_F2 : 7;	/* +0x0 */
		uint32_t RESERVED_15_15 : 1;	/* +0x0 */
		uint32_t PI_TRFC_F0 : 10;	/* +0x0 */
		uint32_t RESERVED_31_26 : 6;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_418;

typedef union {
	struct {
		uint32_t PI_TRFC_F1 : 10;	/* +0x0 */
		uint32_t RESERVED_31_10 : 22;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_420;

typedef union {
	struct {
		uint32_t PI_TREF_F1 : 20;	/* +0x0 */
		uint32_t RESERVED_31_20 : 12;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_421;

typedef union {
	struct {
		uint32_t PI_TRFC_F2 : 10;	/* +0x0 */
		uint32_t RESERVED_31_10 : 22;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_422;

typedef union {
	struct {
		uint32_t PI_TREF_F2 : 20;	/* +0x0 */
		uint32_t RESERVED_23_20 : 4;	/* +0x0 */
		uint32_t PI_TREF_OFFSET : 5;	/* +0x0 */
		uint32_t RESERVED_31_29 : 3;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_423;

typedef union {
	struct {
		uint32_t PI_WRLVL_EN_F1 : 2;	/* +0x0 */
		uint32_t RESERVED_7_2 : 6;	/* +0x0 */
		uint32_t PI_TDFI_WRLVL_WW_F1 : 10;	/* +0x0 */
		uint32_t RESERVED_23_18 : 6;	/* +0x0 */
		uint32_t PI_WRLVL_WCKOFF_F1 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_426;

typedef union {
	struct {
		uint32_t PI_WRLVL_EN_F2 : 2;	/* +0x0 */
		uint32_t RESERVED_7_2 : 6;	/* +0x0 */
		uint32_t PI_TDFI_WRLVL_WW_F2 : 10;	/* +0x0 */
		uint32_t RESERVED_23_18 : 6;	/* +0x0 */
		uint32_t PI_WRLVL_WCKOFF_F2 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_427;

typedef union {
	struct {
		uint32_t PI_TODTL_2CMD_F0 : 8;	/* +0x0 */
		uint32_t PI_ODT_EN_F0 : 1;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t PI_TODTL_2CMD_F1 : 8;	/* +0x0 */
		uint32_t PI_ODT_EN_F1 : 1;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_428;

typedef union {
	struct {
		uint32_t PI_TODTL_2CMD_F2 : 8;	/* +0x0 */
		uint32_t PI_ODT_EN_F2 : 1;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t PI_ODTLON_F0 : 4;	/* +0x0 */
		uint32_t RESERVED_23_20 : 4;	/* +0x0 */
		uint32_t PI_TODTON_MIN_F0 : 4;	/* +0x0 */
		uint32_t RESERVED_31_28 : 4;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_429;

typedef union {
	struct {
		uint32_t PI_ODTLON_F1 : 4;	/* +0x0 */
		uint32_t RESERVED_7_4 : 4;	/* +0x0 */
		uint32_t PI_TODTON_MIN_F1 : 4;	/* +0x0 */
		uint32_t RESERVED_15_12 : 4;	/* +0x0 */
		uint32_t PI_ODTLON_F2 : 4;	/* +0x0 */
		uint32_t RESERVED_23_20 : 4;	/* +0x0 */
		uint32_t PI_TODTON_MIN_F2 : 4;	/* +0x0 */
		uint32_t RESERVED_31_28 : 4;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_430;

typedef union {
	struct {
		uint32_t PI_RDLAT_ADJ_F0 : 9;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t PI_RDLAT_ADJ_F1 : 9;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_437;

typedef union {
	struct {
		uint32_t PI_RDLAT_ADJ_F2 : 9;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t PI_WRLAT_ADJ_F0 : 7;	/* +0x0 */
		uint32_t RESERVED_23_23 : 1;	/* +0x0 */
		uint32_t PI_WRLAT_ADJ_F1 : 7;	/* +0x0 */
		uint32_t RESERVED_31_31 : 1;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_438;

typedef union {
	struct {
		uint32_t PI_WRLAT_ADJ_F2 : 7;	/* +0x0 */
		uint32_t RESERVED_7_7 : 1;	/* +0x0 */
		uint32_t PI_TDFI_PHY_WRDATA_F0 : 3;	/* +0x0 */
		uint32_t RESERVED_15_11 : 5;	/* +0x0 */
		uint32_t PI_TDFI_PHY_WRDATA_F1 : 3;	/* +0x0 */
		uint32_t RESERVED_23_19 : 5;	/* +0x0 */
		uint32_t PI_TDFI_PHY_WRDATA_F2 : 3;	/* +0x0 */
		uint32_t RESERVED_31_27 : 5;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_439;

typedef union {
	struct {
		uint32_t PI_TDFI_CALVL_CC_F1 : 10;	/* +0x0 */
		uint32_t RESERVED_15_10 : 6;	/* +0x0 */
		uint32_t PI_TDFI_CALVL_CAPTURE_F1 : 10;	/* +0x0 */
		uint32_t RESERVED_31_26 : 6;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_441;

typedef union {
	struct {
		uint32_t PI_TDFI_CALVL_CC_F2 : 10;	/* +0x0 */
		uint32_t RESERVED_15_10 : 6;	/* +0x0 */
		uint32_t PI_TDFI_CALVL_CAPTURE_F2 : 10;	/* +0x0 */
		uint32_t RESERVED_31_26 : 6;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_442;

typedef union {
	struct {
		uint32_t PI_TCAENT_F0 : 14;	/* +0x0 */
		uint32_t RESERVED_15_14 : 2;	/* +0x0 */
		uint32_t PI_TMRZ_F1 : 5;	/* +0x0 */
		uint32_t RESERVED_31_21 : 11;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_444;

typedef union {
	struct {
		uint32_t PI_TCAENT_F1 : 14;	/* +0x0 */
		uint32_t RESERVED_15_14 : 2;	/* +0x0 */
		uint32_t PI_TMRZ_F2 : 5;	/* +0x0 */
		uint32_t RESERVED_31_21 : 11;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_445;

typedef union {
	struct {
		uint32_t PI_TCAENT_F2 : 14;	/* +0x0 */
		uint32_t RESERVED_15_14 : 2;	/* +0x0 */
		uint32_t PI_TDFI_CACSCA_F0 : 5;	/* +0x0 */
		uint32_t RESERVED_23_21 : 3;	/* +0x0 */
		uint32_t PI_TDFI_CASEL_F0 : 5;	/* +0x0 */
		uint32_t RESERVED_31_29 : 3;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_446;

typedef union {
	struct {
		uint32_t PI_TDFI_CACSCA_F1 : 5;	/* +0x0 */
		uint32_t RESERVED_7_5 : 3;	/* +0x0 */
		uint32_t PI_TDFI_CASEL_F1 : 5;	/* +0x0 */
		uint32_t RESERVED_15_13 : 3;	/* +0x0 */
		uint32_t PI_TVREF_SHORT_F1 : 10;	/* +0x0 */
		uint32_t RESERVED_31_26 : 6;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_449;

typedef union {
	struct {
		uint32_t PI_TVREF_LONG_F1 : 10;	/* +0x0 */
		uint32_t RESERVED_15_10 : 6;	/* +0x0 */
		uint32_t PI_TVRCG_ENABLE_F1 : 10;	/* +0x0 */
		uint32_t RESERVED_31_26 : 6;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_450;

typedef union {
	struct {
		uint32_t PI_TVRCG_DISABLE_F1 : 10;	/* +0x0 */
		uint32_t RESERVED_15_10 : 6;	/* +0x0 */
		uint32_t PI_TDFI_CACSCA_F2 : 5;	/* +0x0 */
		uint32_t RESERVED_23_21 : 3;	/* +0x0 */
		uint32_t PI_TDFI_CASEL_F2 : 5;	/* +0x0 */
		uint32_t RESERVED_31_29 : 3;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_451;

typedef union {
	struct {
		uint32_t PI_TVREF_SHORT_F2 : 10;	/* +0x0 */
		uint32_t RESERVED_15_10 : 6;	/* +0x0 */
		uint32_t PI_TVREF_LONG_F2 : 10;	/* +0x0 */
		uint32_t RESERVED_31_26 : 6;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_452;

typedef union {
	struct {
		uint32_t PI_TVRCG_ENABLE_F2 : 10;	/* +0x0 */
		uint32_t RESERVED_15_10 : 6;	/* +0x0 */
		uint32_t PI_TVRCG_DISABLE_F2 : 10;	/* +0x0 */
		uint32_t RESERVED_31_26 : 6;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_453;

typedef union {
	struct {
		uint32_t PI_TCKELCK_F0 : 5;	/* +0x0 */
		uint32_t RESERVED_7_5 : 3;	/* +0x0 */
		uint32_t PI_TDFI_CALVL_STROBE_F1 : 4;	/* +0x0 */
		uint32_t RESERVED_15_12 : 4;	/* +0x0 */
		uint32_t PI_TXP_F1 : 5;	/* +0x0 */
		uint32_t RESERVED_23_21 : 3;	/* +0x0 */
		uint32_t PI_TMRWCKEL_F1 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_457;

typedef union {
	struct {
		uint32_t PI_TCKELCK_F1 : 5;	/* +0x0 */
		uint32_t RESERVED_7_5 : 3;	/* +0x0 */
		uint32_t PI_TDFI_CALVL_STROBE_F2 : 4;	/* +0x0 */
		uint32_t RESERVED_15_12 : 4;	/* +0x0 */
		uint32_t PI_TXP_F2 : 5;	/* +0x0 */
		uint32_t RESERVED_23_21 : 3;	/* +0x0 */
		uint32_t PI_TMRWCKEL_F2 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_458;

typedef union {
	struct {
		uint32_t PI_TFC_F0 : 10;	/* +0x0 */
		uint32_t RESERVED_15_10 : 6;	/* +0x0 */
		uint32_t PI_TCKEHDQS_F1 : 6;	/* +0x0 */
		uint32_t RESERVED_31_22 : 10;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_465;

typedef union {
	struct {
		uint32_t PI_TFC_F1 : 10;	/* +0x0 */
		uint32_t RESERVED_15_10 : 6;	/* +0x0 */
		uint32_t PI_TCKEHDQS_F2 : 6;	/* +0x0 */
		uint32_t RESERVED_31_22 : 10;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_466;

typedef union {
	struct {
		uint32_t PI_TFC_F2 : 10;	/* +0x0 */
		uint32_t RESERVED_15_10 : 6;	/* +0x0 */
		uint32_t PI_WDQLVL_NTP_VREF_START_POINT : 7;	/* +0x0 */
		uint32_t RESERVED_23_23 : 1;	/* +0x0 */
		uint32_t PI_WDQLVL_NTP_VREF_STOP_POINT : 7;	/* +0x0 */
		uint32_t RESERVED_31_31 : 1;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_467;

typedef union {
	struct {
		uint32_t PI_TDFI_WDQLVL_WR_F1 : 10;	/* +0x0 */
		uint32_t RESERVED_15_10 : 6;	/* +0x0 */
		uint32_t PI_TDFI_WDQLVL_RW_F1 : 10;	/* +0x0 */
		uint32_t RESERVED_31_26 : 6;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_472;

typedef union {
	struct {
		uint32_t PI_NTP_TRAIN_EN_F1 : 4;	/* +0x0 */
		uint32_t RESERVED_7_4 : 4;	/* +0x0 */
		uint32_t PI_MBIST_RDLAT_ADJ_F1 : 9;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t PI_MBIST_TWCKENL_RD_ADJ_F1 : 6;	/* +0x0 */
		uint32_t RESERVED_31_30 : 2;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_474;

typedef union {
	struct {
		uint32_t PI_TDFI_WDQLVL_WR_F2 : 10;	/* +0x0 */
		uint32_t RESERVED_15_10 : 6;	/* +0x0 */
		uint32_t PI_TDFI_WDQLVL_RW_F2 : 10;	/* +0x0 */
		uint32_t RESERVED_31_26 : 6;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_475;

typedef union {
	struct {
		uint32_t PI_NTP_TRAIN_EN_F2 : 4;	/* +0x0 */
		uint32_t RESERVED_7_4 : 4;	/* +0x0 */
		uint32_t PI_MBIST_RDLAT_ADJ_F2 : 9;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t PI_MBIST_TWCKENL_RD_ADJ_F2 : 6;	/* +0x0 */
		uint32_t RESERVED_31_30 : 2;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_477;

typedef union {
	struct {
		uint32_t PI_TMRW_F0 : 8;	/* +0x0 */
		uint32_t PI_TRTP_F1 : 8;	/* +0x0 */
		uint32_t PI_TRP_F1 : 8;	/* +0x0 */
		uint32_t PI_TRCD_F1 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_484;

typedef union {
	struct {
		uint32_t PI_TCCD_L_F1 : 5;	/* +0x0 */
		uint32_t RESERVED_7_5 : 3;	/* +0x0 */
		uint32_t PI_TWTR_S_F1 : 6;	/* +0x0 */
		uint32_t RESERVED_15_14 : 2;	/* +0x0 */
		uint32_t PI_TWTR_L_F1 : 6;	/* +0x0 */
		uint32_t RESERVED_23_22 : 2;	/* +0x0 */
		uint32_t PI_TCCD_S_F1 : 5;	/* +0x0 */
		uint32_t RESERVED_31_29 : 3;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_485;

typedef union {
	struct {
		uint32_t PI_TWTR_F1 : 6;	/* +0x0 */
		uint32_t RESERVED_7_6 : 2;	/* +0x0 */
		uint32_t PI_TWR_F1 : 8;	/* +0x0 */
		uint32_t PI_TRAS_MAX_F1 : 16;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_486;

typedef union {
	struct {
		uint32_t PI_TRAS_MIN_F1 : 9;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t PI_TDQSCK_MAX_F1 : 4;	/* +0x0 */
		uint32_t RESERVED_23_20 : 4;	/* +0x0 */
		uint32_t PI_TCCDMW_F1 : 6;	/* +0x0 */
		uint32_t RESERVED_31_30 : 2;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_487;

typedef union {
	struct {
		uint32_t PI_TSR_F1 : 8;	/* +0x0 */
		uint32_t PI_TMRD_F1 : 8;	/* +0x0 */
		uint32_t PI_TMRW_F1 : 8;	/* +0x0 */
		uint32_t PI_TRTP_F2 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_488;

typedef union {
	struct {
		uint32_t PI_TRP_F2 : 8;	/* +0x0 */
		uint32_t PI_TRCD_F2 : 8;	/* +0x0 */
		uint32_t PI_TCCD_L_F2 : 5;	/* +0x0 */
		uint32_t RESERVED_23_21 : 3;	/* +0x0 */
		uint32_t PI_TWTR_S_F2 : 6;	/* +0x0 */
		uint32_t RESERVED_31_30 : 2;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_489;

typedef union {
	struct {
		uint32_t PI_TWTR_L_F2 : 6;	/* +0x0 */
		uint32_t RESERVED_7_6 : 2;	/* +0x0 */
		uint32_t PI_TCCD_S_F2 : 5;	/* +0x0 */
		uint32_t RESERVED_15_13 : 3;	/* +0x0 */
		uint32_t PI_TWTR_F2 : 6;	/* +0x0 */
		uint32_t RESERVED_23_22 : 2;	/* +0x0 */
		uint32_t PI_TWR_F2 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_490;

typedef union {
	struct {
		uint32_t PI_TRAS_MAX_F2 : 16;	/* +0x0 */
		uint32_t PI_TRAS_MIN_F2 : 9;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_491;

typedef union {
	struct {
		uint32_t PI_TDQSCK_MAX_F2 : 4;	/* +0x0 */
		uint32_t RESERVED_7_4 : 4;	/* +0x0 */
		uint32_t PI_TCCDMW_F2 : 6;	/* +0x0 */
		uint32_t RESERVED_15_14 : 2;	/* +0x0 */
		uint32_t PI_TSR_F2 : 8;	/* +0x0 */
		uint32_t PI_TMRD_F2 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_492;

typedef union {
	struct {
		uint32_t PI_TMRW_F2 : 8;	/* +0x0 */
		uint32_t PI_TDFI_CTRLUPD_MAX_F0 : 21;	/* +0x0 */
		uint32_t RESERVED_31_29 : 3;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_493;

typedef union {
	struct {
		uint32_t PI_TXSR_F0 : 16;	/* +0x0 */
		uint32_t PI_TXSR_F1 : 16;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_499;

typedef union {
	struct {
		uint32_t PI_TXSR_F2 : 16;	/* +0x0 */
		uint32_t PI_TEXCKE_F0 : 6;	/* +0x0 */
		uint32_t RESERVED_23_22 : 2;	/* +0x0 */
		uint32_t PI_TEXCKE_F1 : 6;	/* +0x0 */
		uint32_t RESERVED_31_30 : 2;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_500;

typedef union {
	struct {
		uint32_t PI_TEXCKE_F2 : 6;	/* +0x0 */
		uint32_t RESERVED_7_6 : 2;	/* +0x0 */
		uint32_t PI_TINIT_F0 : 24;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_501;

typedef union {
	struct {
		uint32_t PI_TXSNR_F1 : 16;	/* +0x0 */
		uint32_t RESERVED_31_16 : 16;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_512;

typedef union {
	struct {
		uint32_t PI_TXSNR_F2 : 16;	/* +0x0 */
		uint32_t PI_RESERVED36 : 12;	/* +0x0 */
		uint32_t RESERVED_31_28 : 4;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_518;

typedef union {
	struct {
		uint32_t PI_RESERVED39 : 12;	/* +0x0 */
		uint32_t RESERVED_15_12 : 4;	/* +0x0 */
		uint32_t PI_TZQCAL_F1 : 12;	/* +0x0 */
		uint32_t RESERVED_31_28 : 4;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_521;

typedef union {
	struct {
		uint32_t PI_TZQLAT_F1 : 7;	/* +0x0 */
		uint32_t RESERVED_7_7 : 1;	/* +0x0 */
		uint32_t PI_RESERVED40 : 12;	/* +0x0 */
		uint32_t RESERVED_31_20 : 12;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_522;

typedef union {
	struct {
		uint32_t PI_RESERVED41 : 12;	/* +0x0 */
		uint32_t RESERVED_15_12 : 4;	/* +0x0 */
		uint32_t PI_TZQCAL_F2 : 12;	/* +0x0 */
		uint32_t RESERVED_31_28 : 4;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_523;

typedef union {
	struct {
		uint32_t PI_TZQLAT_F2 : 7;	/* +0x0 */
		uint32_t RESERVED_7_7 : 1;	/* +0x0 */
		uint32_t PI_RESERVED42 : 12;	/* +0x0 */
		uint32_t RESERVED_31_20 : 12;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_524;

typedef union {
	struct {
		uint32_t PI_RESERVED43 : 12;	/* +0x0 */
		uint32_t RESERVED_15_12 : 4;	/* +0x0 */
		uint32_t PI_RESERVED44 : 12;	/* +0x0 */
		uint32_t RESERVED_31_28 : 4;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_525;

typedef union {
	struct {
		uint32_t PI_TCBTRTW_F0 : 6;	/* +0x0 */
		uint32_t RESERVED_7_6 : 2;	/* +0x0 */
		uint32_t PI_TWCK2DQ7H_F1 : 5;	/* +0x0 */
		uint32_t RESERVED_15_13 : 3;	/* +0x0 */
		uint32_t PI_TDQ7HWCK_F1 : 5;	/* +0x0 */
		uint32_t RESERVED_23_21 : 3;	/* +0x0 */
		uint32_t PI_TDQ7LWCK_F1 : 5;	/* +0x0 */
		uint32_t RESERVED_31_29 : 3;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_528;

typedef union {
	struct {
		uint32_t PI_TDQ72DQ_F1 : 10;	/* +0x0 */
		uint32_t RESERVED_15_10 : 6;	/* +0x0 */
		uint32_t PI_TCBTRTW_F1 : 6;	/* +0x0 */
		uint32_t RESERVED_23_22 : 2;	/* +0x0 */
		uint32_t PI_TWCK2DQ7H_F2 : 5;	/* +0x0 */
		uint32_t RESERVED_31_29 : 3;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_529;

typedef union {
	struct {
		uint32_t PI_TDQ7HWCK_F2 : 5;	/* +0x0 */
		uint32_t RESERVED_7_5 : 3;	/* +0x0 */
		uint32_t PI_TDQ7LWCK_F2 : 5;	/* +0x0 */
		uint32_t RESERVED_15_13 : 3;	/* +0x0 */
		uint32_t PI_TDQ72DQ_F2 : 10;	/* +0x0 */
		uint32_t RESERVED_31_26 : 6;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_530;

typedef union {
	struct {
		uint32_t PI_TCBTRTW_F2 : 6;	/* +0x0 */
		uint32_t RESERVED_7_6 : 2;	/* +0x0 */
		uint32_t PI_TDS_TRAIN_F0 : 4;	/* +0x0 */
		uint32_t RESERVED_15_12 : 4;	/* +0x0 */
		uint32_t PI_TDH_TRAIN_F0 : 4;	/* +0x0 */
		uint32_t RESERVED_23_20 : 4;	/* +0x0 */
		uint32_t PI_TDS_TRAIN_F1 : 4;	/* +0x0 */
		uint32_t RESERVED_31_28 : 4;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_531;

typedef union {
	struct {
		uint32_t PI_MC_TRFC_F0 : 10;	/* +0x0 */
		uint32_t RESERVED_15_10 : 6;	/* +0x0 */
		uint32_t PI_MC_TRFC_F1 : 10;	/* +0x0 */
		uint32_t RESERVED_31_26 : 6;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_533;

typedef union {
	struct {
		uint32_t PI_MC_TRFC_F2 : 10;	/* +0x0 */
		uint32_t RESERVED_15_10 : 6;	/* +0x0 */
		uint32_t PI_PHYMSTR_REQ_EXTEND_NUM_F0 : 4;	/* +0x0 */
		uint32_t RESERVED_23_20 : 4;	/* +0x0 */
		uint32_t PI_REQ_EXTEND_MASK_CMD_EN_F0 : 1;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_534;

typedef union {
	struct {
		uint32_t PI_PHYMSTR_REQ_EXTEND_NUM_F1 : 4;	/* +0x0 */
		uint32_t RESERVED_7_4 : 4;	/* +0x0 */
		uint32_t PI_REQ_EXTEND_MASK_CMD_EN_F1 : 1;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t PI_PHYMSTR_REQ_EXTEND_NUM_F2 : 4;	/* +0x0 */
		uint32_t RESERVED_23_20 : 4;	/* +0x0 */
		uint32_t PI_REQ_EXTEND_MASK_CMD_EN_F2 : 1;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_535;

typedef union {
	struct {
		uint32_t PI_MC_DATA_FREQ_RATIO_F0 : 2;	/* +0x0 */
		uint32_t RESERVED_7_2 : 6;	/* +0x0 */
		uint32_t PI_MC_DATA_FREQ_RATIO_F1 : 2;	/* +0x0 */
		uint32_t RESERVED_15_10 : 6;	/* +0x0 */
		uint32_t PI_MC_DATA_FREQ_RATIO_F2 : 2;	/* +0x0 */
		uint32_t RESERVED_23_18 : 6;	/* +0x0 */
		uint32_t PI_PREAMBLE_SUPPORT_F0 : 2;	/* +0x0 */
		uint32_t RESERVED_31_26 : 6;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_536;

typedef union {
	struct {
		uint32_t PI_TDFI_WCK_STOP_F1 : 8;	/* +0x0 */
		uint32_t PI_TCASOFF_F2 : 8;	/* +0x0 */
		uint32_t PI_TDFI_WCK_STOP_F2 : 8;	/* +0x0 */
		uint32_t PI_LOW_FREQ_MARK_F0 : 1;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_539;

typedef union {
	struct {
		uint32_t PHY_WRLVL_PER_START_0 : 7;	/* +0x0 */
		uint32_t RESERVED_7_7 : 1;	/* +0x0 */
		uint32_t PHY_WRLVL_CAPTURE_CNT_0 : 6;	/* +0x0 */
		uint32_t RESERVED_15_14 : 2;	/* +0x0 */
		uint32_t PHY_WRLVL_UPDT_WAIT_CNT_0 : 4;	/* +0x0 */
		uint32_t RESERVED_23_20 : 4;	/* +0x0 */
		uint32_t PHY_DQ_MASK_0 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PHY_31;

typedef union {
	struct {
		uint32_t PHY_REGULATOR_EN_CNT_0 : 6;	/* +0x0 */
		uint32_t RESERVED_7_6 : 2;	/* +0x0 */
		uint32_t PHY_RX_DFE_EN_0 : 2;	/* +0x0 */
		uint32_t RESERVED_15_10 : 6;	/* +0x0 */
		uint32_t PHY_LP4_BOOT_PHY_RX_DFE_EN_0 : 2;	/* +0x0 */
		uint32_t RESERVED_31_18 : 14;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PHY_67;

typedef union {
	struct {
		uint32_t PHY_LP4_BOOT_WCK_FREQ_RATIO_0 : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t PHY_WCK_SETTING_SOURCE_0 : 2;	/* +0x0 */
		uint32_t RESERVED_15_10 : 6;	/* +0x0 */
		uint32_t PHY_WR_LINK_ECC_EN_0 : 1;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t PHY_PST_AMBLE_0 : 4;	/* +0x0 */
		uint32_t RESERVED_31_28 : 4;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PHY_70;

typedef union {
	struct {
		uint32_t PHY_PAD_DQS_RX_DCD_0 : 5;	/* +0x0 */
		uint32_t RESERVED_7_5 : 3;	/* +0x0 */
		uint32_t PHY_PAD_FDBK_RX_DCD_0 : 5;	/* +0x0 */
		uint32_t RESERVED_15_13 : 3;	/* +0x0 */
		uint32_t PHY_PAD_DSLICE_IO_CFG_0 : 6;	/* +0x0 */
		uint32_t RESERVED_31_22 : 10;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PHY_78;

typedef union {
	struct {
		uint32_t PHY_RDDM_SLAVE_DELAY_0 : 9;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t PHY_GT_RDDQLVL_EN_0 : 1;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t PHY_RX_CAL_ALL_DLY_0 : 6;	/* +0x0 */
		uint32_t RESERVED_31_30 : 2;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PHY_83;

typedef union {
	struct {
		uint32_t PHY_RX_PCLK_CLK_SEL_0 : 3;	/* +0x0 */
		uint32_t RESERVED_7_3 : 5;	/* +0x0 */
		uint32_t PHY_DATA_DC_CAL_CLK_SEL_0 : 3;	/* +0x0 */
		uint32_t RESERVED_15_11 : 5;	/* +0x0 */
		uint32_t PHY_DQ_OE_TIMING_0 : 8;	/* +0x0 */
		uint32_t PHY_DQ_TSEL_RD_TIMING_0 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PHY_84;

typedef union {
	struct {
		uint32_t PHY_LP_DBI_TRAIN_0 : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t PHY_RDDATA_EN_TSEL_DLY_0 : 5;	/* +0x0 */
		uint32_t RESERVED_15_13 : 3;	/* +0x0 */
		uint32_t PHY_RDDATA_EN_OE_DLY_0 : 5;	/* +0x0 */
		uint32_t RESERVED_23_21 : 3;	/* +0x0 */
		uint32_t PHY_SW_MASTER_MODE_0 : 4;	/* +0x0 */
		uint32_t RESERVED_31_28 : 4;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PHY_90;

typedef union {
	struct {
		uint32_t PHY_MASTER_DELAY_HALF_MEASURE_0 : 8;	/* +0x0 */
		uint32_t PHY_RPTR_UPDATE_0 : 4;	/* +0x0 */
		uint32_t RESERVED_15_12 : 4;	/* +0x0 */
		uint32_t PHY_WRLVL_DLY_STEP_0 : 8;	/* +0x0 */
		uint32_t PHY_WRLVL_DLY_FINE_STEP_0 : 4;	/* +0x0 */
		uint32_t RESERVED_31_28 : 4;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PHY_93;

typedef union {
	struct {
		uint32_t PHY_WRLVL_RESP_WAIT_CNT_0 : 6;	/* +0x0 */
		uint32_t RESERVED_7_6 : 2;	/* +0x0 */
		uint32_t PHY_GTLVL_DLY_STEP_0 : 4;	/* +0x0 */
		uint32_t RESERVED_15_12 : 4;	/* +0x0 */
		uint32_t PHY_GTLVL_RESP_WAIT_CNT_0 : 5;	/* +0x0 */
		uint32_t RESERVED_31_21 : 11;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PHY_94;

typedef union {
	struct {
		uint32_t PHY_WDQ_OSC_DELTA_0 : 7;	/* +0x0 */
		uint32_t RESERVED_7_7 : 1;	/* +0x0 */
		uint32_t PHY_MEAS_DLY_STEP_ENABLE_0 : 6;	/* +0x0 */
		uint32_t RESERVED_15_14 : 2;	/* +0x0 */
		uint32_t PHY_RDDATA_EN_DLY_0 : 5;	/* +0x0 */
		uint32_t RESERVED_31_21 : 11;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PHY_104;

typedef union {
	struct {
		uint32_t PHY_CLK_WRDQS_SLAVE_DELAY_0 : 9;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t PHY_WRITE_PATH_LAT_DEC_0 : 1;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t PHY_WRITE_PATH_LAT_ADJ_0 : 2;	/* +0x0 */
		uint32_t RESERVED_31_26 : 6;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PHY_112;

typedef union {
	struct {
		uint32_t PHY_RDDQS_GATE_SLAVE_DELAY_0 : 9;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t PHY_RDDQS_LATENCY_ADJUST_0 : 5;	/* +0x0 */
		uint32_t RESERVED_23_21 : 3;	/* +0x0 */
		uint32_t PHY_WRITE_PATH_LAT_ADD_0 : 3;	/* +0x0 */
		uint32_t RESERVED_31_27 : 5;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PHY_122;

typedef union {
	struct {
		uint32_t PHY_WRITE_PATH_LAT_FRAC_0 : 8;	/* +0x0 */
		uint32_t PHY_GTLVL_RDDQS_SLV_DLY_START_0 : 9;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t PHY_WRLVL_EARLY_FORCE_ZERO_0 : 1;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PHY_123;

typedef union {
	struct {
		uint32_t PHY_GTLVL_LAT_ADJ_START_0 : 5;	/* +0x0 */
		uint32_t RESERVED_7_5 : 3;	/* +0x0 */
		uint32_t PHY_WDQLVL_DQDM_SLV_DLY_START_0 : 10;	/* +0x0 */
		uint32_t RESERVED_23_18 : 6;	/* +0x0 */
		uint32_t PHY_NTP_WRLAT_START_0 : 4;	/* +0x0 */
		uint32_t RESERVED_31_28 : 4;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PHY_124;

typedef union {
	struct {
		uint32_t PHY_ADR_CSLVL_TRAIN_MASK : 7;	/* +0x0 */
		uint32_t RESERVED_7_7 : 1;	/* +0x0 */
		uint32_t PHY_ADR_MEAS_DLY_STEP_VALUE : 8;	/* +0x0 */
		uint32_t PHY_ADR_MEAS_DLY_STEP_ENABLE : 1;	/* +0x0 */
		uint32_t RESERVED_31_17 : 15;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PHY_1027;

typedef union {
	struct {
		uint32_t PHY_ADR_CALVL_RESP_WAIT_CNT : 4;	/* +0x0 */
		uint32_t RESERVED_7_4 : 4;	/* +0x0 */
		uint32_t PHY_ADR_CALVL_PERIODIC_START_OFFSET : 9;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t PHY_ADR_CALVL_DEBUG_MODE : 1;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PHY_1030;

typedef union {
	struct {
		uint32_t PHY_ADR_CALVL_DLY_STEP : 4;	/* +0x0 */
		uint32_t RESERVED_31_4 : 28;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PHY_1043;

typedef union {
	struct {
		uint32_t PHY_FREQ_SEL_FROM_REGIF : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t PHY_FREQ_SEL_MULTICAST_EN : 1;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t PHY_FREQ_SEL_INDEX : 2;	/* +0x0 */
		uint32_t RESERVED_31_18 : 14;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PHY_1281;

typedef union {
	struct {
		uint32_t PHY_PLL_TESTOUT_SEL : 2;	/* +0x0 */
		uint32_t RESERVED_7_2 : 6;	/* +0x0 */
		uint32_t PHY_PLL_REFOUT_SEL : 2;	/* +0x0 */
		uint32_t RESERVED_15_10 : 6;	/* +0x0 */
		uint32_t PHY_LP4_BOOT_LOW_FREQ_SEL : 1;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t PHY_TCKSRE_WAIT : 4;	/* +0x0 */
		uint32_t RESERVED_31_28 : 4;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PHY_1316;

typedef union {
	struct {
		uint32_t PHY_PAD_ACS_IO_CFG : 17;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t PHY_PAD_ACS_RX_PCLK_CLK_SEL : 3;	/* +0x0 */
		uint32_t RESERVED_31_27 : 5;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PHY_1361;

typedef union {
	struct {
		uint32_t PHY_PLL_CTRL : 14;	/* +0x0 */
		uint32_t RESERVED_15_14 : 2;	/* +0x0 */
		uint32_t PHY_PLL_CTRL_8X : 14;	/* +0x0 */
		uint32_t RESERVED_31_30 : 2;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PHY_1364;

typedef struct {
	uint16_t MaxMemFreq;	/* +0x0 */
	uint8_t RankPerCh;	/* +0x2 */
	uint8_t DevWidth;	/* +0x3 */
	uint8_t CA_ODT;	/* +0x4 */
	uint8_t CK_ODT;	/* +0x5 */
	uint8_t CS_ODT;	/* +0x6 */
	uint8_t DQ_ODT;	/* +0x7 */
	uint8_t WCK_ODT;	/* +0x8 */
	uint8_t SOC_ODT;	/* +0x9 */
	uint8_t NTDQ_ODT;	/* +0xa */
	uint8_t ODT_PDDS;	/* +0xb */
	uint16_t DqVref;	/* +0xc */
	uint16_t CaVref;	/* +0xe */
} MEM_CONFIG_BUSCFG_LP5_ENTRY;

typedef struct {
	MEM_CONFIG_BLOCK_HEADER Header;	/* +0x0 */
	MEM_CONFIG_BUSCFG_LP5_ENTRY BusCfg[];	/* +0x10 */
} MEM_CONFIG_BUSCFG_LP5;

union _MR0_DEF {
	struct {
		uint8_t TimingSeparation : 1;	/* +0x0 */
		uint8_t LatencyMode : 1;	/* +0x0 */
		uint8_t EnhWckAlwaysOnMode : 1;	/* +0x0 */
		uint8_t OptimizedRefreshMode : 1;	/* +0x0 */
		uint8_t DmiOutputBehavior : 1;	/* +0x0 */
		uint8_t NtOdtBehavior : 1;	/* +0x0 */
		uint8_t CATR : 1;	/* +0x0 */
		uint8_t RFU_MR0_7 : 1;	/* +0x0 */
	} mr;
	uint8_t AsByte;
};

typedef union _MR0_DEF MR0_DEF;

union _MR1_DEF {
	struct {
		uint8_t RFU_MR1_0_2 : 3;	/* +0x0 */
		uint8_t CkMode : 1;	/* +0x0 */
		uint8_t WL : 4;	/* +0x0 */
	} mr;
	uint8_t AsByte;
};

typedef union _MR1_DEF MR1_DEF;

union _MR2_DEF {
	struct {
		uint8_t RLandRbtp : 4;	/* +0x0 */
		uint8_t WR : 4;	/* +0x0 */
	} mr;
	uint8_t AsByte;
};

typedef union _MR2_DEF MR2_DEF;

union _MR3_DEF {
	struct {
		uint8_t PDDS : 3;	/* +0x0 */
		uint8_t BgOrg : 2;	/* +0x0 */
		uint8_t WLS : 1;	/* +0x0 */
		uint8_t DBI_RD : 1;	/* +0x0 */
		uint8_t DBI_WR : 1;	/* +0x0 */
	} mr;
	uint8_t AsByte;
};

typedef union _MR3_DEF MR3_DEF;

union _MR10_DEF {
	struct {
		uint8_t RdqsPostamble : 1;	/* +0x0 */
		uint8_t RFU_MR10_1 : 1;	/* +0x0 */
		uint8_t WckPostambleLength : 2;	/* +0x0 */
		uint8_t RdqsPreambleLength : 2;	/* +0x0 */
		uint8_t RdqsPostambleLength : 2;	/* +0x0 */
	} mr;
	uint8_t AsByte;
};

typedef union _MR10_DEF MR10_DEF;

union _MR11_DEF {
	struct {
		uint8_t DQ_ODT : 3;	/* +0x0 */
		uint8_t NtOdtEnable : 1;	/* +0x0 */
		uint8_t CA_ODT : 3;	/* +0x0 */
		uint8_t RFU_MR11_7 : 1;	/* +0x0 */
	} mr;
	uint8_t AsByte;
};

typedef union _MR11_DEF MR11_DEF;

union _MR12_DEF {
	struct {
		uint8_t VrefCA : 7;	/* +0x0 */
		uint8_t VBS : 1;	/* +0x0 */
	} mr;
	uint8_t AsByte;
};

typedef union _MR12_DEF MR12_DEF;

union _MR13_DEF {
	struct {
		uint8_t ThermalOffset : 2;	/* +0x0 */
		uint8_t VRO : 1;	/* +0x0 */
		uint8_t RFU_MR13_3 : 1;	/* +0x0 */
		uint8_t DMIControl : 1;	/* +0x0 */
		uint8_t DMD : 1;	/* +0x0 */
		uint8_t CBT : 1;	/* +0x0 */
		uint8_t DualVdd2 : 1;	/* +0x0 */
	} mr;
	uint8_t AsByte;
};

typedef union _MR13_DEF MR13_DEF;

union _MR14_DEF {
	struct {
		uint8_t VrefDQ : 7;	/* +0x0 */
		uint8_t VDLC : 1;	/* +0x0 */
	} mr;
	uint8_t AsByte;
};

typedef union _MR14_DEF MR14_DEF;

union _MR15_DEF {
	struct {
		uint8_t VrefDqUpper;	/* +0x0 */
	} mr;
	uint8_t AsByte;
};

typedef union _MR15_DEF MR15_DEF;

union _MR16_DEF {
	struct {
		uint8_t FspWr : 2;	/* +0x0 */
		uint8_t FspOp : 2;	/* +0x0 */
		uint8_t CBT : 2;	/* +0x0 */
		uint8_t VRCG : 1;	/* +0x0 */
		uint8_t CBTPhase : 1;	/* +0x0 */
	} mr;
	uint8_t AsByte;
};

typedef union _MR16_DEF MR16_DEF;

union _MR17_DEF {
	struct {
		uint8_t SocOdt : 3;	/* +0x0 */
		uint8_t OdtCk : 1;	/* +0x0 */
		uint8_t OdtCs : 1;	/* +0x0 */
		uint8_t OdtCa : 1;	/* +0x0 */
		uint8_t OdtLower : 1;	/* +0x0 */
		uint8_t OdtUpper : 1;	/* +0x0 */
	} mr;
	uint8_t AsByte;
};

typedef union _MR17_DEF MR17_DEF;

union _MR18_DEF {
	struct {
		uint8_t WckOdt : 3;	/* +0x0 */
		uint8_t WckFM : 1;	/* +0x0 */
		uint8_t WckON : 1;	/* +0x0 */
		uint8_t RFU_MR18_5 : 1;	/* +0x0 */
		uint8_t Wck2CkLeveling : 1;	/* +0x0 */
		uint8_t WckRatio : 1;	/* +0x0 */
	} mr;
	uint8_t AsByte;
};

typedef union _MR18_DEF MR18_DEF;

union _MR19_DEF {
	struct {
		uint8_t DVFSC : 2;	/* +0x0 */
		uint8_t DVFSQ : 2;	/* +0x0 */
		uint8_t Wck2DqOsc : 1;	/* +0x0 */
		uint8_t Wck2DqOscSupport : 1;	/* +0x0 */
		uint8_t CsOdt : 2;	/* +0x0 */
	} mr;
	uint8_t AsByte;
};

typedef union _MR19_DEF MR19_DEF;

union _MR20_DEF {
	struct {
		uint8_t Rdqs : 2;	/* +0x0 */
		uint8_t WckMode : 2;	/* +0x0 */
		uint8_t MRWDL : 1;	/* +0x0 */
		uint8_t MRWDU : 1;	/* +0x0 */
		uint8_t RDCDMIMode : 1;	/* +0x0 */
		uint8_t RDCDQMode : 1;	/* +0x0 */
	} mr;
	uint8_t AsByte;
};

typedef union _MR20_DEF MR20_DEF;

union _MR22_DEF {
	struct {
		uint8_t RFU_MR22_3_0 : 4;	/* +0x0 */
		uint8_t WECC : 2;	/* +0x0 */
		uint8_t RECC : 2;	/* +0x0 */
	} mr;
	uint8_t AsByte;
};

typedef union _MR22_DEF MR22_DEF;

union _MR27_DEF {
	struct {
		uint8_t RFM : 1;	/* +0x0 */
		uint8_t RAAIMT : 5;	/* +0x0 */
		uint8_t RAAMULT : 2;	/* +0x0 */
	} mr;
	uint8_t AsByte;
};

typedef union _MR27_DEF MR27_DEF;

union _MR28_DEF {
	struct {
		uint8_t ZQReset : 1;	/* +0x0 */
		uint8_t ZQStop : 1;	/* +0x0 */
		uint8_t ZQInterval : 2;	/* +0x0 */
		uint8_t RFU_MR28_4 : 1;	/* +0x0 */
		uint8_t ZQMode : 1;	/* +0x0 */
		uint8_t RFU_MR28_7_6 : 2;	/* +0x0 */
	} mr;
	uint8_t AsByte;
};

typedef union _MR28_DEF MR28_DEF;

union _MR41_DEF {
	struct {
		uint8_t RFU_MR41_3_0 : 4;	/* +0x0 */
		uint8_t PPRE : 1;	/* +0x0 */
		uint8_t NtDqOdt : 3;	/* +0x0 */
	} mr;
	uint8_t AsByte;
};

typedef union _MR41_DEF MR41_DEF;

typedef struct {
	uint32_t offset;	/* +0x0 */
	uint32_t value;	/* +0x4 */
} REG_VAL_PAIR;

typedef union {
	struct {
		uint32_t PHY_CAL_CLK_SELECT_0 : 3;	/* +0x0 */
		uint32_t RESERVED_7_3 : 5;	/* +0x0 */
		uint32_t PHY_CAL_VREF_SWITCH_TIMER_0 : 8;	/* +0x0 */
		uint32_t PHY_CAL_SETTLING_PRD_0 : 7;	/* +0x0 */
		uint32_t RESERVED_23_23 : 1;	/* +0x0 */
		uint32_t PHY_SLICE_MULTICAST_EN : 1;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PHY_1384;

typedef struct {
	uint16_t AvgFlightTime[4];	/* +0x0 */
	int16_t TraceLenDiff[4];	/* +0x8 */
} TRACE_LEN_DIFF_ENTRY;

typedef struct {
	MEM_CONFIG_BLOCK_HEADER Header;	/* +0x0 */
	TRACE_LEN_DIFF_ENTRY Channel[4];	/* +0x10 */
} MEM_CONFIG_TRACE_LEN_DIFF;

struct _LPDDR5_WL_ENTRY {
	uint16_t Ratio;	/* +0x0 */
	uint16_t MaxFreq;	/* +0x2 */
	uint8_t WLBySet[2];	/* +0x4 */
};

typedef struct _LPDDR5_WL_ENTRY LPDDR5_WL_ENTRY;

struct _LPDDR5_RL_RTP_ENTRY {
	uint16_t Ratio;	/* +0x0 */
	uint16_t MaxFreq;	/* +0x2 */
	struct {
		uint8_t RLBySet[3];	/* +0x0 */
		uint8_t Rbtp;	/* +0x3 */
	} LEccOff;	/* +0x4 */
	struct {
		uint8_t RLBySet[2];	/* +0x0 */
		uint8_t Rbtp;	/* +0x2 */
	} LEccOn;	/* +0x8 */
};

typedef struct _LPDDR5_RL_RTP_ENTRY LPDDR5_RL_RTP_ENTRY;

struct _LPDDR5_TWCK2CK_SYNC_ENTRY {
	uint16_t Ratio;	/* +0x0 */
	uint16_t MaxFreq;	/* +0x2 */
	uint8_t TwckenlFs;	/* +0x4 */
	uint8_t TwckpreStatic;	/* +0x5 */
	uint8_t TwckpreToggleRd;	/* +0x6 */
	uint8_t TwckpreToggleWr;	/* +0x7 */
};

typedef struct _LPDDR5_TWCK2CK_SYNC_ENTRY LPDDR5_TWCK2CK_SYNC_ENTRY;

struct _LPDDR5_NWR_ENTRY {
	uint16_t Ratio;	/* +0x0 */
	uint16_t MaxFreq;	/* +0x2 */
	uint8_t nWR[2][2];	/* +0x4 */
};

typedef struct _LPDDR5_NWR_ENTRY LPDDR5_NWR_ENTRY;

typedef union {
	struct {
		uint32_t MC_RESERVED18 : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t MC_RESERVED19 : 2;	/* +0x0 */
		uint32_t RESERVED_15_10 : 6;	/* +0x0 */
		uint32_t BANK_ARCH_MODE_F0 : 2;	/* +0x0 */
		uint32_t RESERVED_23_18 : 6;	/* +0x0 */
		uint32_t MC_RESERVED20 : 1;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_152;

typedef union {
	struct {
		uint32_t RD_LINK_ECC_EN_F0 : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t WR_LINK_ECC_EN_F0 : 1;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t MC_RESERVED21 : 2;	/* +0x0 */
		uint32_t RESERVED_23_18 : 6;	/* +0x0 */
		uint32_t BANK_ARCH_MODE_F1 : 2;	/* +0x0 */
		uint32_t RESERVED_31_26 : 6;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_153;

typedef union {
	struct {
		uint32_t ZQ_CAL_LATCH_MAP_1 : 2;	/* +0x0 */
		uint32_t RESERVED_7_2 : 6;	/* +0x0 */
		uint32_t BANK_DIFF_0 : 2;	/* +0x0 */
		uint32_t RESERVED_15_10 : 6;	/* +0x0 */
		uint32_t BANK_DIFF_1 : 2;	/* +0x0 */
		uint32_t RESERVED_23_18 : 6;	/* +0x0 */
		uint32_t ROW_DIFF_0 : 3;	/* +0x0 */
		uint32_t RESERVED_31_27 : 5;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_325;

typedef union {
	struct {
		uint32_t ROW_DIFF_1 : 3;	/* +0x0 */
		uint32_t RESERVED_7_3 : 5;	/* +0x0 */
		uint32_t COL_DIFF_0 : 4;	/* +0x0 */
		uint32_t RESERVED_15_12 : 4;	/* +0x0 */
		uint32_t COL_DIFF_1 : 4;	/* +0x0 */
		uint32_t RESERVED_31_20 : 12;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_326;

typedef union {
	struct {
		uint32_t CS_VAL_LOWER_0 : 16;	/* +0x0 */
		uint32_t CS_VAL_UPPER_0 : 16;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_327;

typedef union {
	struct {
		uint32_t MC_RESERVED35 : 3;	/* +0x0 */
		uint32_t RESERVED_7_3 : 5;	/* +0x0 */
		uint32_t CS_MSK_0 : 16;	/* +0x0 */
		uint32_t RESERVED_31_24 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_328;

typedef union {
	struct {
		uint32_t CS_VAL_LOWER_1 : 16;	/* +0x0 */
		uint32_t CS_VAL_UPPER_1 : 16;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_329;

typedef union {
	struct {
		uint32_t MC_RESERVED36 : 3;	/* +0x0 */
		uint32_t RESERVED_7_3 : 5;	/* +0x0 */
		uint32_t CS_MSK_1 : 16;	/* +0x0 */
		uint32_t CS_MAP_NON_POW2 : 2;	/* +0x0 */
		uint32_t RESERVED_31_26 : 6;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_330;

typedef union {
	struct {
		uint32_t PROGRAMMABLE_ADDRESS_ORDER : 4;	/* +0x0 */
		uint32_t RESERVED_7_4 : 4;	/* +0x0 */
		uint32_t BANK_START_BIT : 5;	/* +0x0 */
		uint32_t RESERVED_15_13 : 3;	/* +0x0 */
		uint32_t APREBIT : 5;	/* +0x0 */
		uint32_t RESERVED_23_21 : 3;	/* +0x0 */
		uint32_t AGE_COUNT : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_331;

typedef union {
	struct {
		uint32_t DFI_ERROR_INFO : 20;	/* +0x0 */
		uint32_t RESERVED_23_20 : 4;	/* +0x0 */
		uint32_t BG_ROTATE_EN : 1;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_347;

typedef union {
	struct {
		uint32_t LOWER_ADDR_REGION_0_0 : 32;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_555;

typedef union {
	struct {
		uint32_t LOWER_ADDR_REGION_0_1 : 8;	/* +0x0 */
		uint32_t RESERVED_31_8 : 24;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_556;

typedef union {
	struct {
		uint32_t UPPER_ADDR_REGION_0_0 : 32;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_557;

typedef union {
	struct {
		uint32_t UPPER_ADDR_REGION_0_1 : 8;	/* +0x0 */
		uint32_t RESERVED_31_8 : 24;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_558;

typedef union {
	struct {
		uint32_t LOWER_ADDR_REGION_1_0 : 32;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_559;

typedef union {
	struct {
		uint32_t LOWER_ADDR_REGION_1_1 : 8;	/* +0x0 */
		uint32_t RESERVED_31_8 : 24;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_560;

typedef union {
	struct {
		uint32_t UPPER_ADDR_REGION_1_0 : 32;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_561;

typedef union {
	struct {
		uint32_t UPPER_ADDR_REGION_1_1 : 8;	/* +0x0 */
		uint32_t RESERVED_31_8 : 24;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_562;

typedef union {
	struct {
		uint32_t LOWER_ADDR_REGION_2_0 : 32;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_563;

typedef union {
	struct {
		uint32_t LOWER_ADDR_REGION_2_1 : 8;	/* +0x0 */
		uint32_t RESERVED_31_8 : 24;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_564;

typedef union {
	struct {
		uint32_t UPPER_ADDR_REGION_2_0 : 32;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_565;

typedef union {
	struct {
		uint32_t UPPER_ADDR_REGION_2_1 : 8;	/* +0x0 */
		uint32_t RESERVED_31_8 : 24;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_566;

typedef union {
	struct {
		uint32_t LOWER_ADDR_REGION_3_0 : 32;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_567;

typedef union {
	struct {
		uint32_t LOWER_ADDR_REGION_3_1 : 8;	/* +0x0 */
		uint32_t RESERVED_31_8 : 24;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_568;

typedef union {
	struct {
		uint32_t UPPER_ADDR_REGION_3_0 : 32;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_569;

typedef union {
	struct {
		uint32_t UPPER_ADDR_REGION_3_1 : 8;	/* +0x0 */
		uint32_t ADDR_REMAP_ENABLE : 1;	/* +0x0 */
		uint32_t RESERVED_31_9 : 23;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_570;

typedef union {
	struct {
		uint32_t PI_BANK_DIFF : 2;	/* +0x0 */
		uint32_t RESERVED_7_2 : 6;	/* +0x0 */
		uint32_t PI_ROW_DIFF : 3;	/* +0x0 */
		uint32_t RESERVED_15_11 : 5;	/* +0x0 */
		uint32_t PI_MR12_DATA_BEST_0_F0 : 16;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_222;

typedef union {
	struct {
		uint32_t PI_WR_DBI_EN : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t PI_RD_DBI_EN : 1;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t PI_COL_DIFF : 4;	/* +0x0 */
		uint32_t RESERVED_23_20 : 4;	/* +0x0 */
		uint32_t PI_BG_ROTATE_EN : 1;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_369;

typedef union {
	struct {
		uint32_t START : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t DRAM_CLASS : 4;	/* +0x0 */
		uint32_t RESERVED_15_12 : 4;	/* +0x0 */
		uint32_t CONTROLLER_ID : 16;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_0;

typedef union {
	struct {
		uint32_t AXI0_TRANS_WRFIFO_LOG2_DEPTH : 8;	/* +0x0 */
		uint32_t AXI0_WRCMD_PROC_FIFO_LOG2_DEPTH : 8;	/* +0x0 */
		uint32_t MRR_ERROR_STATUS : 1;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t DFI_FREQ_RATIO_F0 : 2;	/* +0x0 */
		uint32_t RESERVED_31_26 : 6;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_6;

typedef union {
	struct {
		uint32_t DFI_FREQ_RATIO_F1 : 2;	/* +0x0 */
		uint32_t RESERVED_7_2 : 6;	/* +0x0 */
		uint32_t DFI_FREQ_RATIO_F2 : 2;	/* +0x0 */
		uint32_t RESERVED_15_10 : 6;	/* +0x0 */
		uint32_t DFI_CMD_RATIO : 2;	/* +0x0 */
		uint32_t RESERVED_23_18 : 6;	/* +0x0 */
		uint32_t ODT_VALUE : 2;	/* +0x0 */
		uint32_t RESERVED_31_26 : 6;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_7;

typedef union {
	struct {
		uint32_t LPI_WAKEUP_EN : 6;	/* +0x0 */
		uint32_t RESERVED_7_6 : 2;	/* +0x0 */
		uint32_t LPI_CTRL_WAKEUP_EN : 6;	/* +0x0 */
		uint32_t RESERVED_15_14 : 2;	/* +0x0 */
		uint32_t LPI_CTRL_REQ_EN : 1;	/* +0x0 */
		uint32_t RESERVED_31_17 : 15;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_187;

typedef union {
	struct {
		uint32_t TDFI_LP_RESP : 8;	/* +0x0 */
		uint32_t LP_STATE : 7;	/* +0x0 */
		uint32_t RESERVED_15_15 : 1;	/* +0x0 */
		uint32_t LP_AUTO_ENTRY_EN : 4;	/* +0x0 */
		uint32_t RESERVED_23_20 : 4;	/* +0x0 */
		uint32_t LP_AUTO_EXIT_EN : 4;	/* +0x0 */
		uint32_t RESERVED_31_28 : 4;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_190;

typedef union {
	struct {
		uint32_t LP_AUTO_MEM_GATE_EN : 3;	/* +0x0 */
		uint32_t RESERVED_7_3 : 5;	/* +0x0 */
		uint32_t LP_AUTO_PD_IDLE : 12;	/* +0x0 */
		uint32_t RESERVED_31_20 : 12;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_191;

typedef union {
	struct {
		uint32_t LP_AUTO_SR_SHORT_IDLE : 12;	/* +0x0 */
		uint32_t RESERVED_15_12 : 4;	/* +0x0 */
		uint32_t LP_AUTO_SR_LONG_IDLE : 8;	/* +0x0 */
		uint32_t LP_AUTO_SR_LONG_MC_GATE_IDLE : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_192;

typedef union {
	struct {
		uint32_t WRITE_PARITY_ERR_CORRUPT_ECC_EN : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t ENHANCED_PARITY_PROTECTION_EN : 1;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t MEMDATA_RATIO_0 : 3;	/* +0x0 */
		uint32_t RESERVED_23_19 : 5;	/* +0x0 */
		uint32_t MEMDATA_RATIO_1 : 3;	/* +0x0 */
		uint32_t RESERVED_31_27 : 5;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_340;

typedef union {
	struct {
		uint32_t MEM_RST_VALID : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t TDFI_PHY_RDLAT_F0 : 9;	/* +0x0 */
		uint32_t RESERVED_31_17 : 15;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_510;

typedef union {
	struct {
		uint32_t TDFI_RDDATA_EN_F0 : 9;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t TDFI_WRCSLAT_F0 : 9;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_519;

typedef union {
	struct {
		uint32_t TDFI_PHY_RDLAT_F1 : 9;	/* +0x0 */
		uint32_t RESERVED_31_9 : 23;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_521;

typedef union {
	struct {
		uint32_t TDFI_PHY_RDLAT_F2 : 9;	/* +0x0 */
		uint32_t RESERVED_31_9 : 23;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_532;

typedef enum {
	LVL_SRC_INIT = 0,
	LVL_SRC_NON_INIT = 1,
	LVL_SRC_SRX = 2,
	LVL_SRC_DFS = 3,
	LVL_SRC_INTERVAL = 4,
	LVL_SRC_STD_PDX = 5,
	LVL_SRC_LP2_PDX = 6,
	LVL_SRC_MAX = 7,
} MEM_TRAIN_LVL_SRC;

typedef struct {
	uint16_t MaxMemFreq;	/* +0x0 */
	uint8_t RankPerCh;	/* +0x2 */
	uint8_t ChMsk;	/* +0x3 */
	uint8_t Center;	/* +0x4 */
	uint8_t Shift;	/* +0x5 */
	uint8_t Threshold;	/* +0x6 */
	uint8_t rsvd;	/* +0x7 */
} MEM_CONFIG_TRAIN_OPTIMIZE_ENTRY;

typedef struct {
	MEM_CONFIG_BLOCK_HEADER Header;	/* +0x0 */
	MEM_CONFIG_TRAIN_OPTIMIZE_ENTRY TrnOpt[];	/* +0x10 */
} MEM_CONFIG_TRAIN_OPTIMIZE;

typedef union {
	struct {
		uint32_t ddrc_0_lp_ext_req : 1;	/* +0x0 */
		uint32_t ddrc_0_lp_ext_ack : 1;	/* +0x0 */
		uint32_t ddrc_0_lp_ext_fc_reg_copy : 2;	/* +0x0 */
		uint32_t ddrc_0_lp_ext_cmd : 6;	/* +0x0 */
		uint32_t ddrc_0_lp_ext_cmd_strb : 1;	/* +0x0 */
		uint32_t ddrc_0_lp_ext_done : 1;	/* +0x0 */
		uint32_t ddrc_0_lp_ext_resp : 2;	/* +0x0 */
		uint32_t freq_change_ack_ddrp_0 : 1;	/* +0x0 */
		uint32_t freq_change_req_type_ddrp_0 : 5;	/* +0x0 */
		uint32_t freq_change_req_ddrp_0 : 1;	/* +0x0 */
		uint32_t ddrc_0_lp_ext_state : 7;	/* +0x0 */
		uint32_t ddrc_0_cntrl_freq_change_req_type : 2;	/* +0x0 */
		uint32_t ddrc_0_cntrl_freq_change_req : 1;	/* +0x0 */
		uint32_t ddrc_0_cntrl_freq_change_ack : 1;	/* +0x0 */
	} fields;
	uint32_t value;
} DEV_PM_PUBLIC_REG_DDR_FANOUT_0;

typedef union {
	struct {
		uint32_t AREFRESH : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t AREF_STATUS : 1;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t AREF_SW_ACK : 1;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t TREF_ENABLE : 1;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_59;

typedef union {
	struct {
		uint32_t CURRENT_REG_COPY : 2;	/* +0x0 */
		uint32_t RESERVED_7_2 : 6;	/* +0x0 */
		uint32_t CURRENT_FSP : 2;	/* +0x0 */
		uint32_t RESERVED_15_10 : 6;	/* +0x0 */
		uint32_t INIT_FREQ : 2;	/* +0x0 */
		uint32_t RESERVED_23_18 : 6;	/* +0x0 */
		uint32_t INIT_FSP : 2;	/* +0x0 */
		uint32_t RESERVED_31_26 : 6;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_198;

typedef union {
	struct {
		uint32_t INT_ACK_LOWPOWER : 16;	/* +0x0 */
		uint32_t MC_RESERVED45 : 16;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_CTL_362;

typedef union {
	struct {
		uint32_t PI_START : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t PI_DRAM_CLASS : 4;	/* +0x0 */
		uint32_t RESERVED_31_12 : 20;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_0;

typedef union {
	struct {
		uint32_t PI_RELEASE_DFI : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t PI_NORMAL_LVL_SEQ : 1;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t PI_INIT_LVL_EN : 1;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t PI_NOTCARE_PHYUPD : 1;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_3;

typedef union {
	struct {
		uint32_t PI_WRLVL_PERIODIC : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t PI_WRLVL_ON_SREF_EXIT : 1;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t PI_WRLVL_DISABLE_DFS : 1;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t PI_WRLVL_RESP_MASK : 4;	/* +0x0 */
		uint32_t RESERVED_31_28 : 4;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_37;

typedef union {
	struct {
		uint32_t PI_WRLVL_ROTATE : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t PI_WRLVL_CS_MAP : 4;	/* +0x0 */
		uint32_t RESERVED_15_12 : 4;	/* +0x0 */
		uint32_t PI_WRLVL_ERROR_STATUS : 1;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t PI_TDFI_WRLVL_EN : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_38;

typedef union {
	struct {
		uint32_t PI_RDLVL_SEQ_EN : 4;	/* +0x0 */
		uint32_t RESERVED_7_4 : 4;	/* +0x0 */
		uint32_t PI_RDLVL_PERIODIC : 1;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t PI_RDLVL_ON_SREF_EXIT : 1;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t PI_RDLVL_DISABLE_DFS : 1;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_53;

typedef union {
	struct {
		uint32_t PI_RDLVL_GATE_PERIODIC : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t PI_RDLVL_GATE_ON_SREF_EXIT : 1;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t PI_RDLVL_GATE_DISABLE_DFS : 1;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t PI_RDLVL_ROTATE : 1;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_54;

typedef union {
	struct {
		uint32_t PI_RDLVL_GATE_ROTATE : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t PI_RDLVL_CS_MAP : 4;	/* +0x0 */
		uint32_t RESERVED_15_12 : 4;	/* +0x0 */
		uint32_t PI_RDLVL_GATE_CS_MAP : 4;	/* +0x0 */
		uint32_t RESERVED_31_20 : 12;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_55;

typedef union {
	struct {
		uint32_t PI_RDLVL_ERROR_STATUS : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t PI_RDLVL_INTERVAL : 16;	/* +0x0 */
		uint32_t RESERVED_31_24 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_60;

typedef union {
	struct {
		uint32_t PI_DRAMDCA_LVL_ROTATE : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t PI_RDDCM_LVL_CS_MAP : 4;	/* +0x0 */
		uint32_t RESERVED_15_12 : 4;	/* +0x0 */
		uint32_t PI_WRDCM_LVL_CS_MAP : 4;	/* +0x0 */
		uint32_t RESERVED_23_20 : 4;	/* +0x0 */
		uint32_t PI_DRAMDCA_LVL_CS_MAP : 4;	/* +0x0 */
		uint32_t RESERVED_31_28 : 4;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_67;

typedef union {
	struct {
		uint32_t PI_RDDCM_LVL_EN_F0 : 2;	/* +0x0 */
		uint32_t RESERVED_7_2 : 6;	/* +0x0 */
		uint32_t PI_WRDCM_LVL_EN_F0 : 2;	/* +0x0 */
		uint32_t RESERVED_15_10 : 6;	/* +0x0 */
		uint32_t PI_DRAMDCA_LVL_EN_F0 : 2;	/* +0x0 */
		uint32_t RESERVED_23_18 : 6;	/* +0x0 */
		uint32_t PI_RDDCM_LVL_EN_F1 : 2;	/* +0x0 */
		uint32_t RESERVED_31_26 : 6;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_69;

typedef union {
	struct {
		uint32_t PI_WRDCM_LVL_EN_F1 : 2;	/* +0x0 */
		uint32_t RESERVED_7_2 : 6;	/* +0x0 */
		uint32_t PI_DRAMDCA_LVL_EN_F1 : 2;	/* +0x0 */
		uint32_t RESERVED_15_10 : 6;	/* +0x0 */
		uint32_t PI_RDDCM_LVL_EN_F2 : 2;	/* +0x0 */
		uint32_t RESERVED_23_18 : 6;	/* +0x0 */
		uint32_t PI_WRDCM_LVL_EN_F2 : 2;	/* +0x0 */
		uint32_t RESERVED_31_26 : 6;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_70;

typedef union {
	struct {
		uint32_t PI_DRAMDCA_LVL_EN_F2 : 2;	/* +0x0 */
		uint32_t RESERVED_31_2 : 30;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_71;

typedef union {
	struct {
		uint32_t PI_CALVL_DISABLE_DFS : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t PI_CALVL_ROTATE : 1;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t PI_CALVL_CS_MAP : 4;	/* +0x0 */
		uint32_t RESERVED_23_20 : 4;	/* +0x0 */
		uint32_t PI_TDFI_CALVL_EN : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_155;

typedef union {
	struct {
		uint32_t PI_CALVL_RESP_MASK : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t PI_CALVL_ERROR_STATUS : 2;	/* +0x0 */
		uint32_t RESERVED_15_10 : 6;	/* +0x0 */
		uint32_t PI_CALVL_INTERVAL : 16;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_158;

typedef union {
	struct {
		uint32_t PI_WDQLVL_ROTATE : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t PI_WDQLVL_CS_MAP : 4;	/* +0x0 */
		uint32_t RESERVED_15_12 : 4;	/* +0x0 */
		uint32_t PI_WDQLVL_VREF_INITIAL_STEPSIZE : 5;	/* +0x0 */
		uint32_t RESERVED_23_21 : 3;	/* +0x0 */
		uint32_t PI_WDQLVL_VREF_NORMAL_STEPSIZE : 5;	/* +0x0 */
		uint32_t RESERVED_31_29 : 3;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_166;

typedef union {
	struct {
		uint32_t PI_WDQLVL_INTERVAL : 16;	/* +0x0 */
		uint32_t PI_WDQLVL_ON_SREF_EXIT : 1;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t PI_WDQLVL_DISABLE_DFS : 1;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_171;

typedef union {
	struct {
		uint32_t PI_WDQLVL_ERROR_STATUS : 2;	/* +0x0 */
		uint32_t RESERVED_7_2 : 6;	/* +0x0 */
		uint32_t PI_WDQLVL_NEED_SAVE_RESTORE : 2;	/* +0x0 */
		uint32_t RESERVED_15_10 : 6;	/* +0x0 */
		uint32_t PI_WDQLVL_DM_LEVEL_EN : 4;	/* +0x0 */
		uint32_t RESERVED_31_20 : 12;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_172;

typedef union {
	struct {
		uint32_t PI_INT_STATUS_0 : 32;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_296;

typedef union {
	struct {
		uint32_t PI_INT_STATUS_1 : 3;	/* +0x0 */
		uint32_t RESERVED_31_3 : 29;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_297;

typedef union {
	struct {
		uint32_t PI_INT_ACK_0 : 32;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_298;

typedef union {
	struct {
		uint32_t PI_INT_ACK_1 : 2;	/* +0x0 */
		uint32_t RESERVED_31_2 : 30;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_299;

typedef union {
	struct {
		uint32_t PI_RDLVL_EN_F0 : 2;	/* +0x0 */
		uint32_t RESERVED_7_2 : 6;	/* +0x0 */
		uint32_t PI_RDLVL_GATE_EN_F0 : 2;	/* +0x0 */
		uint32_t RESERVED_15_10 : 6;	/* +0x0 */
		uint32_t PI_RDLVL_EN_F1 : 2;	/* +0x0 */
		uint32_t RESERVED_23_18 : 6;	/* +0x0 */
		uint32_t PI_RDLVL_GATE_EN_F1 : 2;	/* +0x0 */
		uint32_t RESERVED_31_26 : 6;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_431;

typedef union {
	struct {
		uint32_t PI_RDLVL_EN_F2 : 2;	/* +0x0 */
		uint32_t RESERVED_7_2 : 6;	/* +0x0 */
		uint32_t PI_RDLVL_GATE_EN_F2 : 2;	/* +0x0 */
		uint32_t RESERVED_15_10 : 6;	/* +0x0 */
		uint32_t PI_RDLVL_RDDQ_EN_F0 : 4;	/* +0x0 */
		uint32_t RESERVED_23_20 : 4;	/* +0x0 */
		uint32_t PI_RDLVL_VREF_EN_F0 : 4;	/* +0x0 */
		uint32_t RESERVED_31_28 : 4;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_432;

typedef union {
	struct {
		uint32_t PI_CALVL_EN_F0 : 2;	/* +0x0 */
		uint32_t RESERVED_7_2 : 6;	/* +0x0 */
		uint32_t PI_CALVL_EN_F1 : 2;	/* +0x0 */
		uint32_t RESERVED_15_10 : 6;	/* +0x0 */
		uint32_t PI_CALVL_EN_F2 : 2;	/* +0x0 */
		uint32_t RESERVED_23_18 : 6;	/* +0x0 */
		uint32_t PI_TMRZ_F0 : 5;	/* +0x0 */
		uint32_t RESERVED_31_29 : 3;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_443;

typedef union {
	struct {
		uint32_t PI_CALVL_VREF_INITIAL_START_POINT_F0 : 7;	/* +0x0 */
		uint32_t RESERVED_7_7 : 1;	/* +0x0 */
		uint32_t PI_CALVL_VREF_INITIAL_STOP_POINT_F0 : 7;	/* +0x0 */
		uint32_t RESERVED_15_15 : 1;	/* +0x0 */
		uint32_t PI_CALVL_VREF_DELTA_F0 : 4;	/* +0x0 */
		uint32_t RESERVED_23_20 : 4;	/* +0x0 */
		uint32_t PI_CALVL_VREF_INITIAL_START_POINT_F1 : 7;	/* +0x0 */
		uint32_t RESERVED_31_31 : 1;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_454;

typedef union {
	struct {
		uint32_t PI_CALVL_VREF_INITIAL_STOP_POINT_F1 : 7;	/* +0x0 */
		uint32_t RESERVED_7_7 : 1;	/* +0x0 */
		uint32_t PI_CALVL_VREF_DELTA_F1 : 4;	/* +0x0 */
		uint32_t RESERVED_15_12 : 4;	/* +0x0 */
		uint32_t PI_CALVL_VREF_INITIAL_START_POINT_F2 : 7;	/* +0x0 */
		uint32_t RESERVED_23_23 : 1;	/* +0x0 */
		uint32_t PI_CALVL_VREF_INITIAL_STOP_POINT_F2 : 7;	/* +0x0 */
		uint32_t RESERVED_31_31 : 1;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_455;

typedef union {
	struct {
		uint32_t PI_WDQLVL_VREF_INITIAL_START_POINT_F1 : 7;	/* +0x0 */
		uint32_t RESERVED_7_7 : 1;	/* +0x0 */
		uint32_t PI_WDQLVL_VREF_INITIAL_STOP_POINT_F1 : 7;	/* +0x0 */
		uint32_t RESERVED_15_15 : 1;	/* +0x0 */
		uint32_t PI_WDQLVL_VREF_DELTA_F1 : 4;	/* +0x0 */
		uint32_t RESERVED_23_20 : 4;	/* +0x0 */
		uint32_t PI_WDQLVL_EN_F1 : 2;	/* +0x0 */
		uint32_t RESERVED_31_26 : 6;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_473;

typedef union {
	struct {
		uint32_t PI_WDQLVL_VREF_INITIAL_START_POINT_F2 : 7;	/* +0x0 */
		uint32_t RESERVED_7_7 : 1;	/* +0x0 */
		uint32_t PI_WDQLVL_VREF_INITIAL_STOP_POINT_F2 : 7;	/* +0x0 */
		uint32_t RESERVED_15_15 : 1;	/* +0x0 */
		uint32_t PI_WDQLVL_VREF_DELTA_F2 : 4;	/* +0x0 */
		uint32_t RESERVED_23_20 : 4;	/* +0x0 */
		uint32_t PI_WDQLVL_EN_F2 : 2;	/* +0x0 */
		uint32_t RESERVED_31_26 : 6;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_476;

typedef union {
	struct {
		uint32_t PI_RDLVL_ADVANCED_EN_F0 : 2;	/* +0x0 */
		uint32_t RESERVED_7_2 : 6;	/* +0x0 */
		uint32_t PI_RDLVL_ADVANCED_EN_F1 : 2;	/* +0x0 */
		uint32_t RESERVED_15_10 : 6;	/* +0x0 */
		uint32_t PI_RDLVL_ADVANCED_EN_F2 : 2;	/* +0x0 */
		uint32_t RESERVED_23_18 : 6;	/* +0x0 */
		uint32_t PI_RDLVL_CS_ADV : 4;	/* +0x0 */
		uint32_t RESERVED_31_28 : 4;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_478;

typedef union {
	struct {
		uint32_t PI_WDQLVL_ADVANCED_EN_F0 : 2;	/* +0x0 */
		uint32_t RESERVED_7_2 : 6;	/* +0x0 */
		uint32_t PI_WDQLVL_ADVANCED_EN_F1 : 2;	/* +0x0 */
		uint32_t RESERVED_15_10 : 6;	/* +0x0 */
		uint32_t PI_WDQLVL_ADVANCED_EN_F2 : 2;	/* +0x0 */
		uint32_t RESERVED_23_18 : 6;	/* +0x0 */
		uint32_t PI_TRTP_F0 : 8;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_479;

typedef union {
	struct {
		uint32_t PHY_PLL_OBS_0 : 16;	/* +0x0 */
		uint32_t RESERVED_31_16 : 16;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PHY_1304;

typedef union {
	struct {
		uint32_t PHY_PLL_OBS_1 : 16;	/* +0x0 */
		uint32_t RESERVED_31_16 : 16;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PHY_1307;

typedef union {
	struct {
		uint32_t PHY_PLL_OBS_2 : 16;	/* +0x0 */
		uint32_t RESERVED_31_16 : 16;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PHY_1310;

typedef union {
	struct {
		uint32_t PHY_PLL_OBS_3 : 16;	/* +0x0 */
		uint32_t RESERVED_31_16 : 16;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PHY_1313;

typedef union {
	struct {
		uint32_t zq_status_in : 2;	/* +0x0 */
		uint32_t block_fm_norm_req : 1;	/* +0x0 */
		uint32_t ci700_addr_bit_drop : 2;	/* +0x0 */
		uint32_t umc_ddr_apbclk_en : 1;	/* +0x0 */
		uint32_t umc_atb_ddr_mux_sel : 3;	/* +0x0 */
		uint32_t mmhub_addr_bit_drop : 2;	/* +0x0 */
		uint32_t rsvd_0 : 21;	/* +0x0 */
	} fields;
	uint32_t value;
} RCSU_DDRC_STRAP_PIN0;

typedef enum {
	TRAINING_IN_PROGRESS = 0,
	WAIT_FOR_REQ = 1,
	FREQ_CHANGE_REQ = 2,
	FREQ_CHANGE_ACK = 3,
	WAIT_FOR_INIT_COMP = 4,
	TRAINING_FAIL = 5,
	TRAINING_DONE = 6,
} MEM_TRAIN_STATUS;

typedef union {
	struct {
		uint32_t PI_INIT_DFS_CALVL_ONLY : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t PI_FSP_FREQ_EN : 1;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t PI_FSP0_FREQ : 5;	/* +0x0 */
		uint32_t RESERVED_23_21 : 3;	/* +0x0 */
		uint32_t PI_FSP1_FREQ : 5;	/* +0x0 */
		uint32_t RESERVED_31_29 : 3;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_11;

typedef union {
	struct {
		uint32_t PI_FREQ_MAP : 32;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_13;

typedef union {
	struct {
		uint32_t PI_INIT_WORK_FREQ : 5;	/* +0x0 */
		uint32_t RESERVED_31_5 : 27;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_14;

typedef union {
	struct {
		uint32_t PI_RESERVED5 : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t PI_CS_MAP : 4;	/* +0x0 */
		uint32_t RESERVED_15_12 : 4;	/* +0x0 */
		uint32_t PI_MC_CS_MAP : 4;	/* +0x0 */
		uint32_t RESERVED_23_20 : 4;	/* +0x0 */
		uint32_t PI_SRX_LVL_TARGET_CS_EN : 1;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_21;

typedef union {
	struct {
		uint32_t PI_ADDRESS_MUX_4 : 3;	/* +0x0 */
		uint32_t RESERVED_7_3 : 5;	/* +0x0 */
		uint32_t PI_ADDRESS_MUX_5 : 3;	/* +0x0 */
		uint32_t RESERVED_15_11 : 5;	/* +0x0 */
		uint32_t PI_ADDRESS_MUX_6 : 3;	/* +0x0 */
		uint32_t RESERVED_23_19 : 5;	/* +0x0 */
		uint32_t PI_DATA_BYTE_SWAP_EN : 1;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_321;

typedef union {
	struct {
		uint32_t PI_DATA_BYTE_SWAP_SLICE0 : 2;	/* +0x0 */
		uint32_t RESERVED_7_2 : 6;	/* +0x0 */
		uint32_t PI_DATA_BYTE_SWAP_SLICE1 : 2;	/* +0x0 */
		uint32_t RESERVED_15_10 : 6;	/* +0x0 */
		uint32_t PI_DATA_BYTE_SWAP_SLICE2 : 2;	/* +0x0 */
		uint32_t RESERVED_23_18 : 6;	/* +0x0 */
		uint32_t PI_DATA_BYTE_SWAP_SLICE3 : 2;	/* +0x0 */
		uint32_t RESERVED_31_26 : 6;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_322;

typedef union {
	struct {
		uint32_t PI_BYTE_MODE_EN : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t PI_WDQLVL_BYTE_MODE_EN : 1;	/* +0x0 */
		uint32_t RESERVED_15_9 : 7;	/* +0x0 */
		uint32_t PI_WRDATA_EN_TUNE_PLUS : 4;	/* +0x0 */
		uint32_t RESERVED_23_20 : 4;	/* +0x0 */
		uint32_t PI_WRDATA_EN_TUNE_MINUS : 4;	/* +0x0 */
		uint32_t RESERVED_31_28 : 4;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_399;

typedef union {
	struct {
		uint32_t PI_RDLVL_PAT0_EN_F0 : 2;	/* +0x0 */
		uint32_t RESERVED_7_2 : 6;	/* +0x0 */
		uint32_t PI_RDLVL_RXCAL_EN_F0 : 2;	/* +0x0 */
		uint32_t RESERVED_15_10 : 6;	/* +0x0 */
		uint32_t PI_RDLVL_DFE_EN_F0 : 4;	/* +0x0 */
		uint32_t RESERVED_23_20 : 4;	/* +0x0 */
		uint32_t PI_RDLVL_MULTI_EN_F0 : 4;	/* +0x0 */
		uint32_t RESERVED_31_28 : 4;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_433;

typedef union {
	struct {
		uint32_t PI_RDLVL_DFE_EN_F1 : 4;	/* +0x0 */
		uint32_t RESERVED_7_4 : 4;	/* +0x0 */
		uint32_t PI_RDLVL_MULTI_EN_F1 : 4;	/* +0x0 */
		uint32_t RESERVED_15_12 : 4;	/* +0x0 */
		uint32_t PI_RDLVL_RDDQ_EN_F2 : 4;	/* +0x0 */
		uint32_t RESERVED_23_20 : 4;	/* +0x0 */
		uint32_t PI_RDLVL_VREF_EN_F2 : 4;	/* +0x0 */
		uint32_t RESERVED_31_28 : 4;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_435;

typedef union {
	struct {
		uint32_t PI_RDLVL_PAT0_EN_F2 : 2;	/* +0x0 */
		uint32_t RESERVED_7_2 : 6;	/* +0x0 */
		uint32_t PI_RDLVL_RXCAL_EN_F2 : 2;	/* +0x0 */
		uint32_t RESERVED_15_10 : 6;	/* +0x0 */
		uint32_t PI_RDLVL_DFE_EN_F2 : 4;	/* +0x0 */
		uint32_t RESERVED_23_20 : 4;	/* +0x0 */
		uint32_t PI_RDLVL_MULTI_EN_F2 : 4;	/* +0x0 */
		uint32_t RESERVED_31_28 : 4;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_436;

typedef union {
	struct {
		uint32_t PI_PREAMBLE_SUPPORT_F1 : 2;	/* +0x0 */
		uint32_t RESERVED_7_2 : 6;	/* +0x0 */
		uint32_t PI_PREAMBLE_SUPPORT_F2 : 2;	/* +0x0 */
		uint32_t RESERVED_15_10 : 6;	/* +0x0 */
		uint32_t PI_TMRR_F0 : 4;	/* +0x0 */
		uint32_t RESERVED_23_20 : 4;	/* +0x0 */
		uint32_t PI_TMRR_F1 : 4;	/* +0x0 */
		uint32_t RESERVED_31_28 : 4;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PI_537;

typedef union {
	struct {
		uint32_t PHY_DQ_TSEL_SELECT_0 : 16;	/* +0x0 */
		uint32_t PHY_DQS_TSEL_ENABLE_0 : 3;	/* +0x0 */
		uint32_t RESERVED_31_19 : 13;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PHY_68;

typedef union {
	struct {
		uint32_t PHY_DQS_TSEL_SELECT_0 : 16;	/* +0x0 */
		uint32_t PHY_TWO_CYC_PREAMBLE_0 : 2;	/* +0x0 */
		uint32_t RESERVED_23_18 : 6;	/* +0x0 */
		uint32_t PHY_WCK_FREQ_RATIO_0 : 1;	/* +0x0 */
		uint32_t RESERVED_31_25 : 7;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PHY_69;

typedef union {
	struct {
		uint32_t PHY_DSLICE_PAD_BOOSTPN_SETTING_0 : 16;	/* +0x0 */
		uint32_t PHY_DSLICE_PAD_RX_CTLE_SETTING_0 : 6;	/* +0x0 */
		uint32_t RESERVED_23_22 : 2;	/* +0x0 */
		uint32_t PHY_DQ_FFE_TAP_0 : 4;	/* +0x0 */
		uint32_t RESERVED_31_28 : 4;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PHY_129;

typedef union {
	struct {
		uint32_t PHY_DQS_FFE_TAP_0 : 4;	/* +0x0 */
		uint32_t RESERVED_7_4 : 4;	/* +0x0 */
		uint32_t PHY_RX_CAL_DQ0_0 : 18;	/* +0x0 */
		uint32_t RESERVED_31_26 : 6;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PHY_130;

typedef union {
	struct {
		uint32_t PHY_LP4_BOOT_WCK_FREQ_RATIO_1 : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t PHY_WCK_SETTING_SOURCE_1 : 2;	/* +0x0 */
		uint32_t RESERVED_15_10 : 6;	/* +0x0 */
		uint32_t PHY_WR_LINK_ECC_EN_1 : 1;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t PHY_PST_AMBLE_1 : 4;	/* +0x0 */
		uint32_t RESERVED_31_28 : 4;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PHY_326;

typedef union {
	struct {
		uint32_t PHY_LP4_BOOT_WCK_FREQ_RATIO_2 : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t PHY_WCK_SETTING_SOURCE_2 : 2;	/* +0x0 */
		uint32_t RESERVED_15_10 : 6;	/* +0x0 */
		uint32_t PHY_WR_LINK_ECC_EN_2 : 1;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t PHY_PST_AMBLE_2 : 4;	/* +0x0 */
		uint32_t RESERVED_31_28 : 4;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PHY_582;

typedef union {
	struct {
		uint32_t PHY_LP4_BOOT_WCK_FREQ_RATIO_3 : 1;	/* +0x0 */
		uint32_t RESERVED_7_1 : 7;	/* +0x0 */
		uint32_t PHY_WCK_SETTING_SOURCE_3 : 2;	/* +0x0 */
		uint32_t RESERVED_15_10 : 6;	/* +0x0 */
		uint32_t PHY_WR_LINK_ECC_EN_3 : 1;	/* +0x0 */
		uint32_t RESERVED_23_17 : 7;	/* +0x0 */
		uint32_t PHY_PST_AMBLE_3 : 4;	/* +0x0 */
		uint32_t RESERVED_31_28 : 4;	/* +0x0 */
	} fields;
	uint32_t value;
} DENALI_PHY_838;

struct __va_list {
	void *__ap;	/* +0x0 */
};

/* firmware_entry: include/storage.h */

enum IRQn {
	IRQn_NMI = -14,
	IRQn_HardFault = -13,
	IRQn_MemManage = -12,
	IRQn_BusFault = -11,
	IRQn_UsageFault = -10,
	IRQn_SecureFault = -9,
	IRQn_SVC = -5,
	IRQn_DBG = -4,
	IRQn_PendSV = -2,
	IRQn_SysTick = -1,
	IRQn_JIAYU_HOST0_INTR = 0,
	IRQn_JIAYU_HOST1_INTR = 1,
	IRQn_JIAYU_HOST2_INTR = 2,
	IRQn_SEC_FAIL = 31,
	IRQn_WDT_LVL1 = 32,
	IRQn_MUL_ROM_PATCH = 44,
	IRQn_HIFI = 64,
	IRQn_PM = 66,
	IRQn_DP0 = 77,
	IRQn_DP1 = 78,
	IRQn_DP2 = 79,
	IRQn_DP3 = 80,
	IRQn_DP4 = 81,
	IRQn_FCH = 88,
	IRQn_GPU = 90,
	IRQn_SMN = 96,
	IRQn_PCIE = 97,
	IRQn_MMHUB = 98,
	IRQn_NPU = 99,
	IRQn_SFU = 105,
	IRQn_Ddrc0 = 219,
	IRQn_Ddrp0 = 220,
	IRQn_Ddrc1 = 221,
	IRQn_Ddrp1 = 222,
	IRQn_Ddrc2 = 223,
	IRQn_Ddrp2 = 224,
	IRQn_Ddrc3 = 225,
	IRQn_Ddrp3 = 226,
	IRQn_PM_WDOGRES = 230,
	IRQn_SE_INTR = 231,
	IRQn_AP_WDOG = 232,
	IRQn_MAX = 256,
};

typedef enum IRQn IRQn_Type;

/* External functions (CIX's static ones are declared in their files) */
void RunMemBistTest(void);	/* MemBist.c */
uint8_t MemDdrcBistTest(uint8_t ChMask, uint32_t BistDataMask, uint64_t StartAddr, uint32_t AddrSpace, MEM_BIST_MODE mode);	/* MemDdrcBist.c */
uint8_t MemDdrpiBistDataCheck(uint8_t ChMask, uint32_t BistDataMask, uint32_t StartAddrHi, uint32_t StartAddrLo, uint32_t AddrSpace, MEM_BIST_MODE mode);	/* MemDdrpiBist.c */
void MemAdjustDqDelay(uint8_t ChannelMsk);	/* MemEyeScan.c */
void MemShiftRdDqDelay(uint8_t ChannelMsk, uint8_t delta);	/* MemEyeScan.c */
void MemBistDataEye(void);	/* MemEyeScan.c */
uint8_t MemDataEyeBistRun(uint8_t Chmsk, uint8_t RankMsk, uint8_t Dq, uint8_t AddrSpace);	/* MemEyeScan.c */
void MemDdrcSetVref(uint8_t Channel, uint8_t Rank, uint8_t Vref, MBIST_EYE_TYPE type);	/* MemEyeScan.c */
void MemDdrcRestoreVref(uint8_t Channel, uint8_t *Vref, MBIST_EYE_TYPE type);	/* MemEyeScan.c */
void MemDdrcSaveVref(uint8_t Channel, uint8_t *Vref, MBIST_EYE_TYPE type);	/* MemEyeScan.c */
void MemDdrcOverrideDqDelayPerDq(uint8_t Channel, uint8_t Dq, uint32_t DqDelay, MBIST_EYE_TYPE type);	/* MemEyeScan.c */
void MemDdrcRestoreDqDelay(uint8_t Channel, uint32_t *DqDelayOri, MBIST_EYE_TYPE type);	/* MemEyeScan.c */
void MemDdrcSaveDqDelay(uint8_t Channel, uint32_t *DqDelayOri, MBIST_EYE_TYPE type);	/* MemEyeScan.c */
void MemBpCheckpoint(uint32_t breakpoint);	/* MemBreakPoint.c */
void MemFeatConfig(void);	/* MemFeatCfg.c */
void MemInlineEccCfg(void);	/* MemFeatCfg.c */
void MemPortPriorityCfg(void);	/* MemPortPriority.c */
void MemRefreshModeCfg(void);	/* MemRefreshMode.c */
void MemRfmModeCfg(void);	/* MemRfmMode.c */
uint32_t CdcbDriverInit(void);	/* Cdcb.c */
uint32_t CdcbLocateBlockBuf(uint32_t guid, uint32_t *size, uint8_t * *buf);	/* Cdcb.c */
void CdcbDeallocateBlockBuf(uint8_t * *buf);	/* Cdcb.c */
uint32_t CdcbGetGlobalHeader(MEM_CONFIG_HEADER * *header);	/* Cdcb.c */
MEM_QUICK_CONFIG * CdcbGetQuickConfig(void);	/* Cdcb.c */
void CdcbDeallocateBuffer(void);	/* Cdcb.c */
void CdcbUpdateBlockBuf(void *buf);	/* Cdcb.c */
bool MemSetupGetField(uint32_t offset, uint8_t width, void *ptr);	/* MemBiosSetupLib.c */
uint8_t DfsEnable(void);	/* MemFeatures.c */
uint8_t DdrIrqPrint(void);	/* MemFeatures.c */
uint8_t DqsOscEn(void);	/* MemFeatures.c */
uint8_t MemBreakpointEn(void);	/* MemFeatures.c */
uint8_t PhyPllBypassEn(void);	/* MemFeatures.c */
uint8_t GetAddressMode(void);	/* MemFeatures.c */
uint8_t IEccEn(void);	/* MemFeatures.c */
uint8_t WrPreambleLen(void);	/* MemFeatures.c */
uint8_t RdPreambleLen(void);	/* MemFeatures.c */
uint8_t RdPostambleEn(void);	/* MemFeatures.c */
uint8_t WrPostambleEn(void);	/* MemFeatures.c */
uint8_t BankArch8BMode(void);	/* MemFeatures.c */
uint8_t SelBankInQEn(void);	/* MemFeatures.c */
uint8_t MemPbrEn(void);	/* MemFeatures.c */
uint8_t AutoPrechargeEn(void);	/* MemFeatures.c */
uint8_t WckAlwaysOn(void);	/* MemFeatures.c */
uint8_t MemRfmEn(void);	/* MemFeatures.c */
uint8_t WriteDbiEn(void);	/* MemFeatures.c */
uint8_t ReadDbiEn(void);	/* MemFeatures.c */
uint8_t DataMaskEn(void);	/* MemFeatures.c */
uint8_t ByteModeEn(void);	/* MemFeatures.c */
uint8_t RdLinkEccEn(uint8_t fsp);	/* MemFeatures.c */
uint8_t WrLinkEccEn(uint8_t fsp);	/* MemFeatures.c */
uint16_t MemFuseGetMaxFrequency(void);	/* MemFuseLib.c */
uint8_t MemFuse8bModeSupported(void);	/* MemFuseLib.c */
uint8_t MemFuseBgModeSupported(void);	/* MemFuseLib.c */
uint8_t MemFuseLp4xSupported(void);	/* MemFuseLib.c */
uint8_t MemFuseLp5Supported(void);	/* MemFuseLib.c */
uint8_t MemFuseGetMaxRankNum(void);	/* MemFuseLib.c */
uint8_t MemFuseGetChannelMsk(void);	/* MemFuseLib.c */
void RegisterDdrcIrq(void);	/* MemIrq.c */
void ddrc_irq_handler(void *ctx);	/* MemIrq.c */
bool SwitchDdrcIrqLog(DDR_IRQ_TYPE type, bool log_en);	/* MemIrq.c */
bool CheckLogFlag(uint32_t flag);	/* MemLogLib.c */
void SetMemLogFilter(uint32_t filter);	/* MemLogLib.c */
void RestoreMemLogFilter(void);	/* MemLogLib.c */
void SaveMemLogFilter(void);	/* MemLogLib.c */
void MemInitialResultOutput(void);	/* MemOutputLib.c */
void MemStructInit(void);	/* MemStructLib.c */
void MemDramFunctionalInit(void);	/* MemStructLib.c */
MEM_BUSCFG_ENTRY_S * GetBusCfgPtr(uint8_t fsp);	/* MemStructLib.c */
MEM_SPD_TIMING_S * GetMemSpdTimingsPtr(void);	/* MemStructLib.c */
LPDDR_TIMING_S * GetMemTimingsPtr(uint8_t Channel, uint8_t fsp);	/* MemStructLib.c */
MEM_CHANNEL_S * GetMemChannelPtr(uint8_t Channel);	/* MemStructLib.c */
MEM_FSP_CONFIG_S * GetMemFspCfgPtr(uint8_t fsp);	/* MemStructLib.c */
MEMORY_INIT_S * GetMemInitPtr(void);	/* MemStructLib.c */
void MemConfigurationInit(void);	/* MemStructLib.c */
void MemFeatBiosSetupOv(void);	/* MemStructLib.c */
void drv_ddr_assert(uint32_t filecode, uint32_t line);	/* MemUtil.c */
void MemorySpaceInit(void);	/* MemUtil.c */
bool MemChannelPopulated(uint8_t Channel);	/* MemUtil.c */
void MemDdrcReset(void);	/* MemUtil.c */
void MemModeRegWrite(uint8_t Channel, uint8_t Rank, uint8_t MrNum, uint8_t Value);	/* MemUtil.c */
uint8_t MemModeRegRead(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t MrNum);	/* MemUtil.c */
void MemDdrcRegWrite(uint32_t offset, uint8_t ch, uint32_t value);	/* MemUtil.c */
uint32_t MemDdrcRegRead(uint32_t offset, uint8_t ch);	/* MemUtil.c */
uint8_t MemGetBootEnv(void);	/* MemUtil.c */
void DdrcPllInit(void);	/* pll.c */
void DdrcPllFrequencyChange(uint32_t Freq);	/* pll.c */
void DdrcDfsPllSwitch(uint8_t FreqSet);	/* pll.c */
uint32_t MemSpdAnalyze(void);	/* MemLpddrSpd.c */
uint8_t MemSpdDecodeDensity(uint8_t SpdDensity);	/* MemLpddrSpd.c */
int8_t GetFineTimeBase(LPDDR_SPD_STRUCT *SpdBuffer);	/* MemLpddrSpd.c */
int8_t GetMediumTimeBase(LPDDR_SPD_STRUCT *SpdBuffer);	/* MemLpddrSpd.c */
bool MemSpdCrcCheck(uint8_t *SpdBuf);	/* MemLpddrSpd.c */
MR_RFM_CFG MemGetRfmMrCfg(uint8_t Channel);	/* MemModeRegisters.c */
void MemModeRegisterOverride(void);	/* MemModeRegisters.c */
uint8_t MemCalcMrVal(uint8_t Mr, uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp);	/* MemModeRegisters.c */
MEM_TECH_FUNC_S * MemGetTechFuncPtr(void);	/* MemTechInit.c */
void MemTechFuncInit(uint8_t DdrType);	/* MemTechInit.c */
void MemPhyTimingCfg(void);	/* MemTiming.c */
void MemDdrcTimingCfg(void);	/* MemTiming.c */
void MemPiTimingCfg(void);	/* MemTiming.c */
uint32_t MemCalculateThresholdNck(uint32_t Speed, uint32_t MinCycleTime, uint32_t MinClocks);	/* MemTiming.c */
uint32_t MemCalculateAcTimingNck(uint32_t Speed, uint32_t MinCycleTime, uint32_t MinClocks);	/* MemTiming.c */
uint32_t MemConvertPsToClks(uint32_t Speed, uint32_t NumberOfPs, bool RoundUp);	/* MemTiming.c */
void MemPlatformLookupBusConfigLp5(void);	/* MemLpddr5BusCfg.c */
void MemLpddr5ModeRegisterOverride(void);	/* MemLpddr5ModeRegister.c */
MR_RFM_CFG MemLpddr5GetRfmMrCfg(uint8_t Channel);	/* MemLpddr5ModeRegister.c */
uint8_t MemLpddr5CalcMR41Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp);	/* MemLpddr5ModeRegister.c */
uint8_t MemLpddr5CalcMR37Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp);	/* MemLpddr5ModeRegister.c */
uint8_t MemLpddr5CalcMR35Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp);	/* MemLpddr5ModeRegister.c */
uint8_t MemLpddr5CalcMR34Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp);	/* MemLpddr5ModeRegister.c */
uint8_t MemLpddr5CalcMR33Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp);	/* MemLpddr5ModeRegister.c */
uint8_t MemLpddr5CalcMR32Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp);	/* MemLpddr5ModeRegister.c */
uint8_t MemLpddr5CalcMR31Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp);	/* MemLpddr5ModeRegister.c */
uint8_t MemLpddr5CalcMR30Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp);	/* MemLpddr5ModeRegister.c */
uint8_t MemLpddr5CalcMR28Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp);	/* MemLpddr5ModeRegister.c */
uint8_t MemLpddr5CalcMR25Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp);	/* MemLpddr5ModeRegister.c */
uint8_t MemLpddr5CalcMR23Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp);	/* MemLpddr5ModeRegister.c */
uint8_t MemLpddr5CalcMR22Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp);	/* MemLpddr5ModeRegister.c */
uint8_t MemLpddr5CalcMR20Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp);	/* MemLpddr5ModeRegister.c */
uint8_t MemLpddr5CalcMR19Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp);	/* MemLpddr5ModeRegister.c */
uint8_t MemLpddr5CalcMR18Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp);	/* MemLpddr5ModeRegister.c */
uint8_t MemLpddr5CalcMR17Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp);	/* MemLpddr5ModeRegister.c */
uint8_t MemLpddr5CalcMR16Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp);	/* MemLpddr5ModeRegister.c */
uint8_t MemLpddr5CalcMR15Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp);	/* MemLpddr5ModeRegister.c */
uint8_t MemLpddr5CalcMR14Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp);	/* MemLpddr5ModeRegister.c */
uint8_t MemLpddr5CalcMR13Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp);	/* MemLpddr5ModeRegister.c */
uint8_t MemLpddr5CalcMR12Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp);	/* MemLpddr5ModeRegister.c */
uint8_t MemLpddr5CalcMR11Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp);	/* MemLpddr5ModeRegister.c */
uint8_t MemLpddr5CalcMR10Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp);	/* MemLpddr5ModeRegister.c */
uint8_t MemLpddr5CalcMR8Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp);	/* MemLpddr5ModeRegister.c */
uint8_t MemLpddr5CalcMR4Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp);	/* MemLpddr5ModeRegister.c */
uint8_t MemLpddr5CalcMR3Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp);	/* MemLpddr5ModeRegister.c */
uint8_t MemLpddr5CalcMR2Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp);	/* MemLpddr5ModeRegister.c */
uint8_t MemLpddr5CalcMR1Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp);	/* MemLpddr5ModeRegister.c */
uint8_t MemLpddr5CalcMR0Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp);	/* MemLpddr5ModeRegister.c */
void MemLpddr5DdrpRegInit(void);	/* MemLpddr5RegInit.c */
void MemLpddr5DdrpiRegInit(void);	/* MemLpddr5RegInit.c */
void MemLpddr5DdrcRegInit(void);	/* MemLpddr5RegInit.c */
void MemLpddr5TimingInit(void);	/* MemLpddr5Timing.c */
void MemLpddr5GetSeqWait(uint8_t Channel, uint8_t fsp, uint32_t *SeqWaitList);	/* MemLpddr5Timing.c */
void MemLpddr5TurnaroundTiming(uint8_t Channel, uint8_t fsp, TURNAROUND_TIMING_S *Timings);	/* MemLpddr5Timing.c */
void MemLpddr5PhyTiming(uint8_t Channel, uint8_t fsp, PHY_TIMING_S *Timings);	/* MemLpddr5Timing.c */
void MemLpddr5PiTiming(uint8_t Channel, uint8_t fsp, PI_TIMING_S *Timings);	/* MemLpddr5Timing.c */
void MemAddressMapping(void);	/* MemAddressMapping.c */
void MemDdrcAfterPhyTrain(void);	/* MemDdrcInit.c */
void MemDdrcInit(void);	/* MemDdrcInit.c */
void MemDdrcEmuOv(void);	/* MemDdrcInit.c */
uint8_t drv_ddr_ecc_enabled(void);	/* drv_ddr.c */
void drv_ddr_set_avail_mem_size(uint32_t size);	/* drv_ddr.c */
uint32_t drv_ddr_get_mem_size(void);	/* drv_ddr.c */
uint32_t drv_ddr_get_ch_num(void);	/* drv_ddr.c */
void drv_ddr_postci(void);	/* drv_ddr.c */
void drv_ddr_init(void);	/* drv_ddr.c */
uint8_t MemDdrssInit(void);	/* MemDdrssInit.c */
uint8_t MemTest(void);	/* MemDdrssInit.c */
uint8_t LpStateSwitch(uint8_t Channel, uint8_t lp_cmd);	/* MemDdrssInit.c */
void ConfigurePiLvlEn(uint32_t pi_lvl_en, uint8_t Channel);	/* MemDdrssInit.c */
void ConfigurePiLvlSrcEn(uint32_t pi_lvl_en, uint8_t Channel);	/* MemDdrssInit.c */
void MemDdrDfsSwtich(uint8_t FreqSet);	/* MemDdrssInit.c */
void MemPiInit(void);	/* MemPhyInit.c */
void MemPiByteSwapCfg(void);	/* MemPhyInit.c */
void MemPhyInit(void);	/* MemPhyInit.c */
void MemPhyPadProgram(void);	/* MemPhyInit.c */
void MemPhyDqSwapCfg(void);	/* MemPhyInit.c */
void MemPhyLEccCfg(void);	/* MemPhyInit.c */
void MemPiDataArrayCfg(void);	/* MemPiDataArray.c */
void MemGetPiDataArray3Settings(uint8_t DdrType, uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp, uint32_t *Array);	/* MemPiDataArray.c */
void MemGetPiDataArray0Settings(uint8_t DdrType, uint8_t Channel, uint32_t *Array);	/* MemPiDataArray.c */
void set_memcfg_img(uint8_t *img, uint32_t offset, uint32_t len);	/* syscall_se.c */
uint8_t get_soc_gpio(uint8_t GpioIndex);	/* syscall_se.c */
void sys_register_ddrc_irq(uint8_t chmsk, void *irq_handler);	/* syscall_se.c */
void disable_ddr_init_monitor(uint8_t ch);	/* syscall_se.c */
uint8_t get_ddr_init_status(uint8_t ch);	/* syscall_se.c */
void enable_ddr_init_monitor(uint8_t ch);	/* syscall_se.c */
uint32_t efuse_get_sw_harveset(void);	/* syscall_se.c */
uint8_t efuse_get_ddr_info(uint8_t *ddr_info, uint32_t ddr_config_size);	/* syscall_se.c */
uint32_t SmnRegRead32(uint32_t addr);	/* syscall_se.c */
void SmnRegWrite32(uint32_t addr, uint32_t val);	/* syscall_se.c */
void postcode(uint32_t pc);	/* syscall_se.c */
void udelay(uint32_t microseconds);	/* syscall_se.c */
uint8_t get_memcfg_img(uint8_t *img, uint32_t *outlen);	/* syscall_se.c */
void log_print(uint32_t flag, const char *fmt, ...);	/* syscall_se.c */
void Tzc400Init(void);	/* MemTzcInit.c */

/* Functions DWARF names only through their abstract origin */
MEM_INIT_OUTPUT_BUFFER *GetMemOutputBuffer(void);	/* MemOutputLib.c */
MEM_FEATURE_S *GetMemFeatPtr(void);	/* MemStructLib.c */
MEM_PHY_PAD_CFG_ENTRY_S *GetPhyPadCfgPtr(uint8_t fsp);	/* MemStructLib.c */

/* What the driver uses from the rest of the firmware */
void *memset(void *s, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
void *malloc(size_t n);
void free(void *p);

/* External variables */
extern MEMORY_INIT_S *gMemInitPtr;	/* MemStructLib.c */
extern DRAM_REF_PARAM Lp5RefParamTbl[5];	/* MemLpddrSpd.c */
extern MEM_TECH_FUNC_S gTechFuncPtr;	/* MemTechInit.c */
extern MEM_MODE_REGISTER_CALLBACK MemLpddr5ModeRegisterCallback;	/* MemLpddr5ModeRegister.c */
extern const LPDDR5_NWR_ENTRY nWRTable[18];	/* MemLpddr5Timing.c */
extern const LPDDR5_WL_ENTRY WriteLatencyTable[18];	/* MemLpddr5Timing.c */
extern const LPDDR5_RL_RTP_ENTRY ReadLatencyTable[18];	/* MemLpddr5Timing.c */
extern const LPDDR5_TWCK2CK_SYNC_ENTRY Twck2ckSyncTable[18];	/* MemLpddr5Timing.c */
extern uint8_t DdrcIrqFlag[4];	/* syscall_se.c */
extern int16_t ddrc_irq_type[4];	/* syscall_se.c: IRQn_Type, CIX builds with short enums */
extern uint8_t ChannelId[4];	/* syscall_se.c */
extern REG_VAL_PAIR Tzc400InitList[5];	/* MemTzcInit.c */

#endif
