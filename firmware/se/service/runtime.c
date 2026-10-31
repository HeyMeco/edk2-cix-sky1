/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Requests the AP can make at run time (CIX service/runtime: service_runtime.c,
 * service_crypto_lib.c, service_fuse.c, service_memrepair.c).
 *
 * Unlike CIX's firmware, secret eFuse fields (device and model keys) are
 * only given to requests on the TEE mailbox, and the hash service only
 * reads DRAM.
 */
#include "se.h"
#include "arch.h"
#include "crypto.h"
#include "ddr.h"
#include "efuse.h"
#include "mailbox.h"
#include "memory.h"
#include "platform.h"
#include "qspi.h"
#include "services.h"
#include "storage.h"

#define RSP_ERR_PARAM		0xfffffffe
#define RSP_ERR_CRYPTO		0xfffffffd

#define IRQn_Ddrc0		219
#define IRQn_Ddrc1		221
#define IRQn_Ddrc2		223
#define IRQn_Ddrc3		225

/* AP physical addresses the SYSHUB window shows at 0x60000000 */
#define AP_DRAM_START		0x80000000u
#define AP_DRAM_END		0xc0000000u

/* Byte ranges of secret fields */
static const struct { uint16_t start, end; } secret_fuses[] = {
	{ 0x054, 0x074 },	/* CIX model and device keys */
	{ 0x0b4, 0x0d4 },	/* test model and device keys */
	{ 0x168, 0x178 },	/* OEM model key */
};

static bool fuse_range_allowed(uint32_t addr, uint32_t bytes)
{
	unsigned int i;

	if (ipc_request_from_tee())
		return true;
	for (i = 0; i < ARRAY_SIZE(secret_fuses); i++)
		if (addr < secret_fuses[i].end && addr + bytes > secret_fuses[i].start)
			return false;
	return true;
}

/* Hash of AP memory: { idx, address, length, mode, 0 } -> { 0, length, digest } */
static void ipc_crypto_hash_handler(void *param, uint32_t plen, void *resp, uint32_t *rlen)
{
	uint32_t *p = param, *r = resp, err;
	sec_sha_context_t ctx;
	sec_sha_t sha;

	if (p == NULL || r == NULL) {
		hal_printf_level(LOG_ERR, "ERR %s, %d\n", __func__, __LINE__);
		return;
	}
	if (p[4] != 0 || p[1] < AP_DRAM_START || p[2] > AP_DRAM_END - p[1]) {
		err = 1;
		goto fail;
	}
	err = sec_sha_init(&sha, p[0]);
	if (err)
		goto fail;
	err = sec_sha_start(&sha, &ctx, p[3]);
	if (!err)
		err = sec_sha_update(&sha, &ctx, (const void *)(uintptr_t)(p[1] - 0x20000000), p[2]);
	if (!err)
		err = sec_sha_finish(&sha, &ctx, (uint8_t *)(r + 2), &r[1]);
	sec_sha_uninit(&sha);
	if (err)
		goto fail;
	r[0] = 0;
	*rlen = r[1] + 8;
	SE_INFO("%s, payload size: %d, rsp size %d\n", __func__, (int)plen, (int)*rlen);
	return;
fail:
	r[0] = RSP_ERR_CRYPTO;
	*rlen = 4;
	SE_INFO("ipc hash error!");
	hexdump("mailbox input", param, MIN(plen, 0x78u));
}

/* Up to 64 random bytes: { length } -> { 0, length, bytes } */
static void ipc_trng_handler(void *param, uint32_t plen, void *resp, uint32_t *rlen)
{
	uint32_t *p = param, *r = resp;

	if (p == NULL || r == NULL) {
		hal_printf_level(LOG_ERR, "ERR %s, %d\n", __func__, __LINE__);
		return;
	}
	if (p[0] <= 64 && sec_trng_get_data((uint8_t *)(r + 2), p[0]) == 0) {
		r[1] = p[0];
		r[0] = 0;
		*rlen = p[0] + 8;
		return;
	}
	r[0] = RSP_ERR_CRYPTO;
	*rlen = 4;
	SE_INFO("ipc trng error!");
}

uint32_t service_crypto_entry(void)
{
	uint32_t err;

	SE_INFO("Service crypto Start...\n");
	err = fw_dispatcher_reg_handler(0x8200000c, ipc_crypto_hash_handler);
	if (!err)
		err = fw_dispatcher_reg_handler(0x82000010, ipc_trng_handler);
	if (err)
		SE_ERR(err);
	return err;
}

/*
 * { flags, id or address, length }: flags bit 0 reads bytes of the OPN
 * area (address 1..2 or 0x4dc..0x4e4); else id 0x240 is the serial number
 * and chip ID, 0xb40 the OEM model key.
 */
static void fuse_id_handler(void *param, uint32_t plen, void *resp, uint32_t *rlen)
{
	uint32_t *p = param, *r = resp, end;
	uint8_t *data = (uint8_t *)(r + 1);

	if (p == NULL || r == NULL) {
		hal_printf_level(LOG_ERR, "ERR %s, %d\n", __func__, __LINE__);
		return;
	}
	r[0] = RSP_ERR_PARAM;
	*rlen = 4;
	if (p[0] & 1) {
		end = p[1] + p[2];
		if (((p[1] == 1 && end <= 2) || (p[1] >= 0x4dc && end <= 0x4e4)) &&
		    fw_efuse_req_from_non_sec(data, p[1], p[2]) == 0) {
			r[0] = 0;
			*rlen = p[2] + 4;
		}
	} else if (p[1] == 0x240) {
		if (fw_efuse_get_pub_ser_num(data) == 0 && fw_efuse_get_chip_id(data + 8) == 0) {
			r[0] = 0;
			*rlen = 16 + 4;
		}
	} else if (p[1] == 0xb40 && ipc_request_from_tee()) {
		if (fw_efuse_get_oem_model_key(data) == 0) {
			r[0] = 0;
			*rlen = 16 + 4;
		}
	}
	if (r[0] == 0)
		SE_INFO("Read fuse: 0x%x\n", (unsigned)*rlen);
	else
		hal_printf_level(LOG_ERR, "ERR %s, %d\n", __func__, __LINE__);
}

/* { address, bit offset, bits } -> { 0, bytes } */
static void fuse_addr_handler(void *param, uint32_t plen, void *resp, uint32_t *rlen)
{
	uint32_t *p = param, *r = resp, bytes;

	if (p == NULL || r == NULL) {
		hal_printf_level(LOG_ERR, "ERR %s, %d\n", __func__, __LINE__);
		return;
	}
	SE_INFO("fuse addr:%d, offset:%d, size:%d\n", (int)p[0], (int)p[1], (int)p[2]);
	bytes = (p[2] + p[1] + 7) / 8;
	if (p[0] < 0x800 && bytes <= 0x70 && p[0] + bytes <= 0x800 && fuse_range_allowed(p[0], bytes) &&
	    sec_efuse_read(&g_handle_efuse, p[0], p[1], p[2], r + 1) == 0) {
		r[0] = 0;
		*rlen = bytes + 4;
		SE_INFO("Read fuse: 0x%x\n", (unsigned)*rlen);
		return;
	}
	r[0] = RSP_ERR_PARAM;
	*rlen = 4;
	hal_printf_level(LOG_ERR, "ERR %s, %d\n", __func__, __LINE__);
}

uint32_t service_fuse_entry(void)
{
	uint32_t err;

	SE_INFO("Service fuse Start...\n");
	err = fw_dispatcher_reg_handler(0x82000002, fuse_id_handler);
	if (!err)
		err = fw_dispatcher_reg_handler(0x8200000e, fuse_addr_handler);
	if (err)
		SE_ERR(err);
	return err;
}

/* { group, index } -> { 0, 1 if data was handed over } */
static void ipc_dis_mem_repair_handler(void *param, uint32_t plen, void *resp, uint32_t *rlen)
{
	uint32_t *p = param, *r = resp, valid = 0;

	*rlen = 8;
	if (p == NULL || r == NULL) {
		hal_printf_level(LOG_ERR, "ERR %s, %d\n", __func__, __LINE__);
		return;
	}
	SE_INFO("memrepair id:%d, index:%d\n", (int)p[0], (int)p[1]);
	if (hal_memr_distribute_by_id(p[0], &valid)) {
		r[0] = RSP_ERR_PARAM;
		r[1] = 0;
		return;
	}
	r[0] = 0;
	r[1] = valid;
	if (valid)
		SE_INFO("ipc distribute memrepair data pass!\n");
	else
		SE_INFO("Memory id: %d don't need repair!\n", (int)p[0]);
}

uint32_t service_memrepair_entry(void)
{
	uint32_t err;

	SE_INFO("Service runtime memrepair start...\n");
	err = fw_dispatcher_reg_handler(0x82000006, ipc_dis_mem_repair_handler);
	if (err)
		SE_ERR(err);
	return err;
}

/* { 0, 2, 0 }: interface version */
static void fw_version_handler(void *param, uint32_t plen, void *resp, uint32_t *rlen)
{
	uint32_t *r = resp;

	if (param == NULL || r == NULL) {
		hal_printf_level(LOG_ERR, "ERR %s, %d\n", __func__, __LINE__);
		return;
	}
	hal_printf_level(LOG_ERR, "[SE_FW]#CiX SE firmware Sky1 %s\n", SE_FW_VERSION);
	r[0] = 0;
	r[1] = 2;
	r[2] = 0;
	*rlen = 12;
}

/* OEM root key hash: 3 not fused, 2 fused, 0 error */
static void ipc_get_oem_pubkey_hash_status_handler(void *param, uint32_t plen, void *resp, uint32_t *rlen)
{
	static const uint8_t zero[32];
	uint8_t fuse[32] = { 0 };
	uint32_t *r = resp;

	if (r == NULL) {
		hal_printf_level(LOG_ERR, "ERR %s, %d\n", __func__, __LINE__);
		return;
	}
	*rlen = 4;
	if (sec_efuse_read(&g_handle_efuse, 0x4e4, 0, 256, fuse)) {
		r[0] = 0;
		SE_INFO("get oem pubkey hash status fail! \n");
	} else if (memcmp(zero, fuse, 32) == 0) {
		r[0] = 3;
		SE_INFO("oem pubkey hash is empty \n");
	} else {
		r[0] = 2;
		SE_INFO("oem pubkey hash is fused \n");
	}
}

/*
 * Blow the OEM root key hash { hash[32], crc16 } into the eFuse: this turns
 * secure boot on for good.  A non-secure part (life cycle 5) also moves to
 * life cycle 3; a secure one first locks debug.  1: done, 2: already
 * fused, 4: bad CRC, 5: read back differs, ~0: failed.
 */
static void ipc_set_oem_pubkey_hash_handler(void *param, uint32_t plen, void *resp, uint32_t *rlen)
{
	static const uint8_t zero[32];
	uint8_t fuse[32] = { 0 };
	uint32_t *r = resp, valid = 1, lc_fuse = 3, lc, err;
	uint16_t crc = 0;

	if (param == NULL || plen < 0x24 || r == NULL) {
		hal_printf_level(LOG_ERR, "ERR %s, %d\n", __func__, __LINE__);
		goto fail;
	}
	*rlen = 4;
	if (hal_crc(param, 32, &crc))
		goto fail;
	hal_printf_level(LOG_ERR, "cal CRC 0x%x buffer CRC 0x%x \n", crc, (unsigned)((uint32_t *)param)[8]);
	if (((uint32_t *)param)[8] != crc) {
		r[0] = 4;
		SE_INFO("oem pubkey hash CRC error!\n");
		return;
	}
	if (sec_efuse_read(&g_handle_efuse, 0x4e4, 0, 256, fuse))
		goto fail;
	if (memcmp(zero, fuse, 32)) {
		r[0] = 2;
		SE_INFO("oem pubkey hash is already fused!\n");
		return;
	}
	hexdump("oem_pkhash_para->oem_pkhash", param, 32);
	if (plat_get_info(PLAT_INFO_LIFE_CYCLE, &lc))
		goto fail;
	if (lc == LIFE_CYCLE_NONSECURE) {
		SE_INFO("un-secure write oem pubkey hash\n");
		err = sec_efuse_program(&g_handle_efuse, 0x4e4, 0, 256, param);
		if (!err)
			err = sec_efuse_program(&g_handle_efuse, 0x15e, 3, 1, &valid);
		if (!err)
			err = sec_efuse_program(&g_handle_efuse, 0, 0, 2, &lc_fuse);
	} else if (lc == LIFE_CYCLE_SECURE) {
		SE_INFO("secure write oem pubkey hash\n");
		REG32(SE_SYSCTRL_ROM_CTRL) |= 1;
		SE_INFO("sec_ctrl:0x%08x\n", (unsigned)REG32(SE_SYSCTRL_ROM_CTRL));
		REG32(SE_SYSCTRL_ROM_CTRL) |= 0x1f0000;
		REG32(SE_SYSCTRL_ROM_CTRL) &= 0xffffff07;
		REG32(SE_SYSCTRL_ROM_CTRL) |= 0x3f00;
		err = sec_efuse_program(&g_handle_efuse, 0x4e4, 0, 256, param);
		if (!err)
			err = sec_efuse_program(&g_handle_efuse, 0x15e, 3, 1, &valid);
	} else {
		goto fail;
	}
	if (err)
		goto fail;
	drv_fuse_re_sense_one_row(0x4e4, 0x20);
	if (sec_efuse_read(&g_handle_efuse, 0x4e4, 0, 256, fuse))
		goto fail;
	if (memcmp(param, fuse, 32)) {
		r[0] = 5;
		SE_INFO("fuse read back compare error\n");
		return;
	}
	r[0] = 1;
	SE_INFO("oem pubkey hash written\n");
	return;
fail:
	if (r) {
		r[0] = 0xffffffff;
		*rlen = 4;
	}
	SE_INFO("write error\n");
}

static void ddr_irq_disable_handler(void *param, uint32_t plen, void *resp, uint32_t *rlen)
{
	if (drv_ddr_get_ch_num() != 2) {
		core_irq_disable(IRQn_Ddrc0);
		core_irq_disable(IRQn_Ddrc1);
		core_irq_unregister(IRQn_Ddrc0);
		core_irq_unregister(IRQn_Ddrc1);
	}
	core_irq_disable(IRQn_Ddrc2);
	core_irq_disable(IRQn_Ddrc3);
	core_irq_unregister(IRQn_Ddrc2);
	core_irq_unregister(IRQn_Ddrc3);
}

/* { value, op }: op 0 reads the MTE setting, op 1 stores value in the SE config */
static void mte_config_handler(void *param, uint32_t plen, void *resp, uint32_t *rlen)
{
	const uint8_t *p = param;

	if (p == NULL || resp == NULL) {
		hal_printf_level(LOG_ERR, "ERR %s, %d\n", __func__, __LINE__);
		return;
	}
	if (p[1] == 0) {
		*(uint8_t *)resp = hal_secfg_get_mte_config();
		*rlen = 1;
	} else if (p[1] == 1) {
		hal_stor_qspi_user_switch(QSPI_USER_SE);
		hal_secfg_set_mte_config(p[0]);
		hal_stor_qspi_user_switch(QSPI_USER_AP);
	}
}

uint32_t service_runtime_entry(void)
{
	static const struct { uint32_t fid; fw_service_cb cb; } handlers[] = {
		{ 0x8200001b, ipc_get_oem_pubkey_hash_status_handler },
		{ 0x8200001a, ipc_set_oem_pubkey_hash_handler },
		{ 0x8200000f, fw_version_handler },
		{ 0x82000011, ddr_irq_disable_handler },
		{ 0x82000020, mte_config_handler },
	};
	uint32_t err, i;

	err = service_str_boot_entry();
	if (!err)
		err = service_crypto_entry();
	if (!err) {
		hal_pc_trace(0x901c);
		err = service_fuse_entry();
	}
	if (!err) {
		hal_pc_trace(0x901d);
		err = service_memrepair_entry();
	}
	for (i = 0; !err && i < ARRAY_SIZE(handlers); i++)
		err = fw_dispatcher_reg_handler(handlers[i].fid, handlers[i].cb);
	if (err)
		SE_ERR(err);
	return err;
}
