/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* The debug UART: a PL011 at 0xa0030000 (UART4 on the Orion O6) */
#ifndef UART_H
#define UART_H

#include "pm.h"

#define UART_BASE		0xa0030000u
#define UART_CLK_HZ		100000000u
#define UART_BAUD		115200u

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);

#endif
