/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Flash through the FCH's Cadence xSPI controller (CIX driver/xspi/drv_xspi.c),
 * for boards whose eFuse says the boot flash hangs off the FCH.
 *
 * STIG mode: a command phase (opcode, address), then a data phase whose
 * bytes move through the slave DMA window.
 */
#include "se.h"
#include "efuse.h"
#include "platform.h"
#include "qspi.h"

#define XSPI_BASE		0x44180000u
#define XSPI_CMD(n)		(XSPI_BASE + (n) * 4)	/* STIG command registers 0..5 */
#define XSPI_CMD_STATUS		(XSPI_BASE + 0x044)
#define XSPI_CTRL_STATUS	(XSPI_BASE + 0x100)
#define XSPI_INTR_STATUS	(XSPI_BASE + 0x110)
#define XSPI_INTR_ENABLE	(XSPI_BASE + 0x230)
#define XSPI_SDMA_SIZE		(XSPI_BASE + 0x240)
#define XSPI_SDMA_TRD_INFO	(XSPI_BASE + 0x244)
#define XSPI_AUX_CLK		(0x44181000u + 0x8)
#define XSPI_SDMA_PORT		0x40010000u
#define FCH_CTRL		0x44160000u

#define XSPI_POLL		0x2faf0800u
#define INTR_SDMA_ERR		BIT(22)
#define INTR_SDMA_TRIGGER	BIT(21)

#define SPI_FLASH_INFO_COUNT	0x188800u	/* flash: entries of the flash info table */
#define SPI_FLASH_INFO		0x188804u	/* 16 bytes each: JEDEC ID reversed, [5] 2: quad, [6] dummy bits */

enum spi_mem_data_dir { SPI_MEM_NO_DATA, SPI_MEM_DATA_IN, SPI_MEM_DATA_OUT };

typedef struct {
	struct { uint8_t nbytes, buswidth; uint16_t opcode; } cmd;
	struct { uint8_t nbytes, buswidth; uint64_t val; } addr;
	struct { uint8_t nbytes, buswidth; } dummy;
	struct { uint8_t buswidth; uint8_t dir; uint32_t nbytes; } data;
} spi_mem_op;

static const uint8_t xspi_bus_clk_table[7] = { 25, 20, 50, 33, 16, 12, 5 };
static uint32_t mSdmaCompleted;

static uint32_t ilog2(uint32_t v)
{
	uint32_t n = 0;

	while (v >>= 1)
		n++;
	return n;
}

static int xspi_command_complete_status(void)
{
	uint32_t i, st;

	for (i = 1; i != XSPI_POLL; i++) {
		st = REG32(XSPI_INTR_STATUS);
		if (!(st & 0xe00000))
			continue;
		if (st & INTR_SDMA_ERR) {
			hal_printf_level(LOG_ERR, "Slave DMA transaction error\n");
			mSdmaCompleted = 1;
		}
		if (st & INTR_SDMA_TRIGGER)
			mSdmaCompleted = 1;
		REG32(XSPI_INTR_STATUS) = st;
		return 0;
	}
	hal_printf_level(LOG_ERR, " Command transaction not completed\n");
	return -1;
}

static void stig(const uint32_t r[6])
{
	int i;

	for (i = 5; i >= 0; i--)		/* register 0 last: it starts the phase */
		REG32(XSPI_CMD(i)) = r[i];
}

static void sdma_copy(uint8_t *data)
{
	uint32_t size = REG32(XSPI_SDMA_SIZE), i;
	volatile uint8_t *port8 = (volatile uint8_t *)XSPI_SDMA_PORT;

	if (((REG32(XSPI_SDMA_TRD_INFO) >> 8) & 1) == 0) {	/* read */
		if (size < 4) {
			for (i = 0; i < size; i++)
				data[i] = *port8;
			return;
		}
		for (i = 0; i < size / 4; i++)
			((uint32_t *)data)[i] = REG32(XSPI_SDMA_PORT);
		for (i = size & ~3u; i < size; i++)
			data[i] = *port8;
	} else {
		for (i = 0; i < size; i++)
			*port8 = data[i];
	}
}

static int spi_nor_spimem_exec_op(const spi_mem_op *op, uint8_t *data)
{
	uint32_t r[6] = { 0 }, addr = (uint32_t)op->addr.val, addr_hi = op->addr.val >> 32;
	uint32_t i, st, dummy;
	int ret;

	for (i = 1; REG32(XSPI_CTRL_STATUS) & BIT(7); i++) {
		if (i == XSPI_POLL - 1) {
			hal_printf_level(LOG_ERR, "wait for controller idle timeout\n");
			return -1;
		}
	}
	REG32(XSPI_INTR_ENABLE) = 0x20;

	/* command phase */
	r[1] = (addr & 0xff) << 24 | (op->data.dir != SPI_MEM_NO_DATA);
	r[2] = ((addr >> 16) & 0xff) << 8 | (addr >> 24) << 16 | ((addr >> 8) & 0xff) | (addr_hi & 0xff) << 24;
	r[3] = (uint32_t)op->addr.nbytes << 28 | (uint32_t)op->cmd.opcode << 16 | ((addr_hi >> 8) & 0xff);
	r[4] = ilog2(op->cmd.buswidth) << 8 | ilog2(op->addr.buswidth);
	stig(r);

	if (op->data.dir != SPI_MEM_NO_DATA) {
		dummy = op->dummy.buswidth ? op->dummy.nbytes * 8u / op->dummy.buswidth : 0;
		r[0] = 1;
		r[1] = 0x7f;
		r[2] = (op->data.nbytes & 0xffff) << 16;
		r[3] = dummy << 20 | op->data.nbytes >> 16;
		r[4] = (op->data.dir != SPI_MEM_DATA_IN) << 4 | ilog2(op->data.buswidth) << 8;
		r[5] = 0;
		stig(r);
		if (xspi_command_complete_status()) {
			hal_printf_level(LOG_ERR, "wait for cmd status timeout\n");
			return -1;
		}
		if (mSdmaCompleted == 1)
			sdma_copy(data);
	}
	if (xspi_command_complete_status())
		return -1;

	st = REG32(XSPI_CMD_STATUS);
	if (!(st & BIT(15))) {
		hal_printf_level(LOG_ERR, "Fatal err - command not completed\n");
		return -1;
	}
	if (st & BIT(14)) {
		ret = 0;
		if (st & BIT(3)) {
			hal_printf_level(LOG_ERR, "Incorrect DQS pulses detected\n");
			ret = -1;
		}
		if (st & BIT(2)) {
			hal_printf_level(LOG_ERR, "CRC  error\n");
			ret = -1;
		}
		if (st & BIT(1)) {
			hal_printf_level(LOG_ERR, "bus error\n");
			ret = -1;
		}
		if (st & BIT(0)) {
			hal_printf_level(LOG_ERR, "Invalid command sequence detected\n");
			ret = -1;
		}
		if (ret)
			return ret;
	}
	hal_printf_level(LOG_ERR, "xspi transferred size=%d\n", (int)op->data.nbytes);
	return 0;
}

static int drv_xspi_flash_read_1_1_1(uint32_t from, uint8_t *data, uint32_t len)
{
	spi_mem_op op = {
		.cmd = { 1, 1, 0x03 },
		.addr = { 3, 1, from },
		.data = { 1, SPI_MEM_DATA_IN, len },
	};

	return spi_nor_spimem_exec_op(&op, data);
}

/* Quad read (0x6b) if the flash info table in flash says the flash can, else 1-1-1 */
uint32_t drv_xspi_flash_read(uint32_t from, void *data, uint32_t len)
{
	uint8_t info[1024], id[6] = { 0 }, exp[3];
	uint32_t n, i;
	spi_mem_op op = {
		.cmd = { 1, 1, 0x9f },
		.data = { 1, SPI_MEM_DATA_IN, 3 },
	};

	if (drv_xspi_flash_read_1_1_1(SPI_FLASH_INFO_COUNT, info, 1) == 0) {
		n = info[0];
		if (drv_xspi_flash_read_1_1_1(SPI_FLASH_INFO, info, n * 16) == 0) {
			if (spi_nor_spimem_exec_op(&op, id))
				hal_printf_level(LOG_ERR, "can not read jedec id\n");
			for (i = 0; i < n; i++) {
				exp[0] = info[i * 16 + 2];
				exp[1] = info[i * 16 + 1];
				exp[2] = info[i * 16];
				if (memcmp(id, exp, 3))
					continue;
				if (info[i * 16 + 5] == 2) {
					spi_mem_op q = {
						.cmd = { 1, 1, 0x6b },
						.addr = { 3, 1, from },
						.dummy = { info[i * 16 + 6] >> 3, 1 },
						.data = { 4, SPI_MEM_DATA_IN, len },
					};

					if (spi_nor_spimem_exec_op(&q, data) == 0)
						return 0;
					hal_printf_level(LOG_ERR, "quad read faild,use one line read\n");
				}
				break;
			}
		}
	}
	return drv_xspi_flash_read_1_1_1(from, data, len);
}

uint32_t drv_xspi_init(void)
{
	uint32_t info = 0, clk = plat_get_plat_id() == PLAT_FPGA ? 6 : 0, div;
	uint32_t apb = plat_get_soc_apbclk_freq();
	uint32_t a;

	plat_clk_enable_soc_apbclk();
	REG32(0x56000800) |= 0x2000000;
	REG32(0x40000300) |= 1;
	REG32(FCH_CTRL) |= 0xc00;
	REG32(FCH_CTRL + 0x4) |= 0x400;
	REG32(FCH_CTRL + 0x10) |= 3;
	if (fw_efuse_get_info(FUSE_ID_SE_BT_FL_BUS_CLK, &info) == 0 && info < 6 && info != 0)
		clk = info;
	clk = xspi_bus_clk_table[clk];
	if (clk - 1 > 49) {
		hal_printf_level(LOG_ERR, "xSPI clk setting is incorrect, using default 25Mhz\n");
		clk = 25;
	}
	div = apb / clk;
	div = div < 2 ? 1 : div < 18 ? div >> 1 : 8;
	REG32(XSPI_AUX_CLK) = div << 24;
	info = 0;
	REG32(XSPI_AUX_CLK) = (REG32(XSPI_AUX_CLK) & ~1u) |
		(fw_efuse_get_info(FUSE_ID_SE_BT_FL_SPI_MODE, &info) == 0 && info == 1);
	hal_printf_level(LOG_ERR, "cdns_xspi clock/reset done, clk_setting=0x%x\n",
			 (unsigned)REG32(XSPI_AUX_CLK));
	/* hand the flash pins to the AP side's controller */
	for (a = 0x560070f0; a < 0x56007108; a += 4)
		REG32(a) = (REG32(a) & 0xfffffe7f) | 0x80;
	return 0;
}

/* QSPI pins: the SE's controller or the AP's (FCH) */
uint32_t hal_stor_qspi_user_switch(qspi_user_t user)
{
	uint32_t a;

	if (user > QSPI_USER_AP)
		return 0xffffffff;
	for (a = 0x560070f0; a < 0x56007108; a += 4)
		REG32(a) = (REG32(a) & 0xfffffe7f) | (user == QSPI_USER_AP ? 0x80 : 0);
	return 0;
}
