/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Sky1 sensor fusion hub (SFH) firmware.
 *
 * A small Cortex-M next to the SE.  The SE copies this image into the SFH
 * SRAM (0x60000000 seen from the SE, 0x20000000 here) and starts the core
 * when the system enters suspend to RAM.  The SFH then serves timed wake-up
 * requests from the AP and the PM over the CIX mailbox and wakes the system
 * through SFH_WAKE.
 */
#ifndef SFH_H
#define SFH_H

#include <stdint.h>

#define REG32(a)		(*(volatile uint32_t *)(uintptr_t)(a))

/* SFH control block; the SE sees it at 0x48080000 */
#define SFH_CTRL		0x50070000u
#define SFH_CLK_EN		(SFH_CTRL + 0x00)	/* mailbox and timer clocks */
#define SFH_CLK_EN2		(SFH_CTRL + 0x14)
#define SFH_LOG_CTRL		(SFH_CTRL + 0x70)
#define SFH_WAKE		(SFH_CTRL + 0x80)	/* 3: wake the system */
#define SFH_HANDSHAKE		(SFH_CTRL + 0x100)	/* 0xaabb: firmware up */

#define SFH_HANDSHAKE_READY	0xaabbu
#define SFH_WAKE_REQUEST	3u

/* Timer block: 64-bit timers (two 32-bit halves) at 0..3, 32-bit at 4..7 */
#define SFH_TIMER_BASE		0x50010000u
#define SFH_TIMER_HZ		25000000u

/* Interrupts */
#define IRQ_TIMER64(n)		(16 + (n))
#define IRQ_TIMER32(n)		(18 + (n))
#define IRQ_MBOX(ch)		(23 + (ch))

/* Messages (word 1 of a three-word mailbox message) */
#define SF_MSG_SUSPEND		0xfff2u	/* AP: suspend, word 2: wake after s */
#define SF_MSG_SUSPEND_ACK	0xfff3u
#define SF_MSG_RESUME		0xfff4u	/* AP: resumed */
#define SF_MSG_RESUME_ACK	0xfff5u
#define SF_MSG_WAKE_AFTER	0xfff7u	/* PM: word 2: wake after ms */
#define SF_MSG_MIN		0xfff0u

/* irq.c */
typedef void (*irq_handler_t)(void *arg);
void irq_register(int irq, irq_handler_t handler, void *arg);
void irq_enable(int irq);
void irq_dispatch(void);
void cpu_setup(void);

/* timer.c */
void timer_init(void);
void timer_busy_wait_ms(uint32_t ms);

/* mbox.c */
#define MBOX_CHANNELS		4
#define MBOX_MSG_WORDS_MAX	32
struct mbox_client;
typedef void (*mbox_rx_cb_t)(struct mbox_client *cl, const uint32_t *msg);
struct mbox_client {
	uint32_t	tx_timeout_ms;
	uint8_t		tx_block;
	uint8_t		channel;
	uint8_t		index;
	mbox_rx_cb_t	rx;
};
int mbox_request_channel(struct mbox_client *cl, unsigned int ch, unsigned int index);
int mbox_send(unsigned int ch, const uint32_t *msg);
static inline unsigned int mbox_msg_words(const uint32_t *msg)
{
	return ((msg[0] & 0x7f) + 3) >> 2;
}

/* log.c: off unless log_enable() (as in the CIX image, which never turns it on) */
struct sfh_log {
	uint8_t		enable;
	uint32_t	level;
};
extern struct sfh_log sfh_log;
extern volatile uint32_t sfh_state;	/* 1: suspended (UART quiet), 2: resumed */
void log_set_level(uint32_t level);
int log_printf(uint32_t level, const char *fmt, ...);
#define LOG_ERR		1
#define LOG_INFO	3
#define LOG_DEBUG	4

#endif
