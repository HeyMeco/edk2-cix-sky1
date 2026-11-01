/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Cadence I2C controllers (registers as in Linux's i2c-cadence.c): CIX's
 * dev_i2c.c and md_i2c.c.
 *
 * The masters move bytes with CIX's register sequence (i2c_controller_
 * write_polled, i2c_controller_read_polled, clear_hold; dev_i2c_master_
 * init, dev_i2c_set_speed): a register read is a write of the register
 * number with HOLD kept, then a read after a repeated start.  md_i2c_mTx,
 * md_i2c_mTx_Sr_mRx and md_i2c_mTx_mTx are i2c_write, i2c_write_read and
 * i2c_write_write.
 *
 * How a transfer waits depends on who asks:
 *  - before the scheduler runs, during the power sequence (rtos_async()
 *    false) and from the SCP-firmware task: polled, as CIX's driver;
 *  - from any other task (the I2C workers of src/rtos.c, which do all of
 *    the PMIC and board-sensor traffic at run time): the controller's
 *    completion and error interrupts (aggregator line 24), the task asleep
 *    in between, the controller held by a mutex for the transfer (CIX's
 *    md_i2c.c port lock).  The rest of the PM runs meanwhile.
 *
 * Either way a transfer gives up after I2C_TIMEOUT_US (CIX allows 600 ms;
 * one of the PMICs' transfers takes about 100 us at 400 kHz).  A time-out
 * or lost arbitration resets the controller and sets it up again (the
 * bus recovery CIX's master has not got).  After BUS_FAILS_DOWN such
 * failures in a row the bus is down: its transfers fail at once (no
 * waiting) for a back-off that doubles from 100 ms to 2 s, then one is
 * tried again.  NACKs are a device's answer, not the bus's: they only
 * fail the transfer (the PMIC probe at boot expects them).
 *
 * Controller 3 is a slave (dev_i2c_slave_init, address 4) unless a board
 * sensor needs it as a master: the EC reads the PM's registers through it
 * (src/board/i2c_slave.c); its interrupt and error recovery are below.
 */
#include <mod_pm_svc.h>

#include "board/board.h"
#include "i2c.h"
#include "irq.h"
#include "lcru.h"
#include "rtos.h"
#include "clk/clk.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

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
#define ISR_BUS_ERR	(ISR_TO | ISR_ARBLOST)
#define MASTER_IRQS	(ISR_COMP | ISR_ERR)
#define FIFO		16
#define IDR		0x28
#define I2C_IRQ_LINE	24

#define I2C_TIMEOUT_US		10000u
#define BUS_FAILS_DOWN		3
#define BUS_DOWN_MIN_US		100000u
#define BUS_DOWN_MAX_US		2000000u


#define R16(i, r)	REG16(I2C_BASE(i) + (r))

static uint32_t speed_hz[I2C_N];
static uint32_t i2c_masters;

/* a master's state at run time */
static struct i2c_bus {
	TaskHandle_t waiter;		/* asleep until the interrupt */
	uint16_t isr_done;		/* ISR when the interrupt ended it */
	uint8_t fails;			/* time-outs and lost arbitrations in a row */
	bool down;
	uint32_t down_us;		/* this back-off */
	uint64_t down_until;
	SemaphoreHandle_t lock;
	StaticSemaphore_t lock_buf;
} bus_state[I2C_N];

/* what each bus went through (a global: the tests read it) */
struct i2c_bus_stats i2c_stats[I2C_N];

/* SCL = clock / (22 * (divA + 1) * (divB + 1)); CIX keeps divA 0 when the
 * divider fits in divB's 6 bits, else divA 3. */
static uint32_t divider(uint32_t hz)
{
	uint32_t d = I2C_CLK_HZ / (22 * hz) - 1;

	return d >> 6 ? (d >> 2) | 0xc0 : d;
}

/* CIX's dev_i2c_master_init */
static void master_init(unsigned i)
{
	R16(i, GLITCH) = 5;
	R16(i, CR) = (uint16_t)(divider(speed_hz[i]) << 8 | (R16(i, CR) & 0xff));
	R16(i, CR) |= CR_MS;
	R16(i, CR) |= CR_NEA;
	R16(i, CR) |= CR_ACKEN;
	REG8(I2C_BASE(i) + TIMEOUT) = 0xff;
}

/* CIX's dev_i2c_slave_init */
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

/* CIX's app_i2c_setup: dev_i2c_init (all in reset), then each controller
 * out of reset and set up */
void i2c_setup(uint32_t hz, uint32_t masters, uint32_t slaves)
{
	i2c_masters = masters;
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

static bool done(uint16_t isr, bool read, unsigned i)
{
	return (isr & ISR_ERR) || ((isr & ISR_COMP) && !(read && REG8(I2C_BASE(i) + XFER)));
}

/* Wait for the transfer to complete (for a read, every byte received),
 * polling */
static int wait_polled(unsigned i, bool read, uint32_t start)
{
	uint32_t isr;

	for (;;) {
		isr = R16(i, ISR) & ISR_ALL;
		if (done((uint16_t)isr, read, i))
			break;
		if (ccount() - start > core_hz / 1000000 * I2C_TIMEOUT_US)
			return I2C_E_TIMEOUT;
	}
	R16(i, ISR) = (uint16_t)isr;
	return isr & ISR_BUS_ERR ? I2C_E_TIMEOUT : isr & ISR_ERR ? -1 : 0;
}

/* the same, asleep until the controller's interrupt (master_isr) */
static int wait_irq(unsigned i, bool read)
{
	struct i2c_bus *b = &bus_state[i];
	uint32_t got, ps;
	uint16_t isr;

	port_tick_hold();
	got = ulTaskNotifyTakeIndexed(1, pdTRUE, pdMS_TO_TICKS(I2C_TIMEOUT_US / 1000) + 1);
	port_tick_release();
	ps = port_irq_disable();
	R16(i, IDR) = MASTER_IRQS;
	b->waiter = NULL;
	isr = b->isr_done;
	port_irq_restore(ps);
	if (!got) {
		/* finished as the time ran out, its interrupt not served yet? */
		isr = R16(i, ISR) & ISR_ALL;
		if (!done(isr, read, i))
			return I2C_E_TIMEOUT;
		R16(i, ISR) = isr;
	}
	return isr & ISR_BUS_ERR ? I2C_E_TIMEOUT : isr & ISR_ERR ? -1 : 0;
}

/* the controller's line 24 source, for a master (from dev_i2c_isr) */
static void master_isr(unsigned i)
{
	struct i2c_bus *b = &bus_state[i];
	uint16_t isr = R16(i, ISR) & ISR_ALL;

	if (!done(isr, R16(i, CR) & CR_RW, i)) {
		R16(i, ISR) = isr;
		return;
	}
	R16(i, IDR) = MASTER_IRQS;
	R16(i, ISR) = isr;
	b->isr_done = isr;
	if (b->waiter)
		(void)xTaskNotifyGiveIndexed(b->waiter, 1);
}

static bool irq_mode(void)
{
	return rtos_async() && !rtos_in_scp_task();
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

/* ADDR written: the transfer starts; then wait for it */
static int go(unsigned i, uint8_t addr, bool read)
{
	struct i2c_bus *b = &bus_state[i];
	bool irq = irq_mode();

	if (irq) {
		b->isr_done = 0;
		b->waiter = xTaskGetCurrentTaskHandle();
		(void)xTaskNotifyStateClearIndexed(NULL, 1);
		(void)ulTaskNotifyValueClearIndexed(NULL, 1, ~0u);
		R16(i, IER) = MASTER_IRQS;
	}
	R16(i, ADDR) = (R16(i, ADDR) & 0xff80) + (addr & 0x7f);
	return irq ? wait_irq(i, read) : wait_polled(i, read, ccount());
}

/* CIX's clear_hold */
static void hold_off(unsigned i)
{
	R16(i, CR) &= (uint16_t)~CR_HOLD;
}

/* CIX's i2c_controller_write_polled, the stop left to the caller */
static int send(unsigned i, uint8_t addr, const uint8_t *buf, unsigned n)
{
	start(i, false, n);
	for (unsigned k = 0; k < n && k < FIFO - 2; k++)
		R16(i, DATA) = (R16(i, DATA) & 0xff00) + buf[k];
	return go(i, addr, false);
}

/*
 * A time-out or lost arbitration: the controller back to how i2c_setup
 * left it (Linux's cdns_i2c_master_reset, then the controller in reset
 * and out again), and the bus's record of failures.
 */
static void master_recover(unsigned i)
{
	R16(i, IDR) = 0xffff;
	R16(i, CR) = (uint16_t)((R16(i, CR) & ~CR_HOLD) | CR_CLRFIFO);
	REG8(I2C_BASE(i) + XFER) = 0;
	R16(i, ISR) = R16(i, ISR) & ISR_ALL;
	lcru_reset(RESET_I2C(i), false);
	udelay_cycles(10);
	lcru_reset(RESET_I2C(i), true);
	master_init(i);
	i2c_stats[i].recoveries++;
}

/* before a transfer: false while the bus is down */
static bool bus_usable(unsigned i)
{
	struct i2c_bus *b = &bus_state[i];

	if (!b->down)
		return true;
	if (arch_getTimeNowUs() < b->down_until) {
		i2c_stats[i].fast_fails++;
		return false;
	}
	return true;			/* one try; a failure doubles the back-off */
}

static void bus_result(unsigned i, int err)
{
	struct i2c_bus *b = &bus_state[i];

	if (err != I2C_E_TIMEOUT) {
		if (b->down)
			i2c_stats[i].ups++;
		b->fails = 0;
		b->down = false;
		b->down_us = 0;
		return;
	}
	i2c_stats[i].timeouts++;
	master_recover(i);
	if (b->down) {
		b->down_us = b->down_us * 2 > BUS_DOWN_MAX_US ? BUS_DOWN_MAX_US : b->down_us * 2;
	} else if (++b->fails >= BUS_FAILS_DOWN) {
		b->down = true;
		b->down_us = BUS_DOWN_MIN_US;
		i2c_stats[i].downs++;
	} else {
		return;
	}
	b->down_until = arch_getTimeNowUs() + b->down_us;
}

/* the controller held for a transfer (from a task other than the SCP one) */
static bool lock(unsigned i)
{
	if (!rtos_async() || !bus_state[i].lock)
		return false;
	(void)xSemaphoreTake(bus_state[i].lock, portMAX_DELAY);
	return true;
}

static void unlock(unsigned i, bool locked)
{
	if (locked)
		(void)xSemaphoreGive(bus_state[i].lock);
}

/* CIX's md_i2c_mTx */
int i2c_write(unsigned bus, uint8_t addr, const uint8_t *buf, unsigned n)
{
	bool locked;
	int err;

	if (bus >= I2C_N || !bus_usable(bus))
		return I2C_E_BUS_DOWN;
	locked = lock(bus);
	err = send(bus, addr, buf, n);
	hold_off(bus);
	bus_result(bus, err);
	unlock(bus, locked);
	return err;
}

/* CIX's md_i2c_mTx_mTx: two writes, each ended with a stop (its polled
 * path calls i2c_controller_write_polled twice with stop set) */
int i2c_write_write(unsigned bus, uint8_t addr, const uint8_t *buf1, unsigned n1,
		    const uint8_t *buf2, unsigned n2)
{
	int err = i2c_write(bus, addr, buf1, n1);

	return err ? err : i2c_write(bus, addr, buf2, n2);
}

/* CIX's md_i2c_mTx_Sr_mRx: the write, then i2c_controller_read_polled */
int i2c_write_read(unsigned bus, uint8_t addr, const uint8_t *wbuf, unsigned wn,
		   uint8_t *rbuf, unsigned rn)
{
	bool locked;
	int err;

	if (bus >= I2C_N || !bus_usable(bus))
		return I2C_E_BUS_DOWN;
	locked = lock(bus);
	err = send(bus, addr, wbuf, wn);
	if (!err) {
		start(bus, true, rn);
		err = go(bus, addr, true);
		for (unsigned k = 0; !err && k < rn; k++)
			rbuf[k] = (R16(bus, SR) & SR_RXDV) ? (uint8_t)R16(bus, DATA) : 0;
	}
	hold_off(bus);
	bus_result(bus, err);
	unlock(bus, locked);
	return err;
}

/* the masters' interrupt sources on, their locks (src/rtos.c, before the
 * scheduler starts) */
void i2c_rtos_init(void)
{
	for (unsigned i = 0; i < I2C_N; i++)
		bus_state[i].lock = xSemaphoreCreateMutexStatic(&bus_state[i].lock_buf);
	REG32(AGG_ENABLE(I2C_IRQ_LINE)) |= i2c_masters;
}

const struct i2c_bus_stats *i2c_bus_stats(unsigned bus)
{
	return bus < I2C_N ? &i2c_stats[bus] : NULL;
}

/* --- the slave and its recovery (CIX's dev_i2c_isr, _i2c_sw_reset,
 * _i2c_postponed_recovery_event; i2c_slave_mode.c has the registers) ------- */

#define SR_RXRW		BIT(3)		/* the master reads */
#define SR_BA		BIT(8)		/* bus active */
#define ISR_DATA	BIT(1)
#define ISR_SLV_ERR	0x2ecu		/* NACK, TO, FIFO over/underflows, arbitration */

static uint8_t slave_state[I2C_N], slave_idx[I2C_N];
static uint16_t slave_reg;		/* CIX's: one register pointer for all */

/* CIX's _i2c_sw_reset: controllers in reset (assert) or out of it */
static void i2c_sw_reset(uint32_t ch_mask, bool to_assert)
{
	if (to_assert)
		REG32(LCRU_RESET) &= ~(ch_mask & 0xfu);
	else
		REG32(LCRU_RESET) |= ch_mask & 0xfu;
}

static void i2c_recovery(void *arg);

/*
 * CIX's _i2c_postponed_recovery_event: a slave that had an error out of
 * reset, its interrupts off and FIFO cleared, set up again; while the bus
 * is still busy it goes back into reset and is tried again later, else
 * its interrupt is on again.
 */
static void i2c_recovery(void *arg)
{
	uint32_t bits = (uint32_t)(uintptr_t)arg & 0xf;

	for (unsigned i = 0; i < I2C_N; i++) {
		if (!(bits & BIT(i)) || (i2c_masters & BIT(i)))
			continue;
		i2c_sw_reset(BIT(i), false);
		udelay_cycles(10);
		R16(i, IDR) = 0xffff;
		R16(i, CR) |= CR_CLRFIFO;
		slave_state[i] = 0;
		slave_idx[i] = 0;
		slave_init(i);
		udelay_cycles(30);
		if (R16(i, SR) & SR_BA) {
			i2c_sw_reset(BIT(i), true);
			(void)pm_svc_job_append(i2c_recovery, (void *)(uintptr_t)BIT(i));
		} else {
			REG32(AGG_ENABLE(I2C_IRQ_LINE)) |= BIT(i);
		}
	}
}

/*
 * CIX's dev_i2c_isr for a slave (the EC as master: CIX's i2c_slave_mode.c).
 * A write's first two bytes select a register (CUS_REG), the next two are
 * written to it; a read gets the register's two bytes.  An error puts the
 * controller in reset and its interrupt off until the recovery.
 */
static void i2c_slave_isr(unsigned ch)
{
	uint16_t isr = R16(ch, ISR) & ISR_ALL;

	R16(ch, ISR) = isr;
	if (!isr)
		return;
	if (isr & ISR_SLV_ERR) {
		REG32(AGG_ENABLE(I2C_IRQ_LINE)) &= ~BIT(ch);
		i2c_sw_reset(BIT(ch), true);
		(void)pm_svc_job_append(i2c_recovery, (void *)(uintptr_t)BIT(ch));
		return;
	}
	if (R16(ch, SR) & SR_RXRW) {
		slave_state[ch] = 2;
		REG8(I2C_BASE(ch) + XFER) = 2;
		if ((isr & ISR_DATA) && slave_reg < CUS_REG_MAX) {
			for (int n = 14; n && slave_idx[ch] < slave_state[ch]; n--)
				R16(ch, DATA) = cus_dev_reg_get(slave_reg, slave_idx[ch]++);
		}
	} else {
		slave_state[ch] = REG8(I2C_BASE(ch) + XFER);
		while (R16(ch, SR) & SR_RXDV) {
			uint8_t b = (uint8_t)R16(ch, DATA);

			if (slave_idx[ch] == 0)
				slave_reg = b;
			else if (slave_idx[ch] == 1)
				slave_reg = (uint16_t)(slave_reg << 8 | b);
			else if (slave_idx[ch] < 4 && slave_reg < CUS_REG_MAX)
				DEV_REG[slave_reg][slave_idx[ch] - 2] = b;
			slave_idx[ch]++;
		}
	}
	if (isr & ISR_COMP)
		slave_idx[ch] = 0;
}

/* CIX's dev_i2c_isr (aggregator line 24, a source per controller): a
 * master's transfer done (master_isr) or the slave's */
void dev_i2c_isr(void)
{
	uint32_t pending = REG32(AGG_STATUS(I2C_IRQ_LINE)) & REG32(AGG_ENABLE(I2C_IRQ_LINE));

	for (unsigned i = 0; i < I2C_N; i++) {
		if (!(pending & BIT(i)))
			continue;
		if (i2c_masters & BIT(i))
			master_isr(i);
		else
			i2c_slave_isr(i);
	}
}
