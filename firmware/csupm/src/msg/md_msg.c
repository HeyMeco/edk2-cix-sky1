/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Message ports (CIX's Source/common/modules/md_msg.c): a port is a
 * channel in a mailbox's data words with a profile (how to read the
 * header, length and status of its format) and a table of messages.
 * md_msg_port_dispatch takes a message from the channel if it is one of
 * CIX's, runs it and answers: the status code, CIX's trace code, the
 * length, the channel freed (route byte 0xc0), the AP's interrupt if
 * asked for.
 *
 * CIX runs messages of type 1 and 2 as jobs (md_jobs) on a task; type 1
 * is answered when its job ran, type 2 at once (with a copy of the input
 * for the job), at most 30 jobs a port waiting.  Here both run in line:
 * the answers and the order of what they do are the same.
 */
#include "msg/msg.h"
#include "mbox.h"

/* CIX's trace codes of the answers (vAssertCalledWithTRC style) */
#define TRC_INACTIVE		0xf1510453u
#define TRC_NOT_BUSY		0xf1510461u
#define TRC_NO_MESSAGE		0xf1510478u
#define TRC_DISABLED		0xf1510489u
#define TRC_DONE		0xf1510520u
#define TRC_DEFERRED_DONE	0xf1510398u
#define TRC_QUEUED		0xf1510630u

/* status codes */
#define MSG_DISABLED		1u
#define MSG_NOT_FOUND		4u
#define MSG_NOT_BUSY		8u

/* the default profile: TF-A's mailbox_mem_t with CIX's fields */
#define CH_ATTR			0x00
#define CH_STATUS		0x04
#define CH_STATUS_CODE		0x08
#define CH_TRACE_CODE		0x0c
#define CH_FLAGS		0x10
#define CH_LENGTH		0x14
#define CH_HEADER		0x18
#define CH_PAYLOAD		0x1c
#define CH_IN_SLOTS		12
#define CH_OUT			(CH_PAYLOAD + 4 * CH_IN_SLOTS)
#define CH_OUT_SLOTS		12
#define ROUTE_MASK		0xf00u
#define ROUTE_PM		0xc00u
#define ROUTE_ANSWER		0xc0u

typedef struct {
	bool isActive;
	uintptr_t channelBase;
	uint32_t mbox_inst_id;
	const MD_MSG_TABLE *msgTab;
	bool isSorted;
	uint32_t tabLen;
	uint32_t curMsgHdr;
} MSG_PORT_CTRL;

static MSG_PORT_CTRL ports[MSG_PORTS];

#define CH(p, off)		REG32((p)->channelBase + (off))

/* CIX's dft_profile_isTargetPM: the route field says it is a PM message */
static bool dft_profile_isTargetPM(const MSG_PORT_CTRL *p)
{
	return (CH(p, CH_ATTR) & ROUTE_MASK) == ROUTE_PM;
}

static uint32_t dft_profile_getMsgHeader(const MSG_PORT_CTRL *p)
{
	return CH(p, CH_HEADER);
}

static uint32_t dft_profile_getMsgLength(const MSG_PORT_CTRL *p)
{
	uint32_t len = CH(p, CH_LENGTH);

	return len > 4 ? (len - 4) >> 2 : 0;
}

static void dft_profile_setOutputLen(const MSG_PORT_CTRL *p, uint32_t words)
{
	CH(p, CH_LENGTH) = 4 + 4 * words;
}

/* the AP gave the channel to the PM: free bit clear */
static bool dft_profile_isChannelBusy(const MSG_PORT_CTRL *p)
{
	return !(CH(p, CH_STATUS) & 1);
}

/* CIX's dft_profile_freeChannel: the answer's route byte, then free */
static void dft_profile_freeChannel(const MSG_PORT_CTRL *p)
{
	REG8(p->channelBase + CH_ATTR + 1) = ROUTE_ANSWER;
	CH(p, CH_STATUS) |= 1;
}

bool md_msg_port_register_table(uint32_t port, const MD_MSG_TABLE *tab)
{
	MSG_PORT_CTRL *p = &ports[port];
	uint32_t n = 0, last = 0;
	bool sorted = true;

	p->msgTab = tab;
	if (tab) {
		for (; tab[n].msgId != 0xffff; n++) {
			if (tab[n].msgId < last)
				sorted = false;
			last = tab[n].msgId;
		}
		p->isSorted = sorted;
	}
	p->tabLen = n;
	return true;
}

bool md_msg_port_enable(uint32_t port)
{
	ports[port].isActive = ports[port].msgTab && ports[port].tabLen;
	return true;
}

void md_msg_port_set_default_profile(uint32_t port, uint32_t mbox_inst_id, uintptr_t chan)
{
	ports[port].channelBase = chan;
	ports[port].mbox_inst_id = mbox_inst_id;
}

static int32_t locate_sorted(uint16_t id, const MSG_PORT_CTRL *p)
{
	uint32_t lo = 0, hi = p->tabLen;

	while (lo < hi) {
		uint32_t mid = (lo + hi) >> 1;

		if (p->msgTab[mid].msgId == id)
			return (int32_t)mid;
		if (id < p->msgTab[mid].msgId)
			hi = mid;
		else
			lo = mid + 1;
	}
	return -1;
}

static int32_t locate_unsorted(uint16_t id, const MSG_PORT_CTRL *p)
{
	for (uint32_t i = 0; i < p->tabLen; i++) {
		if (p->msgTab[i].msgId == id)
			return (int32_t)i;
	}
	return -1;
}

/* the end of an answer: status, trace, the channel freed, the interrupt */
static void answer(const MSG_PORT_CTRL *p, uint32_t words, uint32_t status, uint32_t trace)
{
	bool irq;

	dft_profile_setOutputLen(p, words);
	CH(p, CH_STATUS_CODE) = status;
	CH(p, CH_TRACE_CODE) = trace;
	irq = CH(p, CH_FLAGS) & 1;
	dft_profile_freeChannel(p);
	if (irq)
		mbox_ring(p->mbox_inst_id, MBOX_TO_AP_BIT);	/* dev_mbox_raise_interrupt */
}

/*
 * CIX's md_msg_port_dispatch: false when the channel does not hold a PM
 * message (it is SCMI then); true when it was taken and answered.
 */
bool md_msg_port_dispatch(uint32_t port)
{
	MSG_PORT_CTRL *p = &ports[port];
	const MD_MSG_TABLE *e;
	uint32_t hdr, len, ret, in[CH_IN_SLOTS], out[CH_OUT_SLOTS];
	int32_t idx;

	if (port >= MSG_PORTS || !p->channelBase || !dft_profile_isTargetPM(p))
		return false;
	if (!p->isActive) {
		answer(p, 0, MSG_DISABLED, TRC_INACTIVE);
		return true;
	}
	if (!dft_profile_isChannelBusy(p)) {
		answer(p, 0, MSG_NOT_BUSY, TRC_NOT_BUSY);
		return true;
	}
	hdr = dft_profile_getMsgHeader(p);
	idx = p->isSorted ? locate_sorted((uint16_t)hdr, p) : locate_unsorted((uint16_t)hdr, p);
	if (idx < 0) {
		answer(p, 0, MSG_NOT_FOUND, TRC_NO_MESSAGE);
		return true;
	}
	e = &p->msgTab[idx];
	if (e->msgFlags & 1) {
		answer(p, 0, MSG_DISABLED, TRC_DISABLED);
		return true;
	}
	p->curMsgHdr = hdr;
	len = dft_profile_getMsgLength(p);
	if (e->type == 2) {
		/* answered at once; the message runs on a copy of its input */
		for (uint32_t i = 0; i < len && i < CH_IN_SLOTS; i++)
			in[i] = CH(p, CH_PAYLOAD + 4 * i);
		p->curMsgHdr = UINT32_MAX;
		answer(p, 0, 0, TRC_QUEUED);
		(void)e->msgProc(port, &len, in, out, NULL);
		return true;
	}
	ret = e->msgProc(port, &len, (void *)(p->channelBase + CH_PAYLOAD),
			 (void *)(p->channelBase + CH_OUT), (uint32_t *)(p->channelBase + CH_TRACE_CODE));
	p->curMsgHdr = UINT32_MAX;
	answer(p, len, ret, e->type == 1 ? TRC_DEFERRED_DONE : TRC_DONE);
	return true;
}
