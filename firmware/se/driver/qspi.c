/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * SE QSPI controller and the SPI NOR on it (CIX driver/qspi/drv_qspi.c).
 *
 * Indirect mode: program the command (CCR), address and length, start,
 * then move data through the FIFO.  Commands come from qspi_cmd_list,
 * adjusted to the eFuse flash settings (bus clock, SPI mode, RX delay,
 * 4-byte addressing) before each use.
 */
#include "se.h"
#include "efuse.h"
#include "platform.h"
#include "qspi.h"

#define QSPI_CR		(SE_QSPI_BASE + 0x00)	/* [29:24] prescaler, [4] mode 3, [1] start */
#define QSPI_SR		(SE_QSPI_BASE + 0x04)	/* [15:12] FIFO, [8] flush, [4] done, [2] busy */
#define QSPI_CCR	(SE_QSPI_BASE + 0x0c)
#define QSPI_ABR	(SE_QSPI_BASE + 0x14)
#define QSPI_AR		(SE_QSPI_BASE + 0x18)
#define QSPI_DR		(SE_QSPI_BASE + 0x1c)
#define QSPI_DLR	(SE_QSPI_BASE + 0x20)

#define SR_OWNER_AP	2		/* SR[1:0]: the AP owns the controller */
#define SR_BUSY		BIT(2)
#define SR_DONE		BIT(4)
#define SR_FLUSH	BIT(8)
#define SR_FIFO(sr)	(((sr) >> 12) & 0xf)

#define QSPI_POLL	0xffff
#define CHUNK_2M	0x200000

typedef struct {
	uint8_t sckscaler, ddr_mode, bit_30, bit_29, numdc, rxdly, data_size, data_mode;
	uint8_t alt_size, alt_mode, addr_size, addr_mode;
	uint32_t alt_byte;
	uint8_t inst_mode, inst, mode;
} drv_qspi_t;

enum { CMD_READ, CMD_READ_DUAL, CMD_READ_QUAD, CMD_JEDEC_ID, CMD_WREN, CMD_ERASE_4K,
       CMD_PROGRAM, CMD_WRSR, CMD_RDSR, CMD_SEC_PA };

#define CMD(i, dm, as, am, dc, b30) { .sckscaler = 4, .inst_mode = 1, .inst = (i), \
	.data_mode = (dm), .addr_size = (as), .addr_mode = (am), .numdc = (dc), .bit_30 = (b30) }

static drv_qspi_t qspi_cmd_list[10] = {
	[CMD_READ]	= CMD(0x03, 1, 2, 1, 0, 0),
	[CMD_READ_DUAL]	= CMD(0x3b, 2, 2, 1, 8, 0),
	[CMD_READ_QUAD]	= CMD(0x6b, 3, 2, 1, 8, 0),
	[CMD_JEDEC_ID]	= CMD(0x9f, 1, 0, 0, 0, 0),
	[CMD_WREN]	= CMD(0x06, 0, 0, 0, 0, 1),
	[CMD_ERASE_4K]	= CMD(0x20, 0, 2, 1, 0, 1),
	[CMD_PROGRAM]	= CMD(0x02, 1, 2, 1, 0, 1),
	[CMD_WRSR]	= CMD(0x01, 1, 0, 0, 0, 1),
	[CMD_RDSR]	= CMD(0x05, 1, 0, 0, 0, 0),
	[CMD_SEC_PA]	= CMD(0xa1, 1, 0, 0, 0, 1),
};

/* MHz by FUSE_ID_SE_BT_FL_BUS_CLK */
const uint8_t qspi_bus_clk_table[6] = { 25, 20, 40, 50, 10, 5 };

static uint8_t qspi_32bit_address;	/* flash > 16 MiB and 4-byte mode fused */
static uint8_t qspi_read_adv;		/* FUSE_ID_SE_BT_FL_READ_ADV */

static uint8_t prescaler(uint32_t mhz)
{
	uint32_t div;

	if (mhz - 1 > 49) {
		hal_printf_level(LOG_ERR, "QSPI clk setting is incorrect, using default 25Mhz\n");
		mhz = 25;
	}
	div = (plat_get_core_freq() / mhz) & 0xff;
	div = MIN(MAX(div, 2), 128);
	return (div >> 1) - 1;
}

static void qspi_get_parameters(drv_qspi_t *q, uint32_t len)
{
	uint32_t info = 0, clk = plat_get_plat_id() == PLAT_FPGA ? 5 : 0;

	if (fw_efuse_get_info(FUSE_ID_SE_BT_FL_BUS_CLK, &info) == 0 && info < 6 && info != 0)
		clk = info;
	q->sckscaler = prescaler(qspi_bus_clk_table[clk]);
	info = 0;
	q->mode = fw_efuse_get_info(FUSE_ID_SE_BT_FL_SPI_MODE, &info) == 0 && info == 1;
	info = 0;
	q->rxdly = fw_efuse_get_info(FUSE_ID_SE_BT_FL_RX_DELAY, &info) == 0 && info < 4 ? info : 0;
	if (qspi_32bit_address) {
		if (q->inst == 0x03)
			q->inst = 0x13;
		else if (q->inst == 0x3b)
			q->inst = 0x3c;
		else if (q->inst == 0x6b)
			q->inst = 0x6c;
		q->addr_size = 3;
	}
	if (q->bit_30)
		q->data_size = 0;
	else if ((len & 3) == 0)
		q->data_size = 3;
	else
		q->data_size = (len & 1) == 0;
}

static uint32_t ccr(const drv_qspi_t *q)
{
	return (uint32_t)q->ddr_mode << 31 | (uint32_t)q->numdc << 24 | q->rxdly << 22 |
	       q->data_size << 20 | q->data_mode << 18 | q->alt_size << 16 | q->alt_mode << 14 |
	       q->addr_size << 12 | q->addr_mode << 10 | q->inst_mode << 8 | q->inst;
}

static void qspi_start(const drv_qspi_t *q)
{
	REG32(QSPI_SR) |= SR_FLUSH;
	REG32(QSPI_CR) = (REG32(QSPI_CR) & 0xc0ffffec) | (uint32_t)q->sckscaler << 24 | q->mode << 4 | 2;
}

static uint32_t wait_sr(uint32_t bit)
{
	uint32_t i;

	for (i = 0; !(REG32(QSPI_SR) & bit); i++)
		if (i >= QSPI_POLL - 1)
			return 0x9902;
	return 0;
}

static void qspi_clear_tcf(void)
{
	REG32(QSPI_SR) |= SR_DONE;
}

/* One read command of len bytes (FIFO accesses of 1, 2 or 4 bytes) */
static uint32_t __drv_qspi_flash_read1(drv_qspi_t *q, uint32_t addr, uint8_t *data, uint32_t len)
{
	uint32_t idle = 0, step;

	REG32(QSPI_CCR) = ccr(q);
	REG32(QSPI_AR) = addr;
	REG32(QSPI_DLR) = len - 1;
	qspi_start(q);
	if (wait_sr(SR_BUSY))
		return 0x9902;
	step = q->data_size == 3 ? 4 : q->data_size == 1 ? 2 : q->data_size == 0 ? 1 : 0;
	if (step == 0)
		return 0x9903;
	while (len) {
		if (!SR_FIFO(REG32(QSPI_SR))) {
			/* with READ_ADV the controller is fast enough to wait forever */
			if (!qspi_read_adv && ++idle >= 1000)
				return 0x9902;
			continue;
		}
		idle = 0;
		if (step == 4)
			*(uint32_t *)data = REG32(QSPI_DR);
		else if (step == 2)
			*(uint16_t *)data = REG32(QSPI_DR);
		else
			*data = REG32(QSPI_DR);
		data += step;
		len -= step;
	}
	if (wait_sr(SR_DONE))
		return 0x9902;
	qspi_clear_tcf();
	return 0;
}

/* A command that writes (opcode, address, then len bytes) */
static uint32_t qspi_write(drv_qspi_t *q, uint32_t addr, const uint8_t *data, uint32_t len)
{
	drv_qspi_t cmd = *q;
	uint32_t idle = 0;

	qspi_get_parameters(q, len);
	REG32(QSPI_CCR) = ccr(&cmd) | (uint32_t)cmd.bit_30 << 30;
	REG32(QSPI_ABR) = 0;
	REG32(QSPI_AR) = addr;
	REG32(QSPI_DLR) = cmd.data_mode && len ? len - 1 : 0;
	if ((REG32(QSPI_SR) & 3) == SR_OWNER_AP)
		return 0x9903;
	qspi_start(q);
	if (wait_sr(SR_BUSY))
		return 0x9902;
	if (data == NULL || len == 0)
		return 0;
	if (q->data_size != 0)
		return 0x9903;
	while (len && idle < 1000) {
		if (SR_FIFO(REG32(QSPI_SR)) == 1) {
			idle++;
			continue;
		}
		REG32(QSPI_DR) = *data++;
		len--;
		idle = 0;
	}
	if (idle)
		return 0x9902;
	if (wait_sr(SR_DONE))
		return 0x9902;
	qspi_clear_tcf();
	return 0;
}

/* Reads do not cross a 2 MiB boundary from 2 MiB on */
static uint32_t __drv_qspi_flash_read(drv_qspi_t *q, uint32_t addr, uint8_t *data, uint32_t len)
{
	uint32_t end = addr + len, next = (addr / CHUNK_2M + 1) * CHUNK_2M, err = 0, n;

	if (addr < CHUNK_2M && end <= CHUNK_2M)
		return __drv_qspi_flash_read1(q, addr, data, len);
	if (addr >= CHUNK_2M && end <= next)
		return __drv_qspi_flash_read1(q, addr, data, len);
	while (len) {
		n = MIN(len, next - addr);
		err += __drv_qspi_flash_read1(q, addr, data, n);
		addr += n;
		data += n;
		len -= n;
		next += CHUNK_2M;
	}
	return err;
}

static void set_data_size(drv_qspi_t *q, uint32_t len)
{
	q->data_size = (len & 3) == 0 ? 3 : (len & 1) == 0;
}

/* In commands of up to 1 MiB */
static uint32_t qspi_flash_full_read(drv_qspi_t *q, uint32_t addr, uint8_t *data, uint32_t len)
{
	uint32_t err = 0, n;

	while (len) {
		n = MIN(len, 0x100000);
		if (n == 0x100000)
			q->data_size = 3;
		else
			set_data_size(q, n);
		err += __drv_qspi_flash_read(q, addr, data, n);
		addr += n;
		data += n;
		len -= n;
	}
	return err;
}

uint32_t drv_qspi_flash_read(qspi_cmd_t cmd, uint32_t addr, void *data, uint32_t len)
{
	drv_qspi_t *q = &qspi_cmd_list[cmd];

	if ((REG32(QSPI_SR) & 3) == SR_OWNER_AP || len == 0)
		return 0x9903;
	qspi_get_parameters(q, len);
	return qspi_flash_full_read(q, addr, data, len);
}

/* Read with the boot ROM's per-flash parameters (command, clock, dummy cycles, delay) */
uint32_t drv_qspi_flash_fast_read(const qspi_param_t *p, uint32_t addr, void *data, uint32_t len)
{
	drv_qspi_t *q;

	if (p->read_cmd >= 3) {
		hal_printf_level(LOG_ERR, "parameter read_cmd is incorrect, using default read 1-1-1\n");
	} else if (p->qspi_bus_clk >= 6) {
		hal_printf_level(LOG_ERR, "parameter qspi_bus_clk is incorrect, using default clock setting\n");
	} else if (p->qspi_numdc >= 32) {
		hal_printf_level(LOG_ERR, "parameter qspi_numdc is incorrect, using default read 1-1-1\n");
	} else if (p->qspi_rxdly >= 4) {
		hal_printf_level(LOG_ERR, "parameter qspi_rxdly is incorrect, using default read 1-1-1\n");
	} else {
		if (len & 3)
			hal_printf_level(LOG_WARN, "It would be best if the read length should be multiple of 4!\n");
		q = &qspi_cmd_list[p->read_cmd];
		qspi_get_parameters(q, len);
		q->sckscaler = prescaler(qspi_bus_clk_table[p->qspi_bus_clk]);
		q->numdc = p->qspi_numdc;
		q->rxdly = p->qspi_rxdly;
		return qspi_flash_full_read(q, addr, data, len);
	}
	return drv_qspi_flash_read(QSPI_READ_1_1_1, addr, data, len);
}

uint32_t drv_qspi_read_sr(uint8_t *sr)
{
	drv_qspi_t *q = &qspi_cmd_list[CMD_RDSR];

	qspi_get_parameters(q, 1);
	return __drv_qspi_flash_read(q, 0, sr, 1);
}

static uint32_t drv_qspi_wren(void)
{
	uint8_t sr = 0;
	uint32_t err = qspi_write(&qspi_cmd_list[CMD_WREN], 0, NULL, 0);

	if (err) {
		hal_printf_level(LOG_ERR, "write enable timeout  0x%x\n", (unsigned)err);
		return err;
	}
	drv_qspi_read_sr(&sr);
	if (!(sr & 2)) {
		hal_printf_level(LOG_ERR, "write enable error,sr=0x%x\n", sr);
		return 0x9900;
	}
	return 0;
}

static uint32_t drv_qspi_waitbusy(void)
{
	uint8_t sr = 0;
	uint32_t i = 0;

	do {
		i++;
		if (drv_qspi_read_sr(&sr)) {
			hal_printf_level(LOG_INFO, "%s error\n", __func__);
			return 0x9900;
		}
	} while ((sr & 1) && i < 0xfffffff);
	return i == 0xfffffff ? 0x9902 : 0;
}

static uint32_t write_page(uint32_t addr, const uint8_t *data, uint32_t len)
{
	if (drv_qspi_wren() || qspi_write(&qspi_cmd_list[CMD_PROGRAM], addr, data, len) ||
	    drv_qspi_waitbusy()) {
		hal_printf_level(LOG_INFO, "%s error\n", "drv_qspi_flash_write_prepare");
		return 0x9900;
	}
	return 0;
}

/*
 * Erase 4 KiB sectors.  Each erase needs its own write enable (CIX sends
 * one for the whole range; it only ever erases one sector at a time).
 */
uint32_t drv_qspi_flash_erase(uint32_t addr, uint32_t len)
{
	uint32_t end = addr + len;

	if (len & 0xfff) {
		hal_printf_level(LOG_ERR, "len must multi 4K Byte\n");
		return 0x9904;
	}
	for (; addr != end; addr += 0x1000) {
		if (drv_qspi_wren() || qspi_write(&qspi_cmd_list[CMD_ERASE_4K], addr, NULL, 0) ||
		    drv_qspi_waitbusy()) {
			hal_printf_level(LOG_INFO, "%s error\n", __func__);
			return 0x9900;
		}
	}
	return 0;
}

uint32_t drv_qspi_flash_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
	uint32_t n;

	while (len) {
		n = MIN(len, 0x100);
		if (write_page(addr, data, n)) {
			hal_printf_level(LOG_INFO, "%s error\n", __func__);
			return 0x9900;
		}
		addr += n;
		data += n;
		len -= n;
	}
	return 0;
}

/* Winbond W25Q128JW (EF 8A 17... ID 0x8a): security register protection */
uint32_t drv_qspi_init_sec_pa(uint8_t sec_no)
{
	uint8_t data[4] = { 0x47, sec_no, 0, 0 };

	if (drv_qspi_wren() || qspi_write(&qspi_cmd_list[CMD_SEC_PA], 0, data, 4)) {
		hal_printf_level(LOG_INFO, "%s error\n", __func__);
		return 0x9900;
	}
	return 0;
}

uint32_t drv_qspi_read_jedec_id(uint8_t *id)
{
	uint32_t info = 0, err;

	qspi_read_adv = fw_efuse_get_info(FUSE_ID_SE_BT_FL_READ_ADV, &info) == 0 && info == 1;
	err = drv_qspi_flash_read(QSPI_READ_JEDEC_ID, 0, id, 3);
	if (err)
		return err;
	info = 0;
	qspi_32bit_address = id[2] > 0x18 &&
		fw_efuse_get_info(FUSE_ID_SE_BT_FL_ADDR_MODE, &info) == 0 && info == 1;
	return 0;
}
