/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The counter blocks (CIX's Source/common/drivers/dev_counters.c).  CIX's
 * timer and free-running functions, the stops and the notification
 * switches are not in its image (unused); handle_counter_before_sleep and
 * recover_counter_after_sleep are src/hsm/soc_ctrl.c's.
 *
 * A node's registers (CIX's DEV_CNTS_REG): +0x00 initial value, +0x18
 * pulses, +0x20 compare, +0x24 control (mode [2:0], auto reload 4, load 5,
 * interrupt 6, count down 7, edge 9-10, halt 11, pulse monitor 13, PWM
 * output 14, timer 17, reset 18), +0x28 PWM duty, +0x2c PWM period,
 * +0x30 interrupt status, +0x34 its clear.  The PWM and tachometer clock is
 * 25 MHz (CIX's constant, whatever the instance's reference).
 */
#include "dev_counters.h"
#include "irq.h"
#include "lcru.h"
#include "perf.h"

#define CNTS_INSTANCES		3
#define CNTS_NODES		8
#define CNTS_CLK_HZ		25000000u

#define CNT_LDINIT		0x00
#define CNT_PULSE_CNT		0x18
#define CNT_COMPARE		0x20
#define CNT_TCTL		0x24
#define CNT_PWM_DUTY		0x28
#define CNT_PWM_PERIOD		0x2c
#define CNT_INTR_CLR		0x34
#define TCTL_EDGE(v)		(((v) >> 9) & 3)
#define TCTL_RESET		BIT(18)

#define MODE_PWM		3
#define MODE_TACH		4

/* CIX's DEV_CNTS_SETUP */
typedef struct {
	uint16_t mode;
	uint16_t data;		/* a tachometer: callback parameter << 8 | squares per turn */
	union {
		void (*timerCb)(void);
		void (*tachCb)(uint32_t);
	};
	uint32_t ctrl;
} DEV_CNTS_NODE_CFG_DATA;

typedef struct {
	uint32_t baseAddr;
	uint32_t refClkFreq;
	DEV_CNTS_NODE_CFG_DATA cfgData[CNTS_NODES];
} DEV_CNTS_SETUP;

static uint32_t store_last_cnt[4];	/* dev_cnts_tach_read_rpm's */
static DEV_CNTS_SETUP cnts_inst[CNTS_INSTANCES];

#define CNTS_ASSERT(c, code)	do { if (!(c)) __builtin_trap(); } while (0)

static uint32_t _id_to_base(DEV_CNTS_ID id)
{
	CNTS_ASSERT(id.instIdx < CNTS_INSTANCES, 0xf1380145);
	return cnts_inst[id.instIdx].baseAddr + (id.nodeIdx << 12);
}

static DEV_CNTS_ID id_with_node(DEV_CNTS_ID id, uint32_t node)
{
	id.nodeIdx = node;
	return id;
}

static DEV_CNTS_NODE_CFG_DATA *node_cfg(DEV_CNTS_ID id, uint32_t node)
{
	return &cnts_inst[id.instIdx].cfgData[node];
}

#define CNT_REG(base, off)	REG32((base) + (off))

/*
 * An instance's interrupt: each node pending, lowest first (15 sources):
 * nodes 0-7 their timer callbacks; 8 the 64-bit timer; 9-12 odd nodes'
 * timers; 13-14 the tachometers' (nodes 5 and 7) with their parameter
 */
static void _cnts_IntrHandler(uint32_t instId)
{
	uint32_t status = REG32(instId ? AGG_STATUS(23) : AGG_STATUS(22));
	DEV_CNTS_SETUP *inst = &cnts_inst[instId];

	if (!status)
		return;
	for (uint32_t i = 0; i <= 14; i++) {
		uint32_t b = u_misc_u32lowestOneBit(status, i), node, clr;

		if (b & 0x80)
			break;
		i = b;
		if (b < 8) {
			node = b;
			clr = 1;
			if (inst->cfgData[node].timerCb)
				inst->cfgData[node].timerCb();
		} else if (b == 8) {
			/* CIX's oddity: it tests node 1's callback but calls the word
			 * after the node table; no 64-bit timer is used */
			node = 1;
			clr = 30;
			if (inst->cfgData[1].timerCb)
				inst->cfgData[1].timerCb();
		} else if (b <= 12) {
			node = 2 * b - 17;
			clr = 32;
			if (inst->cfgData[node].timerCb)
				inst->cfgData[node].timerCb();
		} else {
			node = 2 * b - 21;
			clr = 64;
			if (inst->cfgData[node].tachCb)
				inst->cfgData[node].tachCb(inst->cfgData[node].data >> 8);
		}
		CNT_REG(inst->baseAddr + (node << 12), CNT_INTR_CLR) = clr;
	}
}

void dev_cnts_intr0(void *pdata)
{
	(void)pdata;
	_cnts_IntrHandler(0);
}

void dev_cnts_intr1(void *pdata)
{
	(void)pdata;
	_cnts_IntrHandler(1);
}

/* No node in use, the PM's two blocks in reset */
void dev_cnts_init(void)
{
	memset(cnts_inst, 0, sizeof(cnts_inst));
	for (uint32_t i = 0; i < CNTS_INSTANCES; i++) {
		for (uint32_t j = 0; j < CNTS_NODES; j++)
			cnts_inst[i].cfgData[j].mode = CNTS_MODE_NONE;
	}
	REG32(LCRU_RESET) &= ~(RESET_TIMER(0) | RESET_TIMER(1));
}

/*
 * Block instIdx at mmioAddr; blocks 0 and 1 (the PM's) come out of reset,
 * block 2 (the SoC's) has no bit here, CIX's rewrites the register all the
 * same
 */
void dev_cnts_set_instance(uint32_t mmioAddr, uint32_t instIdx, uint32_t refClkFreq)
{
	uint32_t v = REG32(LCRU_RESET);

	CNTS_ASSERT(instIdx < CNTS_INSTANCES, 0xf1380188);
	REG32(LCRU_RESET) = v | ((RESET_TIMER(0) << instIdx) & (RESET_TIMER(0) | RESET_TIMER(1)));
	cnts_inst[instIdx].baseAddr = mmioAddr;
	cnts_inst[instIdx].refClkFreq = refClkFreq;
	for (uint32_t j = 0; j < CNTS_NODES; j++)
		cnts_inst[instIdx].cfgData[j].mode = CNTS_MODE_NONE;
}

/* A node (or the pair it is in) for a use; false if taken */
bool dev_cnts_alloc_node(DEV_CNTS_ID id, uint32_t mode)
{
	uint32_t node = id.nodeIdx;
	DEV_CNTS_NODE_CFG_DATA *even = node_cfg(id, node & 6), *odd = node_cfg(id, node | 1);
	DEV_CNTS_NODE_CFG_DATA *self = node_cfg(id, node);
	bool pair_free;

	CNTS_ASSERT(id.instIdx < CNTS_INSTANCES, 0xf1380219);
	(void)_id_to_base(id);
	(void)_id_to_base(id_with_node(id, node & 6));
	(void)_id_to_base(id_with_node(id, node | 1));
	pair_free = even->mode == CNTS_MODE_NONE && odd->mode == CNTS_MODE_NONE;
	switch (mode) {
	case 0:
	case MODE_PWM:
		if (self->mode != CNTS_MODE_NONE)
			break;
		self->mode = (uint16_t)mode;
		return true;
	case 1:
		if (node >= 2 || !pair_free)
			break;
		odd->mode = 1;
		even->mode = 1;
		return true;
	case 2:
	case 0x8000:
		if (!pair_free)
			break;
		odd->mode = (uint16_t)mode;
		even->mode = (uint16_t)mode;
		return true;
	case MODE_TACH:
		if ((node | 3) != 7 || !pair_free)
			break;
		odd->mode = MODE_TACH;
		even->mode = MODE_TACH;
		return true;
	default:
		break;
	}
	CNTS_ASSERT(0, 0xf1380272);
	return false;
}

/* A node (a pair's both) given back and reset */
bool dev_cnts_free_node(DEV_CNTS_ID id)
{
	uint32_t node = id.nodeIdx, mode, base, even_base, odd_base;

	CNTS_ASSERT(id.instIdx < CNTS_INSTANCES, 0xf1380279);
	base = _id_to_base(id);
	even_base = _id_to_base(id_with_node(id, node & 6));
	odd_base = _id_to_base(id_with_node(id, node | 1));
	mode = node_cfg(id, node)->mode;
	if (mode < 5 && !(BIT(mode) & (BIT(1) | BIT(2) | BIT(4)))) {
		node_cfg(id, node)->mode = CNTS_MODE_NONE;
		CNT_REG(base, CNT_INTR_CLR) = UINT32_MAX;
		CNT_REG(base, CNT_TCTL) = TCTL_RESET;
		return true;
	}
	if (mode >= 5 && mode != 0x8000)
		return false;
	node_cfg(id, node | 1)->mode = CNTS_MODE_NONE;
	node_cfg(id, node & 6)->mode = CNTS_MODE_NONE;
	CNT_REG(even_base, CNT_INTR_CLR) = UINT32_MAX;
	CNT_REG(even_base, CNT_TCTL) = TCTL_RESET;
	CNT_REG(odd_base, CNT_INTR_CLR) = UINT32_MAX;
	CNT_REG(odd_base, CNT_TCTL) = TCTL_RESET;
	return true;
}

/* duty of 256 of the period, in counts; never 0 (2) */
static uint32_t pwm_duty(uint32_t period, uint32_t duty256)
{
	uint32_t d = (uint32_t)(((uint64_t)period * duty256) >> 8);

	return d ? d : 2;
}

bool dev_cnts_pwm_setup(DEV_CNTS_ID id, uint32_t freqHz, uint32_t duty256)
{
	uint32_t base = _id_to_base(id), period;
	volatile uint32_t ctrl;

	CNTS_ASSERT(node_cfg(id, id.nodeIdx)->mode == MODE_PWM, 0xf1380649);
	CNTS_ASSERT(duty256 <= 256 && freqHz - 1 < CNTS_CLK_HZ - 1, 0xf1380650);
	period = CNTS_CLK_HZ / freqHz;
	ctrl = MODE_PWM;
	CNT_REG(base, CNT_PWM_DUTY) = pwm_duty(period, duty256);
	CNT_REG(base, CNT_PWM_PERIOD) = period;
	CNT_REG(base, CNT_TCTL) = ctrl;
	return true;
}

/* PWM output and the timer on, not halted */
bool dev_cnts_pwm_start(DEV_CNTS_ID id)
{
	uint32_t base = _id_to_base(id);
	volatile uint32_t ctrl = (CNT_REG(base, CNT_TCTL) & ~(BIT(11) | BIT(14) | BIT(17))) + (BIT(14) | BIT(17));

	CNT_REG(base, CNT_TCTL) = ctrl;
	return true;
}

/* A new frequency, the duty cycle kept */
bool dev_cnts_pwm_update_freq(DEV_CNTS_ID id, uint32_t freqHz)
{
	uint32_t base = _id_to_base(id), period, duty;

	(void)CNT_REG(base, CNT_TCTL);
	CNTS_ASSERT(node_cfg(id, id.nodeIdx)->mode == MODE_PWM, 0xf1380688);
	CNTS_ASSERT(freqHz - 1 < CNTS_CLK_HZ - 1, 0xf1380689);
	period = CNTS_CLK_HZ / freqHz;
	duty = CNT_REG(base, CNT_PWM_DUTY);
	duty = (uint32_t)((uint64_t)duty * period / CNT_REG(base, CNT_PWM_PERIOD));
	CNT_REG(base, CNT_PWM_DUTY) = duty;
	CNT_REG(base, CNT_PWM_PERIOD) = period;
	return true;
}

bool dev_cnts_pwm_update_duty(DEV_CNTS_ID id, uint32_t duty256)
{
	uint32_t base = _id_to_base(id);

	CNTS_ASSERT(node_cfg(id, id.nodeIdx)->mode == MODE_PWM, 0xf1380707);
	CNTS_ASSERT(duty256 <= 256, 0xf1380708);
	CNT_REG(base, CNT_PWM_DUTY) = pwm_duty(CNT_REG(base, CNT_PWM_PERIOD), duty256);
	return true;
}

/*
 * A tachometer on a pair (nodes 4-5 or 6-7): the even node counts pulses,
 * the odd one counts down monitorIntervalMs and interrupts (the callback
 * with para)
 */
bool dev_cnts_tach_setup(DEV_CNTS_ID id, uint32_t monitorIntervalMs, uint32_t squarePerCycle,
			 void (*callback)(uint32_t), uint32_t para)
{
	uint32_t node = id.nodeIdx, even_base, odd_base, ticks;
	DEV_CNTS_NODE_CFG_DATA *even, *odd;
	uint16_t data;

	CNTS_ASSERT(node >= 4, 0xf1380737);
	even_base = _id_to_base(id_with_node(id, node & 6));
	odd_base = _id_to_base(id_with_node(id, node | 1));
	even = node_cfg(id, node & 6);
	odd = node_cfg(id, node | 1);
	data = (uint16_t)((para << 8) + (squarePerCycle & 0xff));
	CNTS_ASSERT(even->mode == MODE_TACH && odd->mode == MODE_TACH, 0xf1380748);
	ticks = monitorIntervalMs * (CNTS_CLK_HZ / 1000);
	even->tachCb = callback;
	odd->tachCb = callback;
	even->data = data;
	odd->data = data;
	CNT_REG(odd_base, CNT_LDINIT) = ticks;
	CNT_REG(even_base, CNT_LDINIT) = ticks;
	CNT_REG(even_base, CNT_COMPARE) = 0;
	CNT_REG(odd_base, CNT_COMPARE) = 0;
	even->ctrl = MODE_TACH | BIT(13);			/* pulse monitor */
	odd->ctrl = MODE_TACH | BIT(4) | BIT(5) | BIT(6) | BIT(7);	/* reload, interrupt, down */
	return true;
}

bool dev_cnts_tach_start(DEV_CNTS_ID id)
{
	uint32_t node = id.nodeIdx, even_base, odd_base;
	volatile uint32_t odd_ctrl, even_ctrl;

	CNTS_ASSERT(node >= 4, 0xf1380791);
	even_base = _id_to_base(id_with_node(id, node & 6));
	odd_base = _id_to_base(id_with_node(id, node | 1));
	even_ctrl = (node_cfg(id, node & 6)->ctrl & ~(BIT(11) | BIT(13) | BIT(17))) + (BIT(13) | BIT(17));
	odd_ctrl = node_cfg(id, node | 1)->ctrl | BIT(13) | BIT(17);
	CNT_REG(odd_base, CNT_TCTL) = odd_ctrl;
	CNT_REG(even_base, CNT_TCTL) = even_ctrl;
	return true;
}

bool dev_cnts_tach_read_raw(DEV_CNTS_ID id, uint32_t *pOutput)
{
	uint32_t base;

	CNTS_ASSERT(id.nodeIdx & 4, 0xf1380820);
	base = _id_to_base(id_with_node(id, id.nodeIdx & 6));
	if (pOutput)
		*pOutput = CNT_REG(base, CNT_PULSE_CNT);
	return true;
}

/* Revolutions per minute since the last read: pulses over the interval */
bool dev_cnts_tach_read_rpm(DEV_CNTS_ID id, uint32_t *pOutput)
{
	uint32_t node = id.nodeIdx, even_base, odd_base, ctrl, interval, raw = 0, idx, spc, pps;
	int32_t k;

	CNTS_ASSERT(node >= 4, 0xf1380836);
	even_base = _id_to_base(id_with_node(id, node & 6));
	odd_base = _id_to_base(id_with_node(id, node | 1));
	ctrl = CNT_REG(even_base, CNT_TCTL);
	k = (int32_t)node - 4;
	idx = (uint32_t)((k >= 0 ? k : k + 1) >> 1) + 2 * id.instIdx;
	CNTS_ASSERT(idx < 3, 0xf1380853);
	interval = CNT_REG(odd_base, CNT_LDINIT);
	if (!interval)
		return false;
	spc = node_cfg(id, node & 6)->data & 0xff;
	(void)dev_cnts_tach_read_raw(id, &raw);
	pps = (uint32_t)((uint64_t)(raw - store_last_cnt[idx]) * CNTS_CLK_HZ / interval);
	store_last_cnt[idx] = raw;
	if (!spc)
		spc = 1;
	if (TCTL_EDGE(ctrl) >= 2) {
		spc <<= 1;
		if (TCTL_EDGE(ctrl) != 2)
			return false;
	}
	if (pOutput)
		*pOutput = pps * 60 / spc;
	return true;
}

/*
 * The AP's free-running counter (the RAM log's timestamps, the streams):
 * CIX's reads the high word, the low word, and reads again (at most ten
 * times) while the high word changed, or while the two words are equal
 * (odd, but CIX's: on a counter whose high word is 0, as in a model, every
 * read is repeated)
 */
#define AP_FREE_COUNTER_LO	0x56002008u
#define AP_FREE_COUNTER_HI	0x5600200cu

uint64_t dev_cnts_get_ap_free_counter(void)
{
	uint32_t hi, lo, hi2;
	int n = 11;

	for (;;) {
		hi = REG32(AP_FREE_COUNTER_HI);
		lo = REG32(AP_FREE_COUNTER_LO);
		if (--n == 0)
			break;
		hi2 = REG32(AP_FREE_COUNTER_HI);
		if (hi != lo && hi == hi2)
			break;
	}
	return (uint64_t)hi << 32 | lo;
}
