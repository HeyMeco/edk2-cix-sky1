/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Secure debug certificate (CIX framework/fw_sec_debug.c).
 *
 * A 4 KiB "CIXSECDEBUG" block at flash 0x107000 (0x207000 by strap) bound
 * to the chip ID lists debug features to open: secure/non-secure
 * invasive and non-invasive debug, JTAG.  With secure boot it must be
 * signed with the OEM key.
 */
#include "se.h"
#include "efuse.h"
#include "storage.h"
#include "xloader.h"

#define SEC_DEBUG_FLASH(strap)	((strap) & 1 ? 0x207000u : 0x107000u)
#define DBG_CTRL		SE_SYSCTRL_ROM_CTRL

typedef struct {
	uint8_t sec_magic[11];
	uint32_t major, minor;
	uint8_t cpu_id[16];
	uint8_t random[16];
	uint32_t feature_num;
	uint32_t sign_type;
} sec_info;

typedef struct {
	uint32_t feature_id;
	uint32_t value;
} fea_ctrl_info;

static uint32_t fw_sec_debug_check_header(const sec_info *s)
{
	uint8_t chip_id[8] = { 0 };
	uint32_t err;

	if (memcmp("CIXSECDEBUG", s->sec_magic, 11)) {
		hal_printf_level(LOG_INFO, "Invalid secure debug binary\n");
		return 0xa805;
	}
	err = fw_efuse_get_chip_id(chip_id);
	if (err)
		return 0xa806;
	if (memcmp(s->cpu_id, chip_id, 8)) {
		hexdump("Chip", chip_id, 8);
		hexdump("Sec_dbg", s->cpu_id, 8);
		hal_printf_level(LOG_ERR, "[SE_FW]Chip ID mismatch\n");
		return 0xa807;
	}
	return 0;
}

static uint32_t fw_sec_debug_enable(const fea_ctrl_info *f, uint32_t n)
{
	static const struct { uint32_t bit; const char *name; } feat[] = {
		[1] = { 0x80, "spniden" }, [2] = { 0x40, "niden" }, [3] = { 0x20, "spiden" },
		[4] = { 0x10, "dbgen" }, [5] = { 0x08, "jtagen" },
	};
	uint32_t i;

	for (i = 0; i < n; i++) {
		if (!f[i].value)
			continue;
		if (f[i].feature_id < 1 || f[i].feature_id > 5)
			return 0xa808;
		REG32(DBG_CTRL) |= feat[f[i].feature_id].bit;
		hal_printf_level(LOG_ERR, "[SE_FW]Enabel %s bit for debug\n", feat[f[i].feature_id].name);
	}
	return 0;
}

uint32_t fw_sec_debug_check(void)
{
	sec_storage_param_t p;
	uint32_t sec = 0, err;
	sec_info hdr;
	uint8_t *buf;

	hal_printf_level(LOG_INFO, "Start to check secure debug binary\n");
	buf = malloc(0x1000);
	if (buf == NULL)
		return 0xa801;
	hal_printf_level(LOG_INFO, "Flash Offset flag from strappng pin is: 0x%x\n",
			 (unsigned)REG32(SE_CLKCTRL_BASE + 0x60));
	p.offset = SEC_DEBUG_FLASH(REG32(SE_CLKCTRL_BASE + 0x60));
	if (hal_stor_load_fw(SEC_STOR_TYPE_FLASH, &p, buf, 0x1000)) {
		err = 0xa802;
		goto out;
	}
	memcpy(&hdr, buf, sizeof(hdr));
	err = fw_sec_debug_check_header(&hdr);
	if (err) {
		err = 0xa804;
		goto out;
	}
	err = fw_efuse_get_info(FUSE_ID_SE_BT_SEC, &sec);
	if (err)
		goto out;
	if (sec == 1) {
		hal_printf_level(LOG_ERR, "[SE_FW]Start to verify signature of secure debug binary\n");
		if (!fw_verify_sec_debug_img(buf)) {
			err = 0xa803;
			goto out;
		}
	}
	err = fw_sec_debug_enable((const fea_ctrl_info *)(buf + 0x200), MIN(hdr.feature_num, 0x1c0u));
out:
	free(buf);
	return err;
}
