#include "MemConfigBinTool.h"
#include "MemConfigurationTable.h"
#include "BoardConfig.h"

MEM_CONFIG_BLOCK_CONFIG  GlobalConfigBlock_4g = {
  {
    .Signature      = MEM_CONFIG_BLOCK_CONFIG_SIGNAUTE,
    .BlockSize      = sizeof(MEM_CONFIG_BLOCK_CONFIG),
    .BoardMask      = DRAM_4G_MASK
  },
  .MaxFreq          = DDR5500_FREQUENCY,
  .ChMask           = 0xF,
  .DdrType          = DDR_TYPE_LPDDR5,
  .DeviceDensity    = 16,
  .DeviceWidth      = 16,
  .RankNum          = 1,
};

MEM_CONFIG_BLOCK_CONFIG  GlobalConfigBlock_8g = {
  {
    .Signature      = MEM_CONFIG_BLOCK_CONFIG_SIGNAUTE,
    .BlockSize      = sizeof(MEM_CONFIG_BLOCK_CONFIG),
    .BoardMask      = DRAM_8G_MASK
  },
  .MaxFreq          = DDR5500_FREQUENCY,
  .ChMask           = 0xF,
  .DdrType          = DDR_TYPE_LPDDR5,
  .DeviceDensity    = 16,
  .DeviceWidth      = 16,
  .RankNum          = 2,
};

MEM_CONFIG_BLOCK_CONFIG  GlobalConfigBlock_16g = {
  {
    .Signature      = MEM_CONFIG_BLOCK_CONFIG_SIGNAUTE,
    .BlockSize      = sizeof(MEM_CONFIG_BLOCK_CONFIG),
    .BoardMask      = DRAM_16G_MASK
  },
  .MaxFreq          = DDR5500_FREQUENCY,
  .ChMask           = 0xF,
  .DdrType          = DDR_TYPE_LPDDR5,
  .DeviceDensity    = 16,
  .DeviceWidth      = 8,
  .RankNum          = 2,
};

MEM_CONFIG_BLOCK_CONFIG  GlobalConfigBlock_32g = {
  {
    .Signature      = MEM_CONFIG_BLOCK_CONFIG_SIGNAUTE,
    .BlockSize      = sizeof(MEM_CONFIG_BLOCK_CONFIG),
    .BoardMask      = DRAM_32G_MASK
  },
  .MaxFreq          = DDR5500_FREQUENCY,
  .ChMask           = 0xF,
  .DdrType          = DDR_TYPE_LPDDR5,
  .DeviceDensity    = 16,
  .DeviceWidth      = 8,
  .RankNum          = 2,
};