/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Firmware storage (CIX hal/common/hal_storage.c).
 *
 * The boot flash sits on the SE's QSPI controller, or on the FCH's xSPI if
 * the eFuse says so.  The boot ROM leaves the bootloader1 header's per-flash
 * read parameters in SRAM; the firmware table (at 1 or 2 MiB, by strap)
 * locates the other images; the SE config image holds a few settings the
 * AP's setup can change.
 */
#include "se.h"
#include "efuse.h"
#include "memcfg.h"
#include "platform.h"
#include "qspi.h"
#include "storage.h"

/* What the boot ROM leaves: the bootloader1 header's QSPI table */
#define ROM_HDR			0x2007e800u
#define ROM_QSPI_NUMBER		(ROM_HDR + 0x800)
#define ROM_QSPI_CONFIG		(ROM_HDR + 0x804)	/* qspi_param_t[30] */
#define FLASH_STRAP		(SE_CLKCTRL_BASE + 0x60)
#define FW_TABLE_SIZE		0x1010
#define SE_CONFIG_MAGIC		"CIXSECONFIG"
#define DFD_NV			0x56000514u

#define ERR_STOR_PARAM		0x9303
#define ERR_STOR_QSPI_CFG	0x9309
#define ERR_STOR_READ		0x930a
#define ERR_STOR_SEC_PA		0x9901

static uint32_t jedec_id_value;
static bool use_fch;			/* flash on the FCH xSPI */
static bool xspi_ready;
static uint32_t se_config_base;
static se_config_t se_config_all;


/* The ROM's read parameters for the flash fitted (by JEDEC ID) */
static uint32_t get_qspi_config(qspi_param_t *param)
{
	uint32_t n = REG32(ROM_QSPI_NUMBER), i, lc = 0, err;

	for (i = 0; i < n; i++) {
		if (REG32(ROM_QSPI_CONFIG + i * 16) == jedec_id_value) {
			memcpy(param, (const void *)(ROM_QSPI_CONFIG + i * 16), sizeof(*param));
			break;
		}
	}
	err = plat_get_info(PLAT_INFO_LIFE_CYCLE, &lc);
	if (err) {
		SE_ERR(err);
		return err;
	}
	if (lc != LIFE_CYCLE_BLANK && i == n) {
		SE_INFO("Mismatch qspi config\n");
		return ERR_STOR_QSPI_CFG;
	}
	return 0;
}

static uint32_t read_jedec(void)
{
	uint8_t id[3] = { 0 };

	if (drv_qspi_read_jedec_id(id))
		return ERR_STOR_QSPI_CFG;
	jedec_id_value = (uint32_t)id[0] << 16 | id[1] << 8 | id[2];
	return 0;
}

static void xspi_init_once(void)
{
	if (!xspi_ready) {
		drv_xspi_init();
		xspi_ready = true;
	}
}

uint32_t hal_stor_init(void)
{
	uint8_t id[3] = { 0 };
	uint32_t info = 0, err;

	err = sec_efuse_read(&g_handle_efuse, 0x155, 0, 1, &info);
	if (err) {
		SE_ERR(err);
		return err;
	}
	use_fch = info != 0;
	hal_printf_level(LOG_INFO, "Flash spi type: 0x%x\n", (unsigned)info);
	if (drv_qspi_read_jedec_id(id)) {
		SE_ERR(ERR_STOR_SEC_PA);
		return ERR_STOR_SEC_PA;
	}
	hal_printf_level(LOG_INFO, "Flash JEDEC ID: 0x%x 0x%x 0x%x\n", id[0], id[1], id[2]);
	if (id[0] == 0xef && id[1] == 0x8a && id[2] == 0x17) {
		if (drv_qspi_init_sec_pa(1) || drv_qspi_init_sec_pa(2)) {
			SE_ERR(ERR_STOR_SEC_PA);
			return ERR_STOR_SEC_PA;
		}
	}
	return 0;
}

/* The bootloader1 header: 4 KiB at param->offset, with the default read command */
uint32_t hal_stor_load_header(sec_storage_type_t type, sec_storage_param_t *param, uint8_t *header,
			      uint32_t *len)
{
	if (header == NULL) {
		SE_ERR(ERR_STOR_PARAM);
		return ERR_STOR_PARAM;
	}
	if (use_fch) {
		hal_printf_level(LOG_ERR, "Read by fch when load header\n");
		xspi_init_once();
		if (drv_xspi_flash_read(param->offset, header, *len)) {
			SE_ERR(ERR_STOR_QSPI_CFG);
			return ERR_STOR_QSPI_CFG;
		}
		*len = 0x1000;
		return 0;
	}
	if (read_jedec()) {
		SE_ERR(ERR_STOR_QSPI_CFG);
		return ERR_STOR_QSPI_CFG;
	}
	*len = 0x1000;
	if (drv_qspi_flash_read(QSPI_READ_1_1_1, param->offset, header, 0x1000)) {
		SE_ERR(ERR_STOR_QSPI_CFG);
		return ERR_STOR_QSPI_CFG;
	}
	return 0;
}

/*
 * Reads with the flash's fast parameters (QSPI, the JEDEC ID read once per
 * operation as CIX does), or through the FCH.
 */
typedef struct {
	bool ready;
	qspi_param_t p;
} reader_t;

static uint32_t fast_read(reader_t *r, uint32_t offset, void *buf, uint32_t len)
{
	if (use_fch) {
		xspi_init_once();
		return drv_xspi_flash_read(offset, buf, len) ? ERR_STOR_QSPI_CFG : 0;
	}
	if (!r->ready) {
		if (read_jedec() || get_qspi_config(&r->p))
			return ERR_STOR_QSPI_CFG;
		r->ready = true;
	}
	return drv_qspi_flash_fast_read(&r->p, offset, buf, len) ? ERR_STOR_READ : 0;
}

uint32_t hal_stor_load_fw(sec_storage_type_t type, sec_storage_param_t *param, uint8_t *img,
			  uint32_t len)
{
	reader_t r = { 0 };
	uint32_t err;

	if (img == NULL || param->offset == 0) {
		SE_ERR(ERR_STOR_PARAM);
		return ERR_STOR_PARAM;
	}
	if (use_fch)
		hal_printf_level(LOG_ERR, "Read by fch when load se fw\n");
	err = fast_read(&r, param->offset, img, len);
	if (err)
		SE_ERR(err);
	return err;
}

/* The firmware table: a count at 8, entries from 0x10 */
static uint32_t find_entry(reader_t *r, sub_image_type_t type, firmware_entry *e)
{
	uint32_t base, err, i, n, t;
	uint8_t *tbl;

	base = REG32(FLASH_STRAP) & 1 ? 0x200000 : 0x100000;
	tbl = malloc(FW_TABLE_SIZE);
	if (tbl == NULL) {
		SE_ERR(ERR_STOR_READ);
		return ERR_STOR_READ;
	}
	err = fast_read(r, base, tbl, FW_TABLE_SIZE);
	if (err) {
		free(tbl);
		SE_ERR(err);
		return err;
	}
	memcpy(&n, tbl + 8, 4);
	for (i = 0; i < n && (i + 1) * 16 + 16 <= FW_TABLE_SIZE; i++) {
		memcpy(&t, tbl + 16 + i * 16, 4);
		if (t == (uint32_t)type) {
			memcpy(e, tbl + 16 + i * 16, sizeof(*e));
			free(tbl);
			return 0;
		}
	}
	free(tbl);
	SE_INFO("Mismatch Sub image type\n");
	return ERR_STOR_READ;
}

uint32_t hal_stor_get_firmware_entry(sub_image_type_t type, firmware_entry *e)
{
	reader_t r = { 0 };

	if (e == NULL) {
		SE_ERR(ERR_STOR_PARAM);
		return ERR_STOR_PARAM;
	}
	return find_entry(&r, type, e);
}

uint32_t hal_stor_get_sub_img(sub_image_type_t type, uint8_t *img, uint32_t *len)
{
	reader_t r = { 0 };
	firmware_entry e;
	uint32_t err;

	if (img == NULL || len == NULL) {
		SE_ERR(ERR_STOR_PARAM);
		return ERR_STOR_PARAM;
	}
	hal_printf_level(LOG_INFO, "Flash Offset flag from strappng pin is: 0x%x\n",
			 (unsigned)REG32(FLASH_STRAP));
	err = find_entry(&r, type, &e);
	if (err)
		return err;
	err = fast_read(&r, e.img_addr, img, e.img_size);
	if (err) {
		SE_ERR(err);
		return err;
	}
	*len = e.img_size;
	return 0;
}

void hal_secfg_init(void)
{
	firmware_entry e;

	if (hal_stor_get_firmware_entry(SE_CONFIG_IMG_DATA_TYPE, &e)) {
		hal_printf_level(LOG_ERR, "ERR %s, %d\n", __func__, __LINE__);
		return;
	}
	se_config_base = e.img_addr;
	hal_printf_level(LOG_INFO, "SE config base address is: 0x%x\n", (unsigned)se_config_base);
	if (drv_qspi_flash_read(QSPI_READ_1_1_1, se_config_base, &se_config_all, sizeof(se_config_all))) {
		hal_printf_level(LOG_ERR, "ERR %s, %d\n", __func__, __LINE__);
		return;
	}
	if (memcmp(&se_config_all, SE_CONFIG_MAGIC, strlen(SE_CONFIG_MAGIC))) {
		hal_printf_level(LOG_ERR, "[SE_FW]# SE config not set, use default value!\n");
		memset(&se_config_all, 0, sizeof(se_config_all));
		se_config_all.low_power_gate = 1;
	}
	hal_printf_level(LOG_INFO, "low_power_gate: %d\n", (int)se_config_all.low_power_gate);
	hal_printf_level(LOG_INFO, "ta_enc_config: %d\n", (int)se_config_all.ta_enc_config);
	hal_printf_level(LOG_INFO, "dfd_nv_set: %d\n", (int)se_config_all.dfd_nv_set);
	hal_printf_level(LOG_ERR, "[SE_FW]# mte_config: %d\n", (int)se_config_all.mte_config);
	REG32(DFD_NV) = se_config_all.dfd_nv_set;
	hal_printf_level(LOG_INFO, "Dfd nv value(0x%x) for debug\n", (unsigned)se_config_all.dfd_nv_set);
}

uint32_t hal_secfg_get_ta_enc_config(void)
{
	return se_config_all.ta_enc_config;
}

uint32_t hal_secfg_get_mte_config(void)
{
	return se_config_all.mte_config;
}

uint32_t hal_secfg_set_mte_config(uint32_t enable)
{
	se_config_all.mte_config = enable;
	return hal_secfg_update();
}

/* Write se_config_all back (it may straddle two sectors) */
uint32_t hal_secfg_update(void)
{
	return drv_memcfg_flash_write(se_config_base, (const uint8_t *)&se_config_all,
				      sizeof(se_config_all));
}
