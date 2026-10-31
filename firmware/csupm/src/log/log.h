/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The PM's log (CIX's Source/common/logging/fwlog.c and rlog.c), its
 * version for the AP (Source/common/modules/md_fwver.c) and its assertion
 * (Source/projects/sky1/port/lx7/demo_helper.c).  CIX's names.
 *
 * fwlog_write(level, format, ...) formats a message.  If level has a bit
 * of the log level mask (fwlog_setLogLevel), the message goes
 *  - to the RAM log in DRAM (rlog: 0x83db8000, 8 KB), a ring of records
 *    {"level":"INFO","time":"0x<AP counter, 16 hex digits>","log":"<message>\n};
 *    which the SE and the AP's tools read (the SE once the PM wrote 0xaa55
 *    in mailbox 8's fast channel 0); the previous boot's log is kept at
 *    0x83dba000;
 *  - with a time stamp to a line buffer, which goes to the debug UART if
 *    that is permitted (never, in CIX's build) or else to RTT up channel 2
 *    in DRAM once the BIOS's configuration turned the DRAM log on
 *    (pm_log_config).
 */
#ifndef LOG_H
#define LOG_H

#include "pm.h"

/* Level bits: how the RAM log names them, and the bits CIX's callers use */
#define FWLOG_WARN		BIT(0)		/* also bit 2 */
#define FWLOG_ERR		BIT(1)
#define FWLOG_INFO		BIT(7)		/* also bit 3; others: "UNK" */
#define FWLOG_INTR		BIT(9)		/* intr_platform_init */
#define FWLOG_PCC		BIT(18)		/* pcc.c, off by default */
#define FWLOG_CRITICAL		BIT(30)		/* written with interrupts off */

void fwlog_init(void);
void fwlog_enable_dram(bool is_global, bool enable);
bool fwlog_is_rt_dram_enabled(void);
void fwlog_setLogLevel(uint32_t level);
uint32_t fwlog_getLogLevel(void);
void fwlog_write(uint32_t level, const char *format, ...)
	__attribute__((format(printf, 2, 3)));
size_t fwlog_append(size_t size, char *buffer, const char *format, ...)
	__attribute__((format(printf, 3, 4)));
const char *result_str(bool result);
/* The log part of the BIOS's PM configuration (CIX's hw_dbguart.c) */
void pm_log_config(const uint8_t *cfg);

/* RAM log (rlog.c) */
#define RLOG_DRAM_BASE		0x83db8000u
void rlog_init(uint32_t base_addr);
void rlog_write(uint32_t level, const char *message, size_t length);

/* Version strings (md_fwver.c): 0 product, 1 version, 2 "G", 3 date
 * (yymmdd), 4 time (hhmmss), 5 branch, 6 commit */
uint8_t md_version_stringGet(uint8_t u8String, uint8_t *pBuf, uint8_t length);
uint8_t md_version_lengthGet(uint8_t u8String);
const uint8_t *md_version_stringPtr(uint8_t u8String);
void md_version_broadcast(void);

/* CIX's assertion: a code naming the file and line; the PM stops */
__attribute__((noreturn)) void vAssertCalledWithTRC(uint32_t trc);

/* CIX's md_tasks_enterCritical/exitCritical: interrupts off and on (the
 * interrupt vectors only note a line; handlers run from the main loop) */
static inline uint32_t log_lock(void)
{
	uint32_t ps;

	__asm__ volatile("rsil %0, 15" : "=a"(ps) : : "memory");
	return ps;
}

static inline void log_unlock(uint32_t ps)
{
	__asm__ volatile("wsr.ps %0; rsync" : : "a"(ps) : "memory");
}

#endif
