/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The PM's log (CIX's Source/common/logging/fwlog.c), CIX's assertion
 * (vAssertCalledWithTRC, Source/projects/sky1/port/lx7/demo_helper.c) and
 * the log part of the BIOS configuration (pm_log_config, hw_dbguart.c).
 *
 * Each message goes to the RAM log (rlog.c) and, behind a time stamp
 * "hh:mm:ss.uuuuuu ", to a 384-byte line buffer that is flushed to the
 * debug UART when that is permitted (CIX's is_uart_permitted: never) or
 * else to RTT up channel 2 once both DRAM switches are on: the run-time
 * one (fwlog_enable_dram(false, true) at power-on, once the RTT block is
 * there) and the global one (pm_log_config, from the BIOS).  Until then
 * the buffer keeps the first lines of the boot; once full, a line is cut
 * ("~\n" ends the buffer).
 *
 * CIX keeps a line buffer per interrupt priority layer and per task
 * (achieve_buffer); this firmware has neither tasks nor nested handlers,
 * so there is one, CIX's buffer for code before the scheduler.
 *
 * One change: CIX formats the message into the line buffer and gives the
 * RAM log vsnprintf's count, so once the buffer is full (as it is from
 * early in every boot, nothing flushing it) the RAM log gets that many
 * bytes from past the buffer (zeros in a model).  Here the message is
 * formatted on the stack and the RAM log gets the message.
 */
#include <stdarg.h>
#include <stdio.h>

#include "clk/clk.h"
#include "lcru.h"
#include "log.h"
#include "rtt/rtt.h"

#define FWLOG_BUF_SIZE		0x180
#define FWLOG_RTT_CHANNEL	2
#define FWLOG_TRACE_FORMAT	" traceCode 0x%08x"

typedef struct {
	char *pBuf;
	uint32_t bufSize;
	uint32_t msgSize;
} FWLOG_BUFF_TYPE;

static char _isrMsgbuff[FWLOG_BUF_SIZE];
static struct {
	FWLOG_BUFF_TYPE buf;
	bool dram_global;	/* the BIOS turned the DRAM log on */
	bool dram_rt;		/* RTT is there */
	bool initialized;
} _s_isrBuf;

static uint32_t _s_logLevel = 0x8287;

void fwlog_enable_dram(bool is_global, bool enable)
{
	if (is_global)
		_s_isrBuf.dram_global = enable;
	else
		_s_isrBuf.dram_rt = enable;
}

bool fwlog_is_rt_dram_enabled(void)
{
	return _s_isrBuf.dram_rt;
}

void fwlog_init(void)
{
	memset(&_s_isrBuf, 0, sizeof(_s_isrBuf));
	_s_isrBuf.buf.pBuf = _isrMsgbuff;
	_s_isrBuf.buf.bufSize = FWLOG_BUF_SIZE;
	_s_isrBuf.buf.msgSize = 0;
	_s_isrBuf.initialized = true;
}

/*
 * Bits 6-0 of the mask from a verbosity: level n (1-127) turns on its
 * highest bit and those below (CIX's boot: 4 gives 0x07); 0 turns them off; 128 and
 * up set them to 8 (as CIX's does).  Bit 7 (INFO) stays on, the others
 * are kept.
 */
void fwlog_setLogLevel(uint32_t level)
{
	uint32_t v;

	if (level == 0) {
		v = 0;
	} else if (level >= 128) {
		v = (_s_logLevel & ~0x7fu) + 8;
	} else {
		int b = 6;

		while (!(level & BIT(b)))
			b--;
		v = (_s_logLevel & ~0x7fu) | (BIT(b + 1) - 1);
	}
	_s_logLevel = v | FWLOG_INFO;
}

uint32_t fwlog_getLogLevel(void)
{
	return _s_logLevel;
}

/* A line's worth of text into the buffer; what does not fit is cut, the
 * buffer then ends in "~\n" */
static void fwlog_seal_buff(FWLOG_BUFF_TYPE *b, uint32_t length)
{
	if (length < b->bufSize - b->msgSize) {
		b->msgSize += length;
		return;
	}
	b->msgSize = b->bufSize;
	b->pBuf[b->bufSize - 2] = '~';
	b->pBuf[b->bufSize - 1] = '\n';
}

static void fwlog_prefix(void)
{
	FWLOG_BUFF_TYPE *b = &_s_isrBuf.buf;
	char *p = b->pBuf + b->msgSize;
	uint64_t us = arch_getTimeNowUs();
	int n;

	n = snprintf(p, b->bufSize - b->msgSize, "%02d:%02d:%02d.%06d ",
		     (int)(us / 3600000000ull), (int)(us / 60000000 % 60), (int)(us / 1000000 % 60),
		     (int)(us % 1000000));
	for (int i = 0; i < n - 1 && i < (int)(b->bufSize - b->msgSize); i++) {
		if (p[i] == ' ')
			p[i] = '0';
	}
	fwlog_seal_buff(b, (uint32_t)n);
}

static void fwlog_flush(void (*the_putch)(char))
{
	FWLOG_BUFF_TYPE *b = &_s_isrBuf.buf;
	uint32_t ps = log_lock();

	for (uint32_t i = 0; i < b->msgSize; i++)
		the_putch(b->pBuf[i]);
	b->msgSize = 0;
	log_unlock(ps);
}

static void fwlog_putch2dram(char ch)
{
	RTT_Write(FWLOG_RTT_CHANNEL, &ch, 1);
}

static void fwlog_format_write(uint32_t level, uint32_t fw_location, const char *format,
			       va_list args)
{
	FWLOG_BUFF_TYPE *b = &_s_isrBuf.buf;
	char line[FWLOG_BUF_SIZE];
	uint32_t ps = 0;
	int n;

	if (!_s_isrBuf.initialized || !(level & _s_logLevel))
		return;
	if (level & FWLOG_CRITICAL)
		ps = log_lock();
	fwlog_prefix();
	n = vsnprintf(line, sizeof(line), format, args);
	if (n > (int)sizeof(line) - 1)
		n = sizeof(line) - 1;
	rlog_write(level, line, (size_t)n);
	strncpy(b->pBuf + b->msgSize, line, b->bufSize - b->msgSize);
	fwlog_seal_buff(b, (uint32_t)n);
	if (fw_location)
		fwlog_seal_buff(b, (uint32_t)snprintf(b->pBuf + b->msgSize, b->bufSize - b->msgSize,
						      FWLOG_TRACE_FORMAT, (unsigned int)fw_location));
	if (b->msgSize == b->bufSize) {
		b->pBuf[b->bufSize - 2] = '~';
		b->pBuf[b->bufSize - 1] = '\n';
	} else {
		b->pBuf[b->msgSize++] = '\n';
	}
	/* CIX: to the UART (and '\r') if dev_pl011_isEnabled, which its
	 * is_uart_permitted never allows */
	if (_s_isrBuf.dram_global && _s_isrBuf.dram_rt)
		fwlog_flush(fwlog_putch2dram);
	if (level & FWLOG_CRITICAL)
		log_unlock(ps);
}

void fwlog_write(uint32_t level, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	fwlog_format_write(level, 0, format, args);
	va_end(args);
}

/* format appended to the string in buffer (size bytes); the new length */
size_t fwlog_append(size_t size, char *buffer, const char *format, ...)
{
	size_t len = strnlen(buffer, size);
	va_list args;
	int n;

	if (len >= size)
		return len;
	va_start(args, format);
	n = vsnprintf(buffer + len, size - len, format, args);
	va_end(args);
	if (n < 0 || (size_t)n >= size - len)
		vAssertCalledWithTRC(0xf1470669);
	return len + (size_t)n;
}

const char *result_str(bool result)
{
	return result ? "OK" : "FAIL";
}

/*
 * CIX's pm_log_config (hw_dbguart.c), from do_pm_config: the log
 * configuration's first word 0x5b74183c forbids the UART and turns the
 * DRAM log on, anything else turns it off.  The second word is the UART's
 * baud rate (bit 0 clear, rate << 1), which the UART here does not take.
 */
#define LOG_CFG_DRAM_MAGIC	0x5b74183cu

void pm_log_config(const uint8_t *cfg)
{
	uint32_t magic = (uint32_t)cfg[0] | (uint32_t)cfg[1] << 8 | (uint32_t)cfg[2] << 16 |
			 (uint32_t)cfg[3] << 24;

	fwlog_enable_dram(true, magic == LOG_CFG_DRAM_MAGIC);
}

/* CIX's vAssertCalledWithTRC: the code in a scratch word (as the clock
 * code's asserts), then stop */
void vAssertCalledWithTRC(uint32_t trc)
{
	REG32(PM_SCRATCH + 0x3c) = trc;
	__builtin_trap();
}
