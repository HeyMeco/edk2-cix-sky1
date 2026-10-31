#ifndef _MEM_CONFIG_BIN_LAYOUT_H_
#define _MEM_CONFIG_BIN_LAYOUT_H_

// default
extern MEM_QUICK_CONFIG             QuickConfigBlock;
extern MEM_CONFIG_BLOCK_CONFIG      GlobalConfigBlock_4g;
extern MEM_CONFIG_BLOCK_CONFIG      GlobalConfigBlock_8g;
extern MEM_CONFIG_BLOCK_CONFIG      GlobalConfigBlock_16g;
extern MEM_CONFIG_BLOCK_CONFIG      GlobalConfigBlock_32g;

extern MEM_CONFIG_BLOCK_FEATURE     MemFeatureBlock;
extern MEM_CONFIG_BUSCFG_LP5        MemLpddr5BusCfg;
extern MEM_CONFIG_PHYPADCFG         MemPhyPadCfg;
extern MEM_CONFIG_BUSCFG_LP5        MemLpddr5BusCfg_x8;
extern MEM_CONFIG_BUSCFG_LP5        MemLpddr5BusCfg_meigao_32G;
extern MEM_CONFIG_PHYPADCFG         MemPhyPadCfg_x8;
extern MEM_CONFIG_PHYPADCFG         MemPhyPadCfg_x8_meigao_32G;
extern MEM_CONFIG_BIOS_SETUP        MemBiosSetup;
extern MEM_CONFIG_DQ_SWAP           DqDmMapBlock;
extern MEM_CONFIG_BDID_GET_METHOD   BoardIdGetMethod;
extern MEM_CONFIG_TRACE_LEN_DIFF    MemTraceLenDiffCfg;
extern MEM_CONFIG_TRAIN_OPTIMIZE    MemTrainOptimize;

#define CDCB_BLOCK_LIST \
  /* default */ \
  CDCB_BLOCK(MemBiosSetup), \
  CDCB_BLOCK(BoardIdGetMethod), \
  CDCB_BLOCK(GlobalConfigBlock_4g), \
  CDCB_BLOCK(GlobalConfigBlock_8g), \
  CDCB_BLOCK(GlobalConfigBlock_16g), \
  CDCB_BLOCK(GlobalConfigBlock_32g), \
  CDCB_BLOCK(MemFeatureBlock), \
  CDCB_BLOCK(MemLpddr5BusCfg), \
  CDCB_BLOCK(MemPhyPadCfg), \
  CDCB_BLOCK(MemLpddr5BusCfg_x8), \
  CDCB_BLOCK(MemLpddr5BusCfg_meigao_32G), \
  CDCB_BLOCK(MemPhyPadCfg_x8), \
  CDCB_BLOCK(MemPhyPadCfg_x8_meigao_32G), \
  CDCB_BLOCK(DqDmMapBlock), \
  CDCB_BLOCK(MemTraceLenDiffCfg), \
  CDCB_BLOCK(MemTrainOptimize), \

#endif
