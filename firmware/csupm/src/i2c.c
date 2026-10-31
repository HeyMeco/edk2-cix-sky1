/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Cadence I2C controllers (registers as in Linux's i2c-cadence.c), polled.
 * A register read is a write of the register number with HOLD kept, then
 * a read after a repeated start.
 */
#include "i2c.h"
#include "lcru.h"

#define CR		0x00
#define SR		0x04
#define ADDR		0x08
#define DATA		0x0c
#define ISR		0x10
#define XFER		0x14
#define TIMEOUT		0x1c
#define IER		0x24
#define GLITCH		0x2c

#define CR_RW		BIT(0)
#define CR_MS		BIT(1)
#define CR_NEA		BIT(2)
#define CR_ACKEN	BIT(3)
#define CR_HOLD		BIT(4)
#define CR_CLRFIFO	BIT(6)
#define SR_RXDV		BIT(5)
#define ISR_COMP	BIT(0)
#define ISR_NACK	BIT(2)
#define ISR_TO		BIT(3)
#define ISR_ARBLOST	BIT(9)
#define ISR_ALL		0x2ffu
#define ISR_ERR		(ISR_NACK | ISR_TO | ISR_ARBLOST | 0xe0u)
#define FIFO		16

#define R16(i, r)	REG16(I2C_BASE(i) + (r))

static uint32_t speed_hz[I2C_N];

/* SCL = clock / (22 * (divA + 1) * (divB + 1)); CIX keeps divA 0 when the
 * divider fits in divB's 6 bits, else divA 3. */
static uint32_t divider(uint32_t hz)
{
	uint32_t d = I2C_CLK_HZ / (22 * hz) - 1;

	return d >> 6 ? (d >> 2) | 0xc0 : d;
}

static void master_init(unsigned i)
{
	R16(i, GLITCH) = 5;
	R16(i, CR) = (uint16_t)(divider(speed_hz[i]) << 8 | (R16(i, CR) & 0xff));
	R16(i, CR) |= CR_MS;
	R16(i, CR) |= CR_NEA;
	R16(i, CR) |= CR_ACKEN;
	REG8(I2C_BASE(i) + TIMEOUT) = 0xff;
}

static void slave_init(unsigned i)
{
	R16(i, CR) = (uint16_t)(divider(speed_hz[i]) << 8 | (R16(i, CR) & 0xff));
	R16(i, CR) &= (uint16_t)~CR_MS;
	R16(i, CR) |= CR_NEA;
	R16(i, CR) |= CR_ACKEN;
	R16(i, ADDR) = (R16(i, ADDR) & 0xff80) + 4;	/* slave address */
	REG8(I2C_BASE(i) + TIMEOUT) = 0xff;
	R16(i, ISR) = R16(i, ISR) & ISR_ALL;
	R16(i, CR) |= CR_CLRFIFO;
	R16(i, ISR) = ISR_ALL;
	R16(i, IER) = 0xe7;				/* data, NACK, TO, slave ready, FIFO errors */
}

void i2c_setup(uint32_t hz, uint32_t masters, uint32_t slaves)
{
	lcru_reset(0xf, false);
	udelay_cycles(10);
	for (unsigned i = 0; i < I2C_N; i++) {
		lcru_reset(RESET_I2C(i), true);
		speed_hz[i] = hz;
		if (masters & BIT(i))
			master_init(i);
		if (slaves & ~masters & BIT(i))
			slave_init(i);
	}
}

/* Wait for the transfer to complete; 600 ms as CIX allows */
static int wait(unsigned i, uint32_t start)
{
	uint32_t isr;

	for (;;) {
		isr = R16(i, ISR) & ISR_ALL;
		if (isr & ISR_ERR)
			break;
		if (isr & ISR_COMP && !(REG8(I2C_BASE(i) + XFER)))
			break;
		if (ccount() - start > core_hz / 1000000 * 600000)
			return -2;
	}
	R16(i, ISR) = (uint16_t)isr;
	return isr & ISR_ERR ? -1 : 0;
}

static void start(unsigned i, bool read, unsigned n)
{
	R16(i, CR) |= CR_HOLD;
	R16(i, CR) |= CR_CLRFIFO;
	if (read)
		R16(i, CR) |= CR_RW;
	else
		R16(i, CR) &= (uint16_t)~CR_RW;
	R16(i, ISR) = R16(i, ISR) & ISR_ALL;
	REG8(I2C_BASE(i) + XFER) = (uint8_t)(n < FIFO ? n : FIFO);
}

static void hold_off(unsigned i)
{
	R16(i, CR) &= (uint16_t)~CR_HOLD;
}

static int send(unsigned i, uint8_t addr, const uint8_t *buf, unsigned n)
{
	start(i, false, n);
	for (unsigned k = 0; k < n && k < FIFO - 2; k++)
		R16(i, DATA) = (R16(i, DATA) & 0xff00) + buf[k];
	R16(i, ADDR) = (R16(i, ADDR) & 0xff80) + (addr & 0x7f);
	return wait(i, ccount());
}

int i2c_write(unsigned bus, uint8_t addr, const uint8_t *buf, unsigned n)
{
	int err = send(bus, addr, buf, n);

	hold_off(bus);
	return err;
}

int i2c_write_read(unsigned bus, uint8_t addr, const uint8_t *wbuf, unsigned wn,
		   uint8_t *rbuf, unsigned rn)
{
	int err = send(bus, addr, wbuf, wn);

	if (!err) {
		start(bus, true, rn);
		R16(bus, ADDR) = (R16(bus, ADDR) & 0xff80) + (addr & 0x7f);
		err = wait(bus, ccount());
		for (unsigned k = 0; !err && k < rn; k++)
			rbuf[k] = (R16(bus, SR) & SR_RXDV) ? (uint8_t)R16(bus, DATA) : 0;
	}
	hold_off(bus);
	return err;
}
