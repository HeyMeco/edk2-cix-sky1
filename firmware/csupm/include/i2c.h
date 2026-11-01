/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The PM's I2C controllers: Cadence I2C (cdns,i2c-r1p14, 16-bit registers)
 * at 0xa0010000 + 0x1000 * i.  On the boards CIX supports, 0-2 are masters
 * (each with an MP5475 PMIC at 0x60) and 3 is a slave.
 */
#ifndef I2C_H
#define I2C_H

#include "pm.h"

#define I2C_BASE(i)		(0xa0010000u + 0x1000u * (i))
#define I2C_N			4
#define I2C_CLK_HZ		100000000u

void i2c_setup(uint32_t hz, uint32_t masters, uint32_t slaves);
/* CIX's dev_i2c_isr (aggregator line 24): the slave's interrupt */
void dev_i2c_isr(void);
int i2c_write(unsigned bus, uint8_t addr, const uint8_t *buf, unsigned n);
/* CIX's md_i2c_mTx_mTx: two writes */
int i2c_write_write(unsigned bus, uint8_t addr, const uint8_t *buf1, unsigned n1,
		    const uint8_t *buf2, unsigned n2);
int i2c_write_read(unsigned bus, uint8_t addr, const uint8_t *wbuf, unsigned wn,
		   uint8_t *rbuf, unsigned rn);

#endif
