/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Byte ring queues over a caller's buffer (CIX's
 * Source/common/utilities/u_rrq.c): head is where the producer writes,
 * tail where the consumer reads, one byte stays free.  A queue that
 * overrides on full drops its oldest bytes.  u_rrq_free, _set_locker,
 * _try_put_entire, _read, the pattern and package readers, _isEmpty and
 * _isFull are not in CIX's image (unused) and not here.
 *
 * Locks: CIX's U_MUTEX (its u_mutex spinlock) and RTOS_SEMPH (FreeRTOS
 * mutexes) guard against other tasks; this firmware has none (interrupt
 * handlers run from the main loop), so they lock nothing.  CUSTOMIZED
 * calls the queue's locker as CIX's does.
 */
#include "log/log.h"
#include "rtt.h"

typedef union {
	uint32_t u32Lock;
	U_RRQ_LOCKER pLocker;
} RRQ_LOCK;

typedef struct {
	uint8_t *pBuf;
	uint32_t bufLen;
	bool overrideOnFull;
	uint8_t lockType;		/* U_RRQ_LOCK_TYPE */
	uint32_t head;
	uint32_t tail;
	RRQ_LOCK producer;
	RRQ_LOCK consumer;
} U_RRQ_HANDLER;

static U_RRQ_HANDLER _s_rrqCfg[U_RRQ_MAX];
static bool _s_rrqInit;

void u_rrq_init(void)
{
	memset(_s_rrqCfg, 0, sizeof(_s_rrqCfg));
	_s_rrqInit = true;
}

/* A queue over pBuf; its id, U_RRQ_INVALID when all are in use */
uint8_t u_rrq_setup(void *pBuf, uint32_t length, bool overrideOnFull, U_RRQ_LOCK_TYPE lockType)
{
	if (!_s_rrqInit)
		vAssertCalledWithTRC(0xf1860167);
	for (uint8_t i = 0; i < U_RRQ_MAX; i++) {
		U_RRQ_HANDLER *q = &_s_rrqCfg[i];

		if (q->pBuf != NULL || q->bufLen != 0)
			continue;
		memset(&q->bufLen, 0, sizeof(*q) - offsetof(U_RRQ_HANDLER, bufLen));
		q->pBuf = pBuf;
		q->bufLen = length;
		q->overrideOnFull = overrideOnFull;
		q->lockType = (uint8_t)lockType;
		return i;
	}
	return U_RRQ_INVALID;
}

static void _s_lock(RRQ_LOCK *lock, uint8_t type)
{
	if (type > U_RRQ_E_CUSTOMIZED)
		vAssertCalledWithTRC(0xf1860111);
	if (type != U_RRQ_E_CUSTOMIZED)
		return;
	if (lock->pLocker == NULL)
		vAssertCalledWithTRC(0xf1860108);
	lock->pLocker(true, true);
}

static void _s_unlock(RRQ_LOCK *lock, uint8_t type)
{
	if (type > U_RRQ_E_CUSTOMIZED)
		vAssertCalledWithTRC(0xf1860141);
	if (type != U_RRQ_E_CUSTOMIZED)
		return;
	if (lock->pLocker == NULL)
		vAssertCalledWithTRC(0xf1860138);
	lock->pLocker(false, false);
}

static U_RRQ_HANDLER *rrq(uint8_t rrqId, uint32_t trc)
{
	if (rrqId >= U_RRQ_MAX)
		vAssertCalledWithTRC(trc);
	return &_s_rrqCfg[rrqId];
}

/* Empty the queue (the consumer catches up) */
void u_rrq_reset(uint8_t rrqId)
{
	U_RRQ_HANDLER *q = rrq(rrqId, 0xf1860235);

	if (q->overrideOnFull)
		_s_lock(&q->producer, q->lockType);
	_s_lock(&q->consumer, q->lockType);
	q->tail = q->head;
	_s_unlock(&q->consumer, q->lockType);
	if (q->overrideOnFull)
		_s_unlock(&q->producer, q->lockType);
}

/* Up to length bytes in; how many went in */
uint32_t u_rrq_put(uint8_t rrqId, const void *pBuf, uint32_t length)
{
	U_RRQ_HANDLER *q = rrq(rrqId, 0xf1860264);
	const uint8_t *src = pBuf;
	uint32_t n = 0, next;

	if (src == NULL)
		vAssertCalledWithTRC(0xf1860265);
	_s_lock(&q->producer, q->lockType);
	for (; n < length; n++) {
		if ((q->head + 1) % q->bufLen == q->tail)
			break;
		q->pBuf[q->head] = src[n];
		q->head = (q->head + 1) % q->bufLen;
	}
	if (n < length && q->overrideOnFull) {
		/* full: the oldest bytes go */
		_s_lock(&q->consumer, q->lockType);
		for (; n < length; n++) {
			q->pBuf[q->head] = src[n];
			next = (q->head + 1) % q->bufLen;
			if (next == q->tail)
				q->tail = (next + 1) % q->bufLen;
			q->head = next;
		}
		_s_unlock(&q->consumer, q->lockType);
	}
	_s_unlock(&q->producer, q->lockType);
	return n;
}

/* Up to length bytes out; how many came */
uint32_t u_rrq_get(uint8_t rrqId, void *pBuf, uint32_t length)
{
	U_RRQ_HANDLER *q = rrq(rrqId, 0xf1860362);
	uint8_t *dst = pBuf;
	uint32_t n;

	if (dst == NULL)
		vAssertCalledWithTRC(0xf1860363);
	if (q->overrideOnFull)
		_s_lock(&q->producer, q->lockType);
	_s_lock(&q->consumer, q->lockType);
	for (n = 0; n < length && q->head != q->tail; n++) {
		dst[n] = q->pBuf[q->tail];
		q->tail = (q->tail + 1) % q->bufLen;
	}
	_s_unlock(&q->consumer, q->lockType);
	if (q->overrideOnFull)
		_s_unlock(&q->producer, q->lockType);
	return n;
}

uint32_t u_rrq_bytesInQueue(uint8_t rrqId)
{
	U_RRQ_HANDLER *q = &_s_rrqCfg[rrqId];
	uint32_t ps = log_lock(), n;

	n = q->head >= q->tail ? q->head - q->tail : q->head - q->tail + q->bufLen;
	log_unlock(ps);
	if (n >= q->bufLen)
		vAssertCalledWithTRC(0xf1860814);
	return n;
}

uint32_t u_rrq_getHeadIdx(uint8_t rrqId)
{
	return rrq(rrqId, 0xf1860434)->head;
}

uint32_t u_rrq_getTailIdx(uint8_t rrqId)
{
	return rrq(rrqId, 0xf1860442)->tail;
}

void u_rrq_SET_HeadIdx(uint8_t rrqId, uint32_t new_index)
{
	rrq(rrqId, 0xf1860457)->head = new_index;
}

void u_rrq_SET_TailIdx(uint8_t rrqId, uint32_t new_index)
{
	rrq(rrqId, 0xf1860465)->tail = new_index;
}
