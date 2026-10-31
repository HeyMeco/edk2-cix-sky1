/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Memory configuration image in flash (CIX driver/memory/drv_mem_config.c).
 *
 * The bootloader1 image "memory config" (format 2.0) starts with a 32-byte
 * quick configuration (board ID, boot environment, log filter, V/F and
 * DVFS choices), followed by a header and a table of blocks the DDR driver
 * reads.  The AP changes some settings through the SE (setup menu), which
 * rewrites the 4 KiB flash sectors concerned.
 */
#include "se.h"
#include "memcfg.h"
#include "qspi.h"
#include "storage.h"
#include "pvt.h"

#define MEMCFG_HDR_OFFSET	0x20
#define MEMCFG_BLKTBL_OFFSET	0x60
#define MEMCFG_BLK_BIOS_SETUP	0x1007
#define FLASH_SECTOR		0x1000
#define PM_STRAP		0x46510800u	/* CSU_PM boot strap, two words */

#define FUSE_DDR_CH_HARVEST	(SE_EFUSE_BASE + 0x4cc)	/* bits [7:4]: channels fused off */

static uint32_t mem_cfg_addr_s;
static uint8_t g_sector[FLASH_SECTOR] __attribute__((aligned(4)));

uint32_t crc32(const uint8_t *buf, uint32_t len);

uint32_t drv_memcfg_flash_read(uint32_t offset, void *buf, uint32_t len)
{
	return drv_qspi_flash_read(QSPI_READ_1_1_1, offset, buf, len);
}

/* Rewrite one sector with len bytes of buf at offset, and check it */
static uint32_t write_sector(uint32_t sector, uint32_t offset, const uint8_t *buf, uint32_t len)
{
	uint32_t err, crc;

	err = drv_qspi_flash_read(QSPI_READ_1_1_1, sector, g_sector, FLASH_SECTOR);
	if (err) {
		SE_ERR(err);
		return err;
	}
	memcpy(g_sector + offset, buf, len);
	crc = crc32(g_sector, FLASH_SECTOR);
	err = drv_qspi_flash_erase(sector, FLASH_SECTOR);
	if (!err)
		err = drv_qspi_flash_write(sector, g_sector, FLASH_SECTOR);
	if (!err)
		err = drv_qspi_flash_read(QSPI_READ_1_1_1, sector, g_sector, FLASH_SECTOR);
	if (err) {
		SE_ERR(err);
		return err;
	}
	if (crc32(g_sector, FLASH_SECTOR) != crc)
		hal_printf_level(LOG_INFO, "[%s:%d]flash write 4k fail, crc wrong!\n", __func__, __LINE__);
	return 0;
}

/* Up to one sector's worth, possibly across two sectors, below 8 MiB */
uint32_t drv_memcfg_flash_write(uint32_t offset, const uint8_t *buf, uint32_t len)
{
	uint32_t first = offset & ~(FLASH_SECTOR - 1), second = (offset + len) & ~(FLASH_SECTOR - 1);
	uint32_t head = len, err;

	if (len > FLASH_SECTOR || buf == NULL || offset >= 0x800000)
		return 0xffffffff;
	if (len == 0)
		return 0;
	if (first != second)
		head = FLASH_SECTOR - (offset & (FLASH_SECTOR - 1));
	err = write_sector(first, offset & (FLASH_SECTOR - 1), buf, head);
	if (err || first == second || len == head)
		return err;
	return write_sector(second, 0, buf + head, len - head);
}

static uint32_t quick_cfg(MEM_QUICK_CONFIG *q)
{
	uint32_t err = drv_memcfg_flash_read(mem_cfg_addr_s, q, sizeof(*q));

	if (err)
		SE_ERR(err);
	return err;
}

static uint32_t quick_cfg_write(MEM_QUICK_CONFIG *q)
{
	uint32_t err = drv_memcfg_flash_write(mem_cfg_addr_s, (const uint8_t *)q, sizeof(*q));

	if (err)
		SE_ERR(err);
	return err;
}

static uint32_t _drv_memcfg_get_board_info(uint32_t *board_id)
{
	MEM_QUICK_CONFIG q;
	uint32_t err = quick_cfg(&q);

	if (!err)
		*board_id = q.BoardId;
	return err;
}

/* Flash offset of block blk_guid, from the block table */
static uint32_t blk_offset(uint32_t blk_guid, uint32_t *offset)
{
	MEM_CONFIG_HEADER hdr;
	uint16_t *tbl;
	uint32_t err, i, n;

	err = drv_memcfg_flash_read(mem_cfg_addr_s + MEMCFG_HDR_OFFSET, &hdr, sizeof(hdr));
	if (err)
		return err;
	n = hdr.BlockCount;
	tbl = malloc(n * 4);
	if (tbl == NULL)
		return 0xffffffff;
	err = drv_memcfg_flash_read(mem_cfg_addr_s + MEMCFG_BLKTBL_OFFSET, tbl, n * 4);
	for (i = 0; !err && i < n; i++) {
		if (tbl[2 * i] == blk_guid) {
			*offset = mem_cfg_addr_s + MEMCFG_HDR_OFFSET + tbl[2 * i + 1];
			break;
		}
	}
	/*
	 * Not found is an error.  (CIX's loop also fails a match on the last
	 * entry and, for no match, reads the offset from past the table.)
	 */
	if (!err && i >= n)
		err = 0xffffffff;
	free(tbl);
	return err;
}

uint32_t drv_memcfg_get_blk_setup(uint32_t blk_guid, void *blk, uint32_t len)
{
	uint32_t offset, err = blk_offset(blk_guid, &offset);

	if (err)
		return err;
	return drv_memcfg_flash_read(offset, blk, len) ? 0xffffffff : 0;
}

/* Byte 10 of a block header: complement of the sum of its bytes (size at byte 8) */
uint32_t drv_memcfg_set_blk_setup(uint32_t blk_guid, void *blk, uint32_t len)
{
	MEM_CONFIG_BLOCK_HEADER *h = blk;
	uint8_t *p = blk, sum = 0;
	uint32_t offset, err, i;

	err = blk_offset(blk_guid, &offset);
	if (err)
		return err;
	h->BlockChecksum = 0;
	for (i = 0; i < h->BlockSize; i++)
		sum += p[i];
	h->BlockChecksum = ~sum;
	return drv_memcfg_flash_write(offset, blk, len) ? 0xffffffff : 0;
}

uint32_t drv_memcfg_base_init(void)
{
	firmware_entry e;
	uint32_t err = hal_stor_get_firmware_entry(MEMORY_CONFIG_IMG_DATA_TYPE, &e);

	if (err) {
		SE_ERR(err);
		return err;
	}
	mem_cfg_addr_s = e.img_addr;
	return 0;
}

uint32_t drv_memcfg_get_base(void)
{
	return mem_cfg_addr_s;
}

uint32_t drv_memcfg_get_boot_env(uint32_t *is_ate)
{
	MEM_QUICK_CONFIG q;
	uint32_t err = quick_cfg(&q);

	if (!err)
		*is_ate = q.BootEnv == 1;
	return err;
}

uint32_t drv_memcfg_set_vf_ver(uint32_t major, uint32_t minor)
{
	MEM_QUICK_CONFIG q;
	uint32_t err = quick_cfg(&q);

	if (err)
		return err;
	q.vol_major_ver = major;
	q.vol_sub_ver = minor;
	return quick_cfg_write(&q);
}

uint32_t drv_memcfg_get_vf_ver(uint32_t *major, uint32_t *minor)
{
	MEM_QUICK_CONFIG q;
	uint32_t err = quick_cfg(&q);

	if (!err) {
		*major = q.vol_major_ver;
		*minor = q.vol_sub_ver;
	}
	return err;
}

uint32_t drv_memcfg_set_cpu_freq_mode(uint32_t mode)
{
	MEM_QUICK_CONFIG q;
	uint32_t err = quick_cfg(&q);

	if (err)
		return err;
	q.dfs_dvfs = mode;
	return quick_cfg_write(&q);
}

/* 0xff (erased): mode 2 */
uint32_t drv_memcfg_get_cpu_freq_mode(uint32_t *mode)
{
	MEM_QUICK_CONFIG q;
	uint32_t err = quick_cfg(&q);

	if (!err)
		*mode = q.dfs_dvfs == 0xff ? 2 : q.dfs_dvfs;
	return err;
}

static uint32_t bios_setup(MEM_CONFIG_BIOS_SETUP *s)
{
	uint32_t err = drv_memcfg_get_blk_setup(MEMCFG_BLK_BIOS_SETUP, s, sizeof(*s));

	if (err)
		SE_ERR(err);
	return err;
}

static uint32_t bios_setup_write(MEM_CONFIG_BIOS_SETUP *s)
{
	uint32_t err = drv_memcfg_set_blk_setup(MEMCFG_BLK_BIOS_SETUP, s, sizeof(*s));

	if (err)
		SE_ERR(err);
	return err;
}

/* Channels: the setting, less those the eFuse harvests */
uint32_t drv_memcfg_get_ddr_chmsk(uint32_t *chmsk)
{
	MEM_CONFIG_BIOS_SETUP s;
	uint32_t err = bios_setup(&s);

	if (!err)
		*chmsk = s.ChannelMask & ~(REG32(FUSE_DDR_CH_HARVEST) >> 4) & 0xf;
	return err;
}

uint32_t drv_memcfg_set_ddr_chmsk(uint32_t chmsk)
{
	MEM_CONFIG_BIOS_SETUP s;
	uint32_t err;

	if (chmsk != 0xf && chmsk != 0xc)
		return 0xffffffff;
	err = bios_setup(&s);
	if (err)
		return err;
	s.ChannelMask = chmsk & ~(REG32(FUSE_DDR_CH_HARVEST) >> 4) & 0xf;
	return bios_setup_write(&s);
}

/* MT/s; 0xffff: automatic */
uint32_t drv_memcfg_get_ddr_speed(uint32_t *speed)
{
	MEM_CONFIG_BIOS_SETUP s;
	uint32_t err = bios_setup(&s);

	if (!err)
		*speed = s.MemFreq == 0xffff ? 0xffff : (uint32_t)s.MemFreq * 2;
	return err;
}

uint32_t drv_memcfg_set_ddr_speed(uint32_t speed)
{
	MEM_CONFIG_BIOS_SETUP s;
	uint32_t err = bios_setup(&s);

	if (err)
		return err;
	s.MemFreq = speed / 2;
	return bios_setup_write(&s);
}

uint32_t drv_memcfg_get_ddr_ecc_feature(uint32_t *read_ecc, uint32_t *write_ecc)
{
	MEM_CONFIG_BIOS_SETUP s;
	uint32_t err = bios_setup(&s);

	if (!err) {
		*read_ecc = s.RdLEcc;
		*write_ecc = s.WrLEcc;
	}
	return err;
}

static uint32_t set_ecc(int link, int inline_ecc, uint8_t on)
{
	MEM_CONFIG_BIOS_SETUP s;
	uint32_t err = bios_setup(&s);

	if (err)
		return err;
	if (link)
		s.WrLEcc = s.RdLEcc = on;
	if (inline_ecc)
		s.IEcc = on;
	return bios_setup_write(&s);
}

uint32_t drv_memcfg_enable_ecc_check(void) { return set_ecc(1, 0, 1); }
uint32_t drv_memcfg_disable_ecc_check(void) { return set_ecc(1, 0, 0); }
uint32_t drv_memcfg_enable_iecc_check(void) { return set_ecc(0, 1, 1); }
uint32_t drv_memcfg_disable_iecc_check(void) { return set_ecc(0, 1, 0); }

uint32_t drv_memcfg_get_ddr_iecc_feature(uint32_t *iecc)
{
	MEM_CONFIG_BIOS_SETUP s;
	uint32_t err = bios_setup(&s);

	if (!err)
		*iecc = s.IEcc;
	return err;
}

/* SE log level: LogFilter[30:27] (0xf: not set) */
uint32_t drv_memcfg_set_se_log_lv(uint32_t level)
{
	MEM_QUICK_CONFIG q;
	uint32_t err = quick_cfg(&q);

	if (err)
		return err;
	q.LogFilter = (q.LogFilter & 0x87ffffff) | (level & 0xf) << 27;
	return quick_cfg_write(&q);
}

uint32_t drv_memcfg_get_se_log_lv(uint32_t *level)
{
	MEM_QUICK_CONFIG q;
	uint32_t err;

	if (level == NULL)
		return 0xffffffff;
	err = quick_cfg(&q);
	if (!err)
		*level = (q.LogFilter & 0x7fffffff) >> 27;
	return err;
}

/* DDR driver log classes: off, or base, IRQ, breakpoint, debug, MR and fatal on */
uint32_t drv_memcfg_set_ddr_log(uint32_t enable)
{
	MEM_QUICK_CONFIG q;
	uint32_t err = quick_cfg(&q);

	if (err)
		return err;
	q.LogFilter &= 0x7ffe7ff8;
	if (enable)
		q.LogFilter |= 0x80018007;
	return quick_cfg_write(&q);
}

uint32_t drv_memcfg_dump_ddr_log_mode(void)
{
	static const struct { uint32_t bit; const char *name; } cls[] = {
		{ BIT(0), "[BASE_INFO] " }, { BIT(1), "[IRQ_INFO]  " }, { BIT(2), "[BP_INFO]   " },
		{ BIT(15), "[DBG_INFO]  " }, { BIT(16), "[MR_WARN]   " }, { BIT(31), "[FATAL_ERR] " },
	};
	MEM_QUICK_CONFIG q;
	uint32_t err = quick_cfg(&q), i;

	if (err)
		return err;
	for (i = 0; i < ARRAY_SIZE(cls); i++)
		hal_printf_level(LOG_ERR, "MEMCFG: DDR LOG: %s---- %s \n", cls[i].name,
				 q.LogFilter & cls[i].bit ? "open" : "close");
	return 0;
}

/*
 * Boot strap for CSU_PM: board ID, DVFS mode, V/F version, PVT average and
 * whether only channels 2 and 3 are in use, with a checksum byte.
 */
uint32_t drv_memcfg_set_pm_strap_info(void)
{
	uint32_t is_ate, board_id, dvfs, vf_m, vf_s, chmsk, err;
	const PVT_INFO_S *pvt;
	uint8_t s[8] = { 0 }, sum = 0;
	uint32_t w[2];
	int i;

	err = drv_memcfg_get_boot_env(&is_ate);
	if (err || is_ate)
		return err;
	err = _drv_memcfg_get_board_info(&board_id);
	if (!err)
		err = drv_memcfg_get_cpu_freq_mode(&dvfs);
	if (!err)
		err = drv_pvt_get_vf_ver(&vf_m, &vf_s);
	if (err) {
		SE_ERR(err);
		return err;
	}
	s[1] = board_id;
	s[3] = (dvfs & 0xf) | (vf_s & 3) << 4;
	s[2] = vf_m & 0xf;
	hal_printf_level(LOG_ERR, "[SE_FW]# PM V/F - %d.%d\n", (int)vf_m, (int)vf_s);
	pvt = drv_pvt_get_vf_info();
	s[4] = pvt->average & 0xff;
	s[5] = (pvt->average >> 8) & 3;
	hal_printf_level(LOG_ERR, "[SE_FW]# PM PVT average - %d\n", pvt->average);
	err = drv_memcfg_get_ddr_chmsk(&chmsk);
	if (err) {
		SE_ERR(err);
		return err;
	}
	s[2] |= (chmsk == 0xc) << 4;
	for (i = 1; i < 8; i++)
		sum += s[i];
	s[0] = ~sum;
	memcpy(w, s, 8);
	REG32(PM_STRAP) = w[0];
	REG32(PM_STRAP + 4) = w[1];
	hal_printf_level(LOG_INFO, "[%s:%d]@@@:%08x, %08x \n", __func__, __LINE__,
			 (unsigned)w[0], (unsigned)w[1]);
	return 0;
}

void drv_memcfg_init(void)
{
	uint32_t is_ate, board_id, speed, chmsk, rd, wr, mode, err;

	err = drv_memcfg_get_boot_env(&is_ate);
	if (err) {
		SE_ERR(err);
		return;
	}
	if (is_ate)
		return;
	if (_drv_memcfg_get_board_info(&board_id) || drv_memcfg_get_ddr_speed(&speed) ||
	    drv_memcfg_get_ddr_chmsk(&chmsk) || drv_memcfg_get_ddr_ecc_feature(&rd, &wr)) {
		SE_ERR(0xb);
		return;
	}
	hal_printf_level(LOG_WARN, "#####display important memory configs as follow#####\n");
	hal_printf_level(LOG_WARN, "board-id: %02x \n", (unsigned)((board_id & 7) | (board_id >> 6) << 3));
	if (speed == 0xffff)
		hal_printf_level(LOG_WARN, "ddr-speed: %s\n", "AUTO");
	else
		hal_printf_level(LOG_WARN, "ddr-speed: %d MHZ \n", (int)speed);
	if (chmsk == 0xff)
		hal_printf_level(LOG_WARN, "ddr-chmsk: %s\n", "AUTO");
	else
		hal_printf_level(LOG_WARN, "ddr-chmsk: %02x\n", (unsigned)chmsk);
	if (rd == 0xff || wr == 0xff)
		hal_printf_level(LOG_WARN, "ddr-ecc:   %s\n", "AUTO");
	else
		hal_printf_level(LOG_WARN, "ddr-ecc:   WrLEcc[%d] RdLEcc[%d]\n", (int)wr, (int)rd);
	if (drv_memcfg_get_cpu_freq_mode(&mode)) {
		SE_ERR(0xb);
		return;
	}
	hal_printf_level(LOG_WARN, "pm_cpu_freq: %d\n", (int)mode);
}
