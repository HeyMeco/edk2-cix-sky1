/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Sky1 SE (security engine) firmware: common definitions.
 *
 * The SE is a Cortex-M33 (Arm China STAR-SE).  Its own peripherals sit at
 * 0xa0000000 and 0xb0000000; the rest of the SoC is reached at the SoC
 * addresses below 0x60000000 and, for DRAM and other high addresses,
 * through the SYSHUB window at 0x60000000 (drv_syshub_io.c).
 */
#ifndef SE_H
#define SE_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define REG32(a)		(*(volatile uint32_t *)(uintptr_t)(a))
#define REG8(a)			(*(volatile uint8_t *)(uintptr_t)(a))
#define ARRAY_SIZE(a)		(sizeof(a) / sizeof((a)[0]))
#define BIT(n)			(1u << (n))
#define MIN(a, b)		((a) < (b) ? (a) : (b))
#define MAX(a, b)		((a) > (b) ? (a) : (b))

static inline void dsb(void) { __asm__ volatile("dsb sy" ::: "memory"); }
static inline void isb(void) { __asm__ volatile("isb sy" ::: "memory"); }

/* SE peripherals */
#define SE_UART_BASE		0xa0010000u
#define SE_POSTCODE_BASE	0xa0020000u
#define SE_CLKCTRL_BASE		0xa0030000u	/* clock enables, UART divider, mailbox */
#define SE_CE_BASE		0xa0040000u	/* Arm China CE-Lite crypto engine */
#define SE_TIMER_BASE		0xb0010000u
#define SE_WDT_BASE		0xb0020000u
#define SE_SYSCTRL_BASE		0xb0030000u
#define SE_EFUSE_BASE		0xb0040000u
#define SE_EFUSE_CTRL_BASE	0xb0050000u
#define SE_QSPI_BASE		0xe0044000u

/* SE system control registers */
#define SE_SYSCTRL_CTRL		(SE_SYSCTRL_BASE + 0x000)
#define SE_SYSCTRL_CLKDIV	(SE_SYSCTRL_BASE + 0x004)	/* [3:0]: core clock = 800 MHz / (n + 1) */
#define SE_SYSCTRL_WDT_CTRL	(SE_SYSCTRL_BASE + 0x010)
#define SE_SYSCTRL_LIFECYCLE	(SE_SYSCTRL_BASE + 0x0e0)	/* [2:0] */
#define SE_SYSCTRL_ROM_CTRL	(SE_SYSCTRL_BASE + 0x0e8)	/* bit 13: lock the boot ROM */
#define SE_SYSCTRL_SYSH_MAP	(SE_SYSCTRL_BASE + 0x100)	/* SYSHUB window map, 8 x 128 MiB */
#define SE_SYSCTRL_LOG_LEVEL	(SE_SYSCTRL_BASE + 0x208)	/* scratch: current log level */
#define SE_SYSCTRL_PC_LOCK	(SE_SYSCTRL_BASE + 0x20c)	/* scratch: postcode frozen on error */
#define SE_SYSCTRL_IDLE		(SE_SYSCTRL_BASE + 0x22c)	/* scratch */

/* Life cycle (SE_SYSCTRL_LIFECYCLE, from the CE OTP) */
#define LIFE_CYCLE_UNKNOWN	0
#define LIFE_CYCLE_UNRECOGNIZED	1
#define LIFE_CYCLE_FRA		3	/* field return */
#define LIFE_CYCLE_BLANK	4	/* nothing fused */
#define LIFE_CYCLE_NONSECURE	5
#define LIFE_CYCLE_SECUNLOCK	6
#define LIFE_CYCLE_SECURE	7

/* Log levels */
#define LOG_ERR			1
#define LOG_WARN		2
#define LOG_INFO		3

uint32_t hal_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
uint32_t hal_printf_level(uint32_t level, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));
void hal_log_set_level(uint32_t level);
void hexdump(const char *tag, const uint8_t *buf, uint32_t size);

/* Postcodes: progress (0x90xx) and the first error (module, line) */
void hal_pc_trace(uint32_t code);
void hal_pc_log_ec(uint32_t err, uint32_t line);

#define SE_ERR(err) do { \
		hal_pc_log_ec((err), __LINE__); \
		hal_printf_level(LOG_ERR, "[%s:%d]ERR: 0x%08x\n", __func__, __LINE__, (unsigned)(err)); \
	} while (0)

#define SE_INFO(fmt, ...) \
	hal_printf_level(LOG_INFO, "[%s:%d]" fmt, __func__, __LINE__, ##__VA_ARGS__)

/* Delays (SysTick) */
void hal_udelay(uint32_t us);
void hal_mdelay(uint32_t ms);

/* libc */
void *malloc(size_t size);
void *calloc(size_t n, size_t size);
void free(void *p);
unsigned long strtoul(const char *s, char **end, int base);
int strcasecmp(const char *a, const char *b);
void system_heap_init(void);

#endif
