/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Memory configuration image, format 2.0 (driver/mem_config.c) */
#ifndef MEMCFG_H
#define MEMCFG_H

#include <stdint.h>

typedef struct {
	uint8_t BoardIdGettingMethod;
	uint8_t BoardId;
	uint8_t BoardIdChanged;
	uint8_t BootEnv;		/* 1: ATE */
	uint32_t LogFilter;		/* DDR log classes; [30:27] SE log level */
	uint8_t vol_major_ver;
	uint8_t dfs_dvfs;
	uint8_t vol_sub_ver;
	uint8_t Reserved_0;
	uint32_t FeatHash;
	uint8_t gpu_opp_patch;
	uint8_t BoardRevId;
	uint8_t BoardRevIdChanged;
	uint8_t Reserved_1[13];
} MEM_QUICK_CONFIG;

typedef struct {
	uint32_t Signature;
	uint32_t Reserved0;
	uint16_t MajorVer;
	uint16_t MinorVer;
	uint16_t BlockTbOffset;
	uint8_t BlockCount;
	uint8_t Reserved1;
	uint8_t HeaderSize;
	uint8_t HeaderCheckSum;
	uint16_t TotalSize;
	uint32_t Reserved2[10];
	uint32_t HashValue;
} MEM_CONFIG_HEADER;

typedef struct {
	uint32_t Signature;
	uint32_t Reserved0;
	uint16_t BlockSize;
	uint8_t BlockChecksum;		/* ~(sum of the block's bytes) */
	uint8_t CompBit;
	uint16_t BoardMask;
	uint16_t Reserved2;
} MEM_CONFIG_BLOCK_HEADER;

typedef struct __attribute__((packed)) {
	MEM_CONFIG_BLOCK_HEADER Header;
	uint16_t MemFreq;		/* MHz (half the data rate); 0xffff automatic */
	uint8_t WckAlwaysOn, DataMask, RfmEn, AutoPrechargeEn, PbrEn, PortPriority;
	uint8_t BdwP0, BdwP1, MbistEn, MbistMode, EyeScanEn, BpEn;
	uint8_t ChannelMask, WrLEcc, RdLEcc;
	uint8_t RPriorityP0, WPriorityP0, BdwOvflowP0, RPriorityP1, WPriorityP1, BdwOvflowP1;
	uint8_t WrPostamble, RdPostamble, WrDbi, RdDbi, IEcc, DdrIrqPrt;
} MEM_CONFIG_BIOS_SETUP;

uint32_t drv_memcfg_flash_read(uint32_t offset, void *buf, uint32_t len);
uint32_t drv_memcfg_flash_write(uint32_t offset, const uint8_t *buf, uint32_t len);
uint32_t drv_memcfg_get_blk_setup(uint32_t blk_guid, void *blk, uint32_t len);
uint32_t drv_memcfg_set_blk_setup(uint32_t blk_guid, void *blk, uint32_t len);
uint32_t drv_memcfg_base_init(void);
uint32_t drv_memcfg_get_base(void);
uint32_t drv_memcfg_get_boot_env(uint32_t *is_ate);
uint32_t drv_memcfg_set_vf_ver(uint32_t major, uint32_t minor);
uint32_t drv_memcfg_get_vf_ver(uint32_t *major, uint32_t *minor);
uint32_t drv_memcfg_set_cpu_freq_mode(uint32_t mode);
uint32_t drv_memcfg_get_cpu_freq_mode(uint32_t *mode);
uint32_t drv_memcfg_get_ddr_chmsk(uint32_t *chmsk);
uint32_t drv_memcfg_set_ddr_chmsk(uint32_t chmsk);
uint32_t drv_memcfg_get_ddr_speed(uint32_t *speed);
uint32_t drv_memcfg_set_ddr_speed(uint32_t speed);
uint32_t drv_memcfg_get_ddr_ecc_feature(uint32_t *read_ecc, uint32_t *write_ecc);
uint32_t drv_memcfg_enable_ecc_check(void);
uint32_t drv_memcfg_disable_ecc_check(void);
uint32_t drv_memcfg_get_ddr_iecc_feature(uint32_t *iecc);
uint32_t drv_memcfg_enable_iecc_check(void);
uint32_t drv_memcfg_disable_iecc_check(void);
uint32_t drv_memcfg_set_se_log_lv(uint32_t level);
uint32_t drv_memcfg_get_se_log_lv(uint32_t *level);
uint32_t drv_memcfg_set_ddr_log(uint32_t enable);
uint32_t drv_memcfg_dump_ddr_log_mode(void);
uint32_t drv_memcfg_set_pm_strap_info(void);
void drv_memcfg_init(void);

#endif
