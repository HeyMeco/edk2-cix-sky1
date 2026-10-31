#include "MemConfigBinTool.h"
#include "MemConfigurationTable.h"

#define ENTRY_COUNT 2

//
// Vref: the v1.6 table listed 340, 280 here, but its entry struct has a
// reserved byte after ODT_PDDS, so 340 fell into that byte and the board
// has always trained with DqVref 280 and CaVref 0 (auto) -- the retail
// Minisforum image carries the same bytes.  Kept as they ran.
//
MEM_CONFIG_BUSCFG_LP5 MemLpddr5BusCfg = {
  {
    .Signature      = MEM_CONFIG_BLOCK_BUSCFG_LP5_SIGNATURE,
    .BlockSize      = sizeof(MEM_CONFIG_BUSCFG_LP5) + sizeof(MEM_CONFIG_BUSCFG_LP5_ENTRY) * ENTRY_COUNT,
    .BoardMask      = BOARD_ID_MASK_DEFAULT
  },
  {
    //MaxMemFreq        RankPerCh DevWidth CA_ODT   CK_ODT   CS_ODT   DQ_ODT   WCK_ODT  SOC_ODT  NTDQ_ODT ODT_PDDS DqVref CaVref
    {DDR6400_FREQUENCY, RANK_SR,  DEV_x16, RTT_120, RTT_120, RTT_80,  RTT_80,  RTT_80,  RTT_40,  RTT_OFF,  RTT_40,  280, 0},
    {DDR6400_FREQUENCY, RANK_DR,  DEV_x16, RTT_240, RTT_240, RTT_80,  RTT_120, RTT_120, RTT_40,  RTT_OFF,  RTT_40,  280, 0},
  }
};
