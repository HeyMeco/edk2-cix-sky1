/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * RTT channels in DRAM (CIX's Source/common/rtt/rtt.c): a SEGGER-style
 * control block the host (the AP, or a debugger reading DRAM) finds by
 * its ID, with up channels (PM to host) and down channels (host to PM).
 * Each channel's buffer is a ring queue (u_rrq.c) over the buffer as the
 * PM sees it through the hub window; the queue's head and tail follow
 * the control block's write and read offsets, the host moving the other
 * end.  RTT_DumpChanInfo (debug output) is left out.
 */
#include "log/log.h"
#include "rtt.h"

static struct {
	uint8_t up_rrq[RTT_UP_CHANNELS];
	uint8_t down_rrq[RTT_DOWN_CHANNELS];
	RTT_CB *cb;
} rtt;

/* The control block at p (the PM's address of it), the channels'
 * queues */
void RTT_Init(RTT_CB *p, const RTT_CHANNEL *channels, int NumOfChan)
{
	/* CIX's leaves the down channel's queue id as it was */
	memset(rtt.up_rrq, U_RRQ_INVALID, sizeof(rtt.up_rrq));
	memset(&p->acID[9], 0, sizeof(p->acID) - 9);
	memcpy(p->acID, "CIXPM", 5);
	p->acID[5] = ' ';
	memcpy(&p->acID[6], "RTT", 3);
	p->MaxNumUpBuffers = RTT_UP_CHANNELS;
	p->MaxNumDownBuffers = RTT_DOWN_CHANNELS;

	for (int i = 0; i < NumOfChan; i++) {
		const RTT_CHANNEL *ch = &channels[i];
		uint8_t id;

		if (ch->isUp) {
			RTT_BUFFER_UP *b;

			if (ch->index >= RTT_UP_CHANNELS)
				vAssertCalledWithTRC(0xf15d0029);
			b = &p->aUp[ch->index];
			b->sNameJLINK = ch->sNameJLINK;
			b->pBufferJLINK = ch->pBufferJLINKBase + ch->Offset;
			b->SizeOfBuffer = ch->SizeOfBuffer;
			b->RdOff = 0;
			b->WrOff = 0;
			b->Flags = ch->Flags;
			id = u_rrq_setup((void *)ch->pBufferPM, ch->SizeOfBuffer, false, U_RRQ_E_NOLOCK);
			rtt.up_rrq[ch->index] = id;
			if (id == U_RRQ_INVALID)
				vAssertCalledWithTRC(0xf15d0040);
		} else {
			RTT_BUFFER_DOWN *b;

			if (ch->index >= RTT_DOWN_CHANNELS)
				vAssertCalledWithTRC(0xf15d0042);
			b = &p->aDown[ch->index];
			b->sNameJLINK = ch->sNameJLINK;
			b->pBufferJLINK = ch->pBufferJLINKBase + ch->Offset;
			b->SizeOfBuffer = ch->SizeOfBuffer;
			b->RdOff = 0;
			b->WrOff = 0;
			b->Flags = ch->Flags;
			id = u_rrq_setup((void *)ch->pBufferPM, ch->SizeOfBuffer, false, U_RRQ_E_NOLOCK);
			rtt.down_rrq[ch->index] = id;
			if (id == U_RRQ_INVALID)
				vAssertCalledWithTRC(0xf15d0053);
		}
	}
	rtt.cb = p;
}

static uint8_t RTT_get_rrqid(uint32_t chID, bool isUp)
{
	uint8_t id;

	if (isUp) {
		if (chID >= RTT_UP_CHANNELS)
			vAssertCalledWithTRC(0xf15d0065);
		id = rtt.up_rrq[chID];
	} else {
		if (chID >= RTT_DOWN_CHANNELS)
			vAssertCalledWithTRC(0xf15d0068);
		id = rtt.down_rrq[chID];
	}
	if (id == U_RRQ_INVALID)
		vAssertCalledWithTRC(0xf15d0072);
	return id;
}

/* What the host has read of an up channel frees space */
static void _update_up_rrq_tail(uint8_t rrq_id, uint32_t upChannel)
{
	uint32_t tail = rtt.cb->aUp[upChannel].RdOff;

	if (tail >= rtt.cb->aUp[upChannel].SizeOfBuffer)
		vAssertCalledWithTRC(0xf15d0091);
	u_rrq_SET_TailIdx(rrq_id, tail);
}

/* What the host has written to a down channel is there to read */
static void _update_down_rrq_head(uint8_t rrq_id, uint32_t downChannel)
{
	uint32_t head = rtt.cb->aDown[downChannel].WrOff;

	if (head >= rtt.cb->aDown[downChannel].SizeOfBuffer)
		vAssertCalledWithTRC(0xf15d0117);
	u_rrq_SET_HeadIdx(rrq_id, head);
}

/* Up to len bytes to an up channel; how many went */
uint32_t RTT_Write(uint32_t upChannel, const void *data, uint32_t len)
{
	uint8_t id = RTT_get_rrqid(upChannel, true);
	uint32_t ps = log_lock(), n;

	_update_up_rrq_tail(id, upChannel);
	n = u_rrq_put(id, data, len);
	rtt.cb->aUp[upChannel].WrOff = u_rrq_getHeadIdx(id);
	log_unlock(ps);
	return n;
}

/* Up to len bytes from a down channel; how many came */
uint32_t RTT_Read(uint32_t downChannel, uint8_t *data, uint32_t len)
{
	uint8_t id = RTT_get_rrqid(downChannel, false);
	uint32_t ps = log_lock(), n;

	_update_down_rrq_head(id, downChannel);
	n = u_rrq_get(id, data, len);
	rtt.cb->aDown[downChannel].RdOff = u_rrq_getTailIdx(id);
	log_unlock(ps);
	return n;
}

uint32_t RTT_BytesInQueue(uint32_t chID, bool isUp)
{
	uint8_t id = RTT_get_rrqid(chID, isUp);

	if (isUp)
		_update_up_rrq_tail(id, chID);
	else
		_update_down_rrq_head(id, chID);
	return u_rrq_bytesInQueue(id);
}

/* Drop what a channel holds: the writer's offset to the reader's, until
 * they agree (the host may be moving its end) */
void RTT_Reset(uint32_t chID, bool isUp)
{
	uint8_t id = RTT_get_rrqid(chID, isUp);
	uint32_t ps = log_lock(), pointer, loop_cnt = 0;

	if (isUp) {
		RTT_BUFFER_UP *b = &rtt.cb->aUp[chID];

		do {
			pointer = b->RdOff;
			b->WrOff = pointer;
			if (++loop_cnt % 100 == 0)
				fwlog_write(FWLOG_INFO, "--Reset RTT [%d:%d] pm to host, loop %d ...",
					    (int)chID, id, (int)loop_cnt);
		} while (b->WrOff != b->RdOff);
	} else {
		RTT_BUFFER_DOWN *b = &rtt.cb->aDown[chID];

		do {
			pointer = b->WrOff;
			b->RdOff = pointer;
			if (++loop_cnt % 100 == 0)
				fwlog_write(FWLOG_INFO, "--Reset RTT [%d:%d] host to pm, loop %d ...",
					    (int)chID, id, (int)loop_cnt);
		} while (b->RdOff != b->WrOff);
	}
	u_rrq_SET_HeadIdx(id, pointer);
	u_rrq_SET_TailIdx(id, pointer);
	log_unlock(ps);
}
