/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Small SE drivers (CIX driver/{uart,timer,wdt,postcode,rom,syscnt,syshub}) */
#ifndef DRIVERS_H
#define DRIVERS_H

#include <stdint.h>

/* PL011 UART */
uint32_t drv_uart_init(uint32_t baud_rate, uint32_t input_clock);
uint32_t drv_pl011_putchar(char ch);
char drv_pl011_getchar(void);
int8_t drv_pl011_getchar_poll(uint32_t wait);

/* Free-running timers 0 and 1, 24 MHz */
void drv_timer_init(void);
uint64_t drv_timer_get_current_count(void);

/* SE watchdog */
uint32_t drv_wdt_init(uint32_t ms, uint32_t reset_enable);
uint32_t drv_wdt_deinit(void);
uint32_t drv_wdt_start(void);
uint32_t drv_wdt_stop(void);
uint32_t drv_wdt_feed(void);
uint32_t drv_wdt_cur_count(void);
void drv_wdt_reset_sys(void) __attribute__((noreturn));

uint32_t drv_pc_trace(uint16_t code);
uint32_t drv_rom_lock(void);

/* System counter (generic timer reference for the AP) */
void drv_syscounter_clk_enable(void);
uint64_t drv_syscounter_read(void);

/*
 * SYSHUB window: SE addresses 0x60000000..0x7fffffff reach the SoC's
 * 64-bit address space in eight 128 MiB slots.
 */
#define SYSH_IO_WINDOW		0x60000000u
#define SYSH_IO_SLOT_SIZE	0x08000000u
uint32_t sysh_io_init(uint64_t map_addr);
uint32_t sysh_io_map(uint64_t addr, uint32_t range);
uint64_t sysh_io_get_map_addr(void);

#endif
