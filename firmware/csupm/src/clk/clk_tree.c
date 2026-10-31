/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * CIX's clk_tree.c translated: the clock tree itself.
 *
 * clk_tree_st[] is linked into a tree (each node's TREE_NODE points back
 * at its CLK_TREE_STRUCT).  Enabling a clock counts a reference on it and
 * on every node above it; the nodes whose count leaves or reaches 0 are
 * switched in hardware top down (on) or bottom up (off), then the state
 * and rate of the subtree are read back from the registers.  A rate
 * request is tried on the node and, if it cannot make it, passed up the
 * tree scaled by the divider in between.  The rates a node can make are
 * listed for SCMI from its parents' ranges and its divisors.
 *
 * CIX's debug printing (clk_tree_print_layout, clk_tree_print_node) is
 * left out.
 */
#include <stdlib.h>
#include <string.h>

#include "clk.h"

/* clk_port.c */

/* pll_policy.c: PLLs are driven through their policy tasks */


#define PLL_REGS(pll)		((void *)(uintptr_t)(pll)->pll_base)

/* The divisors POSTDIV1 x POSTDIV2 (1..7 each) make, bit n for n + 1 */
#define PLL_POSTDIV_DIVISORS	0x1020c299aebffull

/* Rate limits of what a PLL is asked for */
#define PLL_FREQ_MIN		32700000u
#define PLL_FREQ_MAX		4000000000u
#define PLL_VCO_MIN		1600000000u

/* The NPU's clocks: bit 31 of this register is set above 800 MHz */
#define NPU_FREQ_SEL_REG	0x54250308u
#define NPU_FREQ_SEL_MASK	0x80000000u
#define NPU_FREQ_SEL_ABOVE	800000000u

/* Lock ids pll_policy puts on subtrees while it retunes a PLL */
#define PLL_LOCK_ID_MASK	0xffff0000u
#define PLL_LOCK_ID_TAG		0xf0c00000u

#define MUX_SRC_NUM		6

static uint32_t _clk_tree_nodes_num;
static uint32_t _clk_tree_max_depth;
static uint32_t vpu_clk_freqMhz;

static CLK_TREE_STRUCT *node_st(const TREE_NODE *node)
{
	return (CLK_TREE_STRUCT *)node->node_data;
}

/* --- building ------------------------------------------------------------ */

static void _tree_init_node(TREE_NODE *node, const void *data)
{
	memset(node, 0, offsetof(TREE_NODE, node_data));
	node->node_data = data;
}

/*
 * Link the table into a tree by parent_idx; every named node is a leaf
 * until a child names it.  Marks the nodes SCMI exports.  Returns the root.
 */
TREE_NODE *clk_tree_build(CLK_TREE_STRUCT *pSt, uint32_t length)
{
	CLK_TREE_STRUCT *p = pSt;
	uint32_t i, scmi, max_depth = 0;

	_clk_tree_nodes_num = length;
	_clk_tree_max_depth = 0;
	for (i = 0; i < CLK_TREE_MAX_IDX && i < length; i++, p++) {
		_tree_init_node(&p->clk_tree_node, p);
		if (i != 0 && p->name != NULL) {
			pSt[p->parent_idx].is_leaf = 0;
			p->is_leaf = 1;
			scmi = clk_nodes_search_for_scmi_index((CLK_TREE_ENUM_IDX)i);
			if (scmi != 0xffffffff) {
				p->scmi_idx = scmi;
				p->is_scmi_node = 1;
			} else {
				p->is_scmi_node = 0;
			}
			CLK_ASSERT(p->parent_idx < i, 0xf1560078);
			tree_add_child(&pSt[p->parent_idx].clk_tree_node, &p->clk_tree_node);
		}
		if (p->clk_tree_node.depth > max_depth)
			max_depth = p->clk_tree_node.depth;
	}
	_clk_tree_max_depth = max_depth + 1;
	return &pSt->clk_tree_node;
}

uint32_t clk_tree_nodes_num(void)
{
	return _clk_tree_nodes_num;
}

uint32_t clk_tree_max_depth(void)
{
	return _clk_tree_max_depth;
}

/* Count a reference up the tree for each node set to be on from start */
void clk_tree_calc_ref_counter(CLK_TREE_STRUCT *pSt, uint32_t length)
{
	TREE_NODE *n;
	uint32_t i;

	for (i = 0; i < CLK_TREE_MAX_IDX && i < length; i++, pSt++) {
		if (pSt->name == NULL || !pSt->set_ref_cnt)
			continue;
		for (n = &pSt->clk_tree_node; n; n = n->parent)
			node_st(n)->ref_count++;
	}
}

/* The eight pixel muxes are updated from their parent PLL */
bool is_pixel_mux_node(const TREE_NODE *node)
{
	return (uint16_t)(clk_nodeptr_to_idx(node) - CLK_TREE_PIXEL_MUX0) < 8;
}

/* --- rate lists ------------------------------------------------------------ */

/* Index of a rate within 5 Hz in the sorted array, -1 if none */
static int32_t _binary_serach_with_tolerance(const uint32_t *rates, uint32_t num, uint32_t rate)
{
	int32_t lo = 0, hi = (int32_t)num - 1, mid;

	while (lo <= hi) {
		mid = (lo + hi) / 2;
		if (tolerance_equal(rates[mid], rate))
			return mid;
		if (rate < rates[mid])
			hi = mid - 1;
		else
			lo = mid + 1;
	}
	return -1;
}

/* Insert a rate in the ascending array unless one within 5 Hz is there */
static void _insert_to_array_if_not_exists(uint32_t *rates_array, uint32_t *rates_num, uint32_t new_rate)
{
	uint32_t i = *rates_num;

	if (i == 0) {
		rates_array[0] = new_rate;
		*rates_num = 1;
		return;
	}
	if (_binary_serach_with_tolerance(rates_array, i, new_rate) >= 0)
		return;
	for (; i > 0 && new_rate < rates_array[i - 1]; i--)
		rates_array[i] = rates_array[i - 1];
	rates_array[i] = new_rate;
	(*rates_num)++;
}

/*
 * Add input_freq divided by each divisor of the bitmap (bit n for n + 1),
 * largest first; false once the array is full.  For dividers, divisors
 * past 16 are the special ones.
 */
static bool _merge_rates_array_for_discrete(uint32_t input_freq, uint64_t div_bitmap, uint32_t *rates_array,
					    uint32_t array_size, uint32_t *rates_num_in_array)
{
	uint32_t div;

	for (div = 64; div > 0; div--) {
		uint32_t d = div;

		if (!((div_bitmap >> (div - 1)) & 1))
			continue;
		if (d > 16)
			d = div_ctrl_get_special_div(d);
		if (*rates_num_in_array >= array_size)
			return false;
		_insert_to_array_if_not_exists(rates_array, rates_num_in_array, input_freq / d);
	}
	return true;
}

/* The same with plain divisors (PLL post dividers) */
static bool _merge_rates_array(uint32_t input_freq, uint64_t div_bitmap, uint32_t *rates_array,
			       uint32_t array_size, uint32_t *rates_num_in_array)
{
	uint32_t div;

	for (div = 64; div > 0; div--) {
		if (!((div_bitmap >> (div - 1)) & 1))
			continue;
		if (*rates_num_in_array >= array_size)
			return false;
		_insert_to_array_if_not_exists(rates_array, rates_num_in_array, input_freq / div);
	}
	return true;
}

/*
 * How many distinct rates the divisors make of a range swept by step (the
 * whole range in one step if step is 0): counted by building them when
 * there can be duplicates.
 */
static uint32_t _calc_rates_num_from_continuos_freq(uint32_t freq_min, uint32_t freq_max, uint32_t freq_step,
						    uint64_t div_bitmap)
{
	uint32_t bits, num, cnt = 0, f, *tmp;

	if (div_bitmap == 0)
		return 0;
	bits = u_misc_u64BitCount(div_bitmap);
	if (freq_step == 0) {
		if (freq_max == freq_min)
			return bits;
		freq_step = freq_max - freq_min;
	}
	num = ((freq_max - freq_min) / freq_step + 1) * bits;
	if (bits < 2 || bits >= num)
		return num;

	tmp = malloc(num * sizeof(uint32_t));
	CLK_ASSERT(tmp != NULL, 0xf1561689);
	for (f = freq_min; f < freq_max; f += freq_step)
		CLK_ASSERT(_merge_rates_array(f, div_bitmap, tmp, num, &cnt), 0xf1561693);
	CLK_ASSERT(_merge_rates_array(freq_max, div_bitmap, tmp, num, &cnt), 0xf1561696);
	free(tmp);
	return cnt;
}

/* The same for a list of input rates and divider divisors */
static uint32_t _calc_rates_num_from_discrete_array(uint32_t *rates_in_array, uint32_t rates_in_size,
						    uint64_t div_bitmap)
{
	uint32_t bits = u_misc_u64BitCount(div_bitmap);
	uint32_t num = bits * rates_in_size, cnt = 0, i, *tmp;

	if (rates_in_size < 2 || bits < 2)
		return num;

	tmp = malloc(num * sizeof(uint32_t));
	CLK_ASSERT(tmp != NULL, 0xf1561651);
	for (i = 0; i < rates_in_size; i++)
		CLK_ASSERT(_merge_rates_array_for_discrete(rates_in_array[i], div_bitmap, tmp, num, &cnt),
			   0xf1561655);
	free(tmp);
	return cnt;
}

/* The rates a divider (div_bitmap) makes of a range of input rates */
bool clk_tree_list_possible_rates_array_from_continuous(uint32_t freq_min, uint32_t freq_max, uint32_t freq_step,
							uint64_t div_bitmap, uint32_t **rates_out_array,
							uint32_t *rates_out_size)
{
	uint32_t num, cnt = 0, step, f;

	CLK_ASSERT(rates_out_array != NULL && rates_out_size != NULL, 0xf1561738);
	num = _calc_rates_num_from_continuos_freq(freq_min, freq_max, freq_step, div_bitmap);
	if (num == 0)
		return false;

	*rates_out_array = malloc(num * sizeof(uint32_t));
	step = freq_step ? freq_step : freq_max - freq_min;
	for (f = freq_min; f < freq_max; f += step) {
		if (!_merge_rates_array(f, div_bitmap, *rates_out_array, num, &cnt))
			goto fail;
	}
	if (!_merge_rates_array(freq_max, div_bitmap, *rates_out_array, num, &cnt))
		goto fail;
	*rates_out_size = cnt;
	return true;
fail:
	free(*rates_out_array);
	return false;
}

/* Add a parent's rates to a growing array, reallocating it as needed */
void merge_rates_with_relloc(uint32_t **rate_array, uint32_t *rate_used, uint32_t *rate_total,
			     CLK_RATES_DATA *rates_data_parent)
{
	CLK_RATES_DATA *r = rates_data_parent;
	uint32_t add = 0, min, max, step, f, i, *p;

	CLK_ASSERT(r != NULL, 0xf1561774);
	min = r->continuous.min;
	max = r->continuous.max;
	step = r->continuous.step;
	if (r->mode == CLK_RATES_TYPE_DISCRETE)
		add = r->discrete.rates_num;
	else if (r->mode == CLK_RATES_TYPE_CONINUTES)
		add = _calc_rates_num_from_continuos_freq(min, max, step, 1);

	if (*rate_total < *rate_used + add) {
		p = malloc((*rate_used + add) * sizeof(uint32_t));
		CLK_ASSERT(p != NULL, 0xf1561791);
		memcpy(p, *rate_array, *rate_used * sizeof(uint32_t));
		if (*rate_array)
			free(*rate_array);
		*rate_array = p;
		*rate_total = *rate_used + add;
	}

	if (r->mode == CLK_RATES_TYPE_CONINUTES) {
		for (f = min; f < max; f += step)
			_insert_to_array_if_not_exists(*rate_array, rate_used, f);
		_insert_to_array_if_not_exists(*rate_array, rate_used, max);
	}
	if (r->mode == CLK_RATES_TYPE_DISCRETE) {
		for (i = 0; i < r->discrete.rates_num; i++)
			_insert_to_array_if_not_exists(*rate_array, rate_used, r->discrete.rates_array[i]);
	}
}

/* The rates a divider (div_bitmap) makes of a list of input rates */
bool clk_tree_list_possible_rates_array_from_discrete(uint32_t *rates_in_array, uint32_t rates_in_size,
						      uint64_t div_bitmap, uint32_t **rates_out_array,
						      uint32_t *rates_out_size)
{
	uint32_t num, cnt = 0, i;

	CLK_ASSERT(rates_out_size != NULL && rates_in_array != NULL && rates_in_size != 0 &&
		   rates_out_array != NULL, 0xf1561711);
	if (div_bitmap == 0)
		return false;
	num = _calc_rates_num_from_discrete_array(rates_in_array, rates_in_size, div_bitmap);
	if (num == 0)
		return false;

	*rates_out_array = malloc(num * sizeof(uint32_t));
	for (i = 0; i < rates_in_size; i++) {
		if (!_merge_rates_array_for_discrete(rates_in_array[i], div_bitmap, *rates_out_array, num, &cnt)) {
			free(*rates_out_array);
			return false;
		}
	}
	*rates_out_size = cnt;
	return true;
}

/*
 * The rates a node can make: a range (PLLs) or a sorted list (dividers,
 * muxes).  A range above 4 GHz is given in kHz (is_khz); its children's
 * rates go back to Hz when they fit.
 */
bool clk_tree_enum_rates(TREE_NODE *node, CLK_RATES_DATA *rates_data)
{
	CLK_RATES_DATA parent = { .is_khz = 0 };
	CLK_TREE_STRUCT *pSt, *up, *src;
	CLK_PLL_ATTR *pll;
	uint64_t bitmap = 0, v;
	uint32_t n, i, *arr = NULL, used = 0, total = 0;
	bool ok;

	if (node == NULL || rates_data == NULL)
		return false;
	pSt = node_st(node);
	rates_data->is_khz = 0;

	switch (pSt->type) {
	case CLK_TYPE_ROOT:
		if (pSt->actual_freq) {
			rates_data->mode = CLK_RATES_TYPE_DISCRETE;
			rates_data->discrete.rates_array = malloc(sizeof(uint32_t));
			rates_data->discrete.rates_array[0] = pSt->actual_freq;
			rates_data->discrete.rates_num = 1;
			ok = true;
			break;
		}
		/* fall through */
	case CLK_TYPE_GATE:
		if (!clk_tree_enum_rates(node->parent, &parent))
			return false;
		*rates_data = parent;
		ok = true;
		break;

	case CLK_TYPE_PLL:
		pll = pSt->pll;
		rates_data->mode = CLK_RATES_TYPE_CONINUTES;
		if (pll->is_static) {
			CLK_ASSERT(pll->pll_base != 0, 0xf1561843);
			n = pll_ctrl_get_postdiv_freq(PLL_REGS(pll));
			rates_data->continuous.min = n;
			rates_data->continuous.max = n;
			rates_data->continuous.step = 0;
		} else {
			rates_data->continuous.min = pll->freq_min;
			rates_data->continuous.max = pll->freq_max;
			rates_data->continuous.step = pll->freq_step;
		}
		ok = true;
		break;

	case CLK_TYPE_PLL_VCO:
		pll = pSt->pll;
		rates_data->mode = CLK_RATES_TYPE_CONINUTES;
		if (pll->is_static) {
			CLK_ASSERT(pll->pll_base != 0, 0xf1561867);
			v = pll_ctrl_get_vco_freq(PLL_REGS(pll));
			if (v >> 32) {
				rates_data->is_khz = 1;
				v /= 1000;
			}
			rates_data->continuous.min = (uint32_t)v;
			rates_data->continuous.max = (uint32_t)v;
			rates_data->continuous.step = 0;
		} else {
			uint32_t min = pll->freq_min, max = pll->freq_max;

			if (pll->freq_max_above_4G || pll->freq_min_above_4G) {
				rates_data->is_khz = 1;
				max = (((uint64_t)pll->freq_max_above_4G << 32) | max) / 1000;
				min = (((uint64_t)pll->freq_min_above_4G << 32) | min) / 1000;
			}
			rates_data->continuous.min = min;
			rates_data->continuous.max = max;
			rates_data->continuous.step = pll->freq_step;
		}
		ok = true;
		break;

	case CLK_TYPE_PLL_POSTDIV:
		if (clk_tree_enum_rates(node->parent, &parent)) {
			pll = pSt->pll;
			if (pll->is_static) {
				CLK_ASSERT(pll->pll_base != 0, 0xf1561896);
				n = pll_ctrl_get_postdiv_num(PLL_REGS(pll));
				bitmap = 1ull << (n ? n - 1 : 0);
			} else {
				CLK_ASSERT(parent.mode == CLK_RATES_TYPE_CONINUTES, 0xf1561905);
				bitmap = PLL_POSTDIV_DIVISORS;
			}
			ok = clk_tree_list_possible_rates_array_from_continuous(parent.continuous.min,
					parent.continuous.max, parent.continuous.step, bitmap,
					&rates_data->discrete.rates_array, &rates_data->discrete.rates_num);
			rates_data->mode = CLK_RATES_TYPE_DISCRETE;
			break;
		}
		/* CIX asks the parent again, as for a divider */
		/* fall through */
	case CLK_TYPE_DIV:
	case CLK_TYPE_FIXED_DIV:
		if (!clk_tree_enum_rates(node->parent, &parent))
			return false;
		rates_data->mode = CLK_RATES_TYPE_DISCRETE;
		if (pSt->type == CLK_TYPE_FIXED_DIV) {
			n = pSt->fixed_div.div_factor;
			if (n <= 64)
				bitmap = n ? 1ull << (n - 1) : 1;
		} else if (pSt->type == CLK_TYPE_DIV) {
			bitmap = pSt->duty_div.div_bitmap;
			if (pSt->duty_div.is_static)
				bitmap = 1ull << (div_ctrl_get_divisor(pSt->duty_div.div_reg,
								       pSt->duty_div.div_mask) - 1);
		}
		if (parent.mode == CLK_RATES_TYPE_DISCRETE) {
			ok = clk_tree_list_possible_rates_array_from_discrete(parent.discrete.rates_array,
					parent.discrete.rates_num, bitmap,
					&rates_data->discrete.rates_array, &rates_data->discrete.rates_num);
			free(parent.discrete.rates_array);
		} else if (parent.mode == CLK_RATES_TYPE_CONINUTES) {
			ok = clk_tree_list_possible_rates_array_from_continuous(parent.continuous.min,
					parent.continuous.max, parent.continuous.step, bitmap,
					&rates_data->discrete.rates_array, &rates_data->discrete.rates_num);
		} else {
			return false;
		}
		break;

	case CLK_TYPE_MUX:
		/* below a tunable PLL: whatever the PLL makes */
		up = node_st(node->parent);
		if (up->type == CLK_TYPE_PLL && !up->pll->is_static) {
			if (!clk_tree_enum_rates(node->parent, &parent))
				return false;
			*rates_data = parent;
			ok = true;
			break;
		}
		/* else the rates of all its sources */
		for (i = 0; i < MUX_SRC_NUM; i++) {
			if (!((pSt->mux.src_bitmap >> i) & 1))
				continue;
			src = clk_nodes_get(pSt->mux.src_clk[i]);
			CLK_ASSERT(src->clk_tree_node.node_data != NULL, 0xf1562012);
			if (clk_tree_enum_rates(&src->clk_tree_node, &parent))
				merge_rates_with_relloc(&arr, &used, &total, &parent);
			else
				clk_assert_failed(0xf1562018);
		}
		rates_data->discrete.rates_array = arr;
		rates_data->mode = CLK_RATES_TYPE_DISCRETE;
		rates_data->discrete.rates_num = used;
		ok = true;
		break;

	case CLK_TYPE_CUSTOM:
		ok = pSt->cus.opr(clk_op_emu_rates, pSt->cus.params, rates_data);
		break;

	default:
		return false;
	}

	/* rates from a kHz range: back to Hz if the largest fits */
	if (!parent.is_khz || !ok)
		return ok;
	rates_data->is_khz = 1;
	if (rates_data->mode != CLK_RATES_TYPE_CONINUTES) {
		n = rates_data->discrete.rates_num;
		if (n == 0 || (uint64_t)rates_data->discrete.rates_array[n - 1] * 1000 > UINT32_MAX)
			return ok;
		for (i = 0; i < rates_data->discrete.rates_num; i++)
			rates_data->discrete.rates_array[i] *= 1000;
	} else {
		if ((uint64_t)rates_data->continuous.max * 1000 > UINT32_MAX ||
		    (uint64_t)rates_data->continuous.min * 1000 > UINT32_MAX)
			return ok;
		rates_data->continuous.min *= 1000;
		rates_data->continuous.max *= 1000;
	}
	rates_data->is_khz = 0;
	return ok;
}

/* --- state ------------------------------------------------------------------ */

static bool clk_tree_get_state(const TREE_NODE *node)
{
	return node_st(node)->actual_state;
}

/*
 * Turn a PLL on or off: the pixel PLLs directly (they have no references),
 * the others through the tree.
 */
bool pll_set_stat(bool stat, CLK_TREE_STRUCT *pSt)
{
	if ((uint16_t)(clk_struct_to_idx(pSt) - CLK_TREE_PIXEL_PLL0) >= 5)
		return clk_tree_set_state(&pSt->clk_tree_node, stat);
	if (!g_enable_pll_policy) {
		pll_ctrl_set_state(stat, PLL_REGS(pSt->pll));
		return true;
	}
	return pll_policy_set_state(stat, clk_struct_to_idx(pSt));
}

/*
 * Set a node's desired state and switch it in hardware: PLLs, gates and
 * custom nodes are turned on or off (static ones only if
 * effect_for_static_node), a divider with a rate request pending gets its
 * divisor once its input runs.
 */
static void _set_state_of_node(TREE_NODE *node, void *data, bool effect_for_static_node)
{
	CLK_TREE_STRUCT *pSt, *parent;
	CLK_PLL_ATTR *pll;

	if (node == NULL || node->parent == NULL)
		return;
	parent = node_st(node->parent);
	pSt = node_st(node);
	pSt->desired_state = data ? 1 : 3;

	switch (pSt->type) {
	case CLK_TYPE_PLL:
	case CLK_TYPE_PLL_VCO:
		pll = pSt->pll;
		if (pll->is_static && !effect_for_static_node)
			return;
		if (pll->no_pllen || pll->pll_base == 0)
			return;
		if (g_enable_pll_policy)
			pll_policy_set_state(data != NULL, clk_struct_to_idx(pSt));
		else
			pll_ctrl_set_state(data != NULL, PLL_REGS(pll));
		break;
	case CLK_TYPE_DIV:
		if (!pSt->duty_div.has_pending_freq_req)
			return;
		if (pSt->duty_div.is_static && !effect_for_static_node)
			return;
		if (parent->actual_freq == 0 || data == NULL)
			return;
		pSt->duty_div.has_pending_freq_req = 0;
		div_ctrl_set_divisor(pSt->duty_div.div_reg, pSt->duty_div.div_mask,
				     div_ctrl_calc_best_divisor(parent->actual_freq, pSt->desired_freq,
								pSt->duty_div.round_mode,
								pSt->duty_div.div_bitmap));
		break;
	case CLK_TYPE_GATE:
		if (pSt->gate.is_static && !effect_for_static_node)
			return;
		gate_ctrl_set_state(pSt->gate.reg, pSt->gate.mask, data != NULL);
		break;
	case CLK_TYPE_CUSTOM:
		if (pSt->cus.is_static && !effect_for_static_node)
			return;
		if (pSt->cus.opr)
			pSt->cus.opr(clk_op_set_state, pSt->cus.params, data);
		break;
	default:
		break;
	}
}

/*
 * Take (enable) or drop a reference on a node and the nodes above it.  The
 * nodes from the topmost one whose count left or reached 0 down to the node
 * are switched; the subtree is then read back.  If the node did not end up
 * in the state asked for, an enable's references on that path are undone.
 */
bool clk_tree_set_state(TREE_NODE *node, bool enable)
{
	CLK_TREE_STRUCT *pSt, *top = NULL;
	TREE_NODE *path[10], *n;
	uint32_t num, i;

	for (n = node; n; n = n->parent) {
		pSt = node_st(n);
		if (enable) {
			if (pSt->ref_count == 0)
				top = pSt;
			pSt->ref_count++;
		} else {
			if (pSt->ref_count == 0)
				break;
			pSt->ref_count--;
			if (pSt->ref_count == 0)
				top = pSt;
		}
	}
	if (top == NULL)
		return true;

	num = node->depth - top->clk_tree_node.depth + 1;
	CLK_ASSERT(num <= 10, 0xf1561397);
	for (i = num, n = node; i > 0; n = n->parent)
		path[--i] = n;

	if (enable) {
		for (i = 0; i < num; i++)
			_set_state_of_node(path[i], (void *)1, false);
	} else {
		for (i = num; i > 0; i--)
			_set_state_of_node(path[i - 1], NULL, false);
	}

	n = &top->clk_tree_node;
	if (is_pixel_mux_node(n))
		n = n->parent;
	tree_traverse(n, clk_tree_calc_state_of_node);

	if (node_st(node)->actual_state == enable)
		return true;
	if (enable) {
		for (i = num; i > 0; i--) {
			pSt = node_st(path[i - 1]);
			if (pSt->ref_count)
				pSt->ref_count--;
		}
	}
	return false;
}

/*
 * Switch a mux to source mux_clkB_id.  A source that is off is kept on
 * for the switch (and settles a little after it); psc_reg_addr, if not 0,
 * is a PLL's clock ramp control to go through.
 */
bool clk_tree_mux_switch_with_psc_ramp_up(TREE_NODE *node, uint32_t mux_clkB_id, uint32_t psc_reg_addr)
{
	CLK_TREE_STRUCT *pSt = node_st(node), *src;
	uint32_t cur, both, f;

	CLK_ASSERT(mux_clkB_id < MUX_SRC_NUM && pSt->type == CLK_TYPE_MUX, 0xf1560616);
	cur = mux_ctrl_get_src(pSt->mux.reg, pSt->mux.mask);
	both = (1u << cur) | (1u << mux_clkB_id);
	CLK_ASSERT((pSt->mux.src_bitmap & both) == both, 0xf1560619);
	if (cur == mux_clkB_id)
		return true;

	src = clk_nodes_get(pSt->mux.src_clk[cur]);
	if (clk_tree_get_state(&src->clk_tree_node)) {
		mux_ctrl_set_src_with_psc_ramp_up(pSt->mux.reg, pSt->mux.mask, mux_clkB_id, psc_reg_addr);
		return true;
	}
	if (!pll_set_stat(true, src))
		return false;
	f = clk_tree_get_freq(&src->clk_tree_node);
	mux_ctrl_set_src_with_psc_ramp_up(pSt->mux.reg, pSt->mux.mask, mux_clkB_id, psc_reg_addr);
	if (f < 100000000)
		cc_delay(200);
	else if (f < 800000000)
		cc_delay(50);
	pll_set_stat(false, src);
	return true;
}

/* A node's rate: asked of custom nodes, else as last read back */
uint32_t clk_tree_get_freq(TREE_NODE *node)
{
	CLK_TREE_STRUCT *pSt = node_st(node);
	uint32_t freq = 0;

	if (pSt->type == CLK_TYPE_CUSTOM) {
		pSt->cus.opr(clk_op_get_freq, pSt->cus.params, &freq);
		return freq;
	}
	return pSt->actual_freq;
}

bool clk_tree_mux_switch(TREE_NODE *node, uint32_t mux_clkB_id)
{
	return clk_tree_mux_switch_with_psc_ramp_up(node, mux_clkB_id, 0);
}

/*
 * Record a node's hardware setting (rate, divisor, source, on/off) as the
 * one to restore, unless the node is not to be saved.  PLLs under their
 * policy keep their own.
 */
void clk_tree_save_as_por_setting(TREE_NODE *node)
{
	CLK_TREE_STRUCT *pSt;
	CLK_PLL_ATTR *pll;
	uint32_t v;

	if (node == NULL || node->parent == NULL)
		return;
	pSt = node_st(node);
	if (g_enable_pll_policy && pSt->type >= CLK_TYPE_PLL && pSt->type <= CLK_TYPE_PLL_POSTDIV)
		return;

	switch (pSt->type) {
	case CLK_TYPE_PLL:
	case CLK_TYPE_PLL_VCO:
	case CLK_TYPE_PLL_POSTDIV:
		pll = pSt->pll;
		if (pll->str_nosave || pll->pll_base == 0) {
			pll->por_force = 0;
			break;
		}
		pll->por_fvcoKHz = pSt->type == CLK_TYPE_PLL_POSTDIV ? 0 :
				   (uint32_t)(pll_ctrl_get_vco_freq(PLL_REGS(pll)) / 1000);
		pll->por_freq = pSt->type == CLK_TYPE_PLL_VCO ? 0 : pll_ctrl_get_postdiv_freq(PLL_REGS(pll));
		pll->por_state = pll_ctrl_get_state(PLL_REGS(pll)) != 0;
		pll->por_force = 1;
		break;
	case CLK_TYPE_DIV:
		if (pSt->duty_div.str_nosave || pSt->duty_div.div_reg == 0 || pSt->duty_div.div_mask == 0) {
			pSt->duty_div.por_force = 0;
			break;
		}
		pSt->duty_div.por_divcfg = div_ctrl_get_divisor(pSt->duty_div.div_reg, pSt->duty_div.div_mask);
		pSt->duty_div.por_force = 1;
		break;
	case CLK_TYPE_GATE:
		if (pSt->gate.str_nosave || pSt->gate.reg == 0 || pSt->gate.mask == 0) {
			pSt->gate.por_force = 0;
			break;
		}
		pSt->gate.por_state = gate_ctrl_get_state(pSt->gate.reg, pSt->gate.mask);
		pSt->gate.por_force = 1;
		break;
	case CLK_TYPE_MUX:
		if (pSt->mux.str_nosave || pSt->mux.reg == 0 || pSt->mux.mask == 0) {
			pSt->mux.por_force = 0;
			break;
		}
		pSt->mux.por_select = mux_ctrl_get_src(pSt->mux.reg, pSt->mux.mask);
		pSt->mux.por_force = 1;
		break;
	case CLK_TYPE_CUSTOM:
		if (pSt->cus.str_nosave || pSt->cus.opr == NULL) {
			pSt->cus.por_force = 0;
			break;
		}
		v = pSt->cus.por_state;
		pSt->cus.opr(clk_op_get_state, pSt->cus.params, &v);
		pSt->cus.por_state = v != 0;
		pSt->cus.opr(clk_op_get_freq, pSt->cus.params, &v);
		pSt->cus.por_freq = v;
		pSt->cus.por_force = 1;
		break;
	default:
		break;
	}
}

/* Put back what clk_tree_save_as_por_setting recorded, once */
void clk_tree_apply_and_clear_por_setting(TREE_NODE *node)
{
	CLK_TREE_STRUCT *pSt;
	CLK_PLL_ATTR *pll;
	uint64_t vco;
	bool ok;

	if (node == NULL || node->parent == NULL)
		return;
	pSt = node_st(node);

	switch (pSt->type) {
	case CLK_TYPE_PLL:
	case CLK_TYPE_PLL_VCO:
	case CLK_TYPE_PLL_POSTDIV:
		pll = pSt->pll;
		if (!pll->por_force || pll->pll_base == 0)
			return;
		vco = (uint64_t)pll->por_fvcoKHz * 1000;
		if (pll->por_freq && pll->por_fvcoKHz) {
			pSt->desired_freq = pSt->type == CLK_TYPE_PLL_VCO ? (uint32_t)vco : pll->por_freq;
			if (g_enable_pll_policy) {
				pll_policy_set_FoutVCO(vco, clk_struct_to_idx(pSt));
				pll_policy_set_FoutPOSTDIV(pll->por_freq, clk_struct_to_idx(pSt));
			} else {
				pll_ctrl_set_FoutVCO(vco, PLL_REGS(pll));
				pll_ctrl_set_FoutPOSTDIV(pll->por_freq, PLL_REGS(pll));
			}
		} else if (pll->por_freq) {
			pSt->desired_freq = pll->por_freq;
			if (g_enable_pll_policy) {
				if (pSt->type == CLK_TYPE_PLL_POSTDIV)
					pll_policy_set_FoutPOSTDIV(pll->por_freq, clk_struct_to_idx(pSt));
				else
					pll_policy_set_freq(pll->por_freq, clk_struct_to_idx(pSt));
			} else {
				if (pSt->type == CLK_TYPE_PLL_POSTDIV)
					pll_ctrl_set_FoutPOSTDIV(pll->por_freq, PLL_REGS(pll));
				else
					pll_ctrl_set_freq(pll->por_freq, PLL_REGS(pll));
			}
		} else if (pll->por_fvcoKHz) {
			pSt->desired_freq = (uint32_t)vco;
			if (g_enable_pll_policy)
				pll_policy_set_FoutVCO(vco, clk_struct_to_idx(pSt));
			else
				pll_ctrl_set_FoutVCO(vco, PLL_REGS(pll));
		}
		_set_state_of_node(node, (void *)(uintptr_t)pll->por_state, true);
		pll->por_force = 0;
		break;
	case CLK_TYPE_DIV:
		if (!pSt->duty_div.por_force)
			return;
		div_ctrl_set_divisor(pSt->duty_div.div_reg, pSt->duty_div.div_mask, pSt->duty_div.por_divcfg);
		pSt->duty_div.por_force = 0;
		break;
	case CLK_TYPE_GATE:
		if (!pSt->gate.por_force)
			return;
		gate_ctrl_set_state(pSt->gate.reg, pSt->gate.mask, pSt->gate.por_state);
		pSt->gate.por_force = 0;
		break;
	case CLK_TYPE_MUX:
		if (!pSt->mux.por_force)
			return;
		ok = clk_tree_mux_switch(node, pSt->mux.por_select);
		CLK_ASSERT(ok, 0xf1561047);
		pSt->mux.por_force = 0;
		break;
	case CLK_TYPE_CUSTOM:
		if (!pSt->cus.por_force || pSt->cus.opr == NULL)
			return;
		pSt->cus.opr(clk_op_set_state, pSt->cus.params, (void *)(uintptr_t)pSt->cus.por_state);
		if (pSt->cus.por_freq)
			pSt->cus.opr(clk_op_set_freq, pSt->cus.params, (void *)(uintptr_t)pSt->cus.por_freq);
		pSt->cus.por_force = 0;
		break;
	default:
		break;
	}
}

/*
 * Read a node's state and rate back from the hardware (a tree_traverse
 * visitor, parents before children).  A divider with a rate request
 * pending gets its divisor here once its input runs.  Changes of tracked
 * clocks are reported to the perf stream.
 */
void clk_tree_calc_state_of_node(TREE_NODE *node)
{
	CLK_TREE_STRUCT *pSt, *parent, *src;
	CLK_PLL_ATTR *pll;
	uint32_t old_freq, idx, d;
	uint64_t v;

	if (node == NULL || node->parent == NULL)
		return;
	pSt = node_st(node);
	parent = node_st(node->parent);
	old_freq = pSt->actual_freq;

	if (pSt->type != CLK_TYPE_MUX && pSt->type != CLK_TYPE_CUSTOM && !parent->actual_state) {
		/* no input */
		pSt->actual_freq = 0;
		pSt->actual_state = 0;
		goto track;
	}

	switch (pSt->type) {
	case CLK_TYPE_PLL:
	case CLK_TYPE_PLL_VCO:
	case CLK_TYPE_PLL_POSTDIV:
		pll = pSt->pll;
		if (pll->pll_base == 0)
			return;
		if (pll->no_pllen)
			pSt->actual_state = 1;
		else
			pSt->actual_state = pll_ctrl_get_state(PLL_REGS(pll)) != 0;
		if (!pSt->actual_state) {
			pSt->actual_freq = 0;
			pSt->actual_freq_above_4G = 0;
		} else if (pSt->type == CLK_TYPE_PLL_VCO) {
			v = pll_ctrl_get_vco_freq(PLL_REGS(pll));
			pSt->actual_freq = (uint32_t)v;
			pSt->actual_freq_above_4G = (v >> 32) != 0;
		} else {
			pSt->actual_freq = pll_ctrl_get_postdiv_freq(PLL_REGS(pll));
			/* the pixel PLLs divide once more after the PLL */
			idx = clk_struct_to_idx(pSt);
			if (idx >= CLK_TREE_PIXEL_PLL0 && idx <= CLK_TREE_PIXEL_PLL4) {
				d = div_ctrl_get_divisor(pll->pll_base + (idx == CLK_TREE_PIXEL_PLL4 ? 28 : 24), 15);
				if (d >= 2)
					pSt->actual_freq /= d;
			}
		}
		break;
	case CLK_TYPE_DIV:
		pSt->actual_state = parent->actual_state;
		if (pSt->duty_div.has_pending_freq_req && !pSt->duty_div.is_static && parent->actual_freq) {
			pSt->duty_div.has_pending_freq_req = 0;
			d = div_ctrl_calc_best_divisor(parent->actual_freq, pSt->desired_freq,
						       pSt->duty_div.round_mode, pSt->duty_div.div_bitmap);
			div_ctrl_set_divisor(pSt->duty_div.div_reg, pSt->duty_div.div_mask, d);
		} else {
			d = div_ctrl_get_divisor(pSt->duty_div.div_reg, pSt->duty_div.div_mask);
		}
		pSt->actual_freq = parent->actual_freq / d;
		break;
	case CLK_TYPE_FIXED_DIV:
		pSt->actual_state = parent->actual_state;
		d = pSt->fixed_div.div_factor;
		if (parent->type == CLK_TYPE_PLL_VCO) {
			/* the VCO may be above 4 GHz; rounded to nearest */
			uint32_t q, rem;

			v = (uint64_t)parent->actual_freq_above_4G << 32 | parent->actual_freq;
			q = (uint32_t)(v / d);
			rem = parent->actual_freq - d * q;
			pSt->actual_freq = q + (d < rem * 2);
		} else {
			pSt->actual_freq = parent->actual_freq / d;
		}
		break;
	case CLK_TYPE_GATE:
		pSt->actual_state = gate_ctrl_get_state(pSt->gate.reg, pSt->gate.mask);
		if (pSt->actual_state && parent->actual_state)
			pSt->actual_freq = parent->actual_freq;
		else
			pSt->actual_freq = 0;
		break;
	case CLK_TYPE_MUX:
		idx = mux_ctrl_get_src(pSt->mux.reg, pSt->mux.mask);
		CLK_ASSERT((pSt->mux.src_bitmap >> idx) & 1, 0xf1561304);
		src = clk_nodes_get(pSt->mux.src_clk[idx]);
		pSt->actual_state = src->actual_state;
		pSt->actual_freq = src->actual_freq;
		break;
	case CLK_TYPE_CUSTOM:
		pSt->cus.opr(clk_op_update_state, pSt->cus.params, NULL);
		break;
	default:
		/* fixed clocks: as their parent, the root's children as set */
		if (parent != clk_nodes_get(CLK_TREE_ROOT)) {
			pSt->actual_state = parent->actual_state;
			pSt->actual_freq = parent->actual_freq;
		}
		break;
	}

track:
	if (perf_stream_is_active() && old_freq != pSt->actual_freq && pSt->tracking_node)
		perf_stream_track_clk_change(clk_struct_to_idx(pSt), pSt->actual_freq / 1000);
}

/* List the rates of a node exported to SCMI into pSt->rates */
void clk_tree_list_rates_of_scmi_node(TREE_NODE *node)
{
	CLK_TREE_STRUCT *pSt;

	if (node == NULL || node->parent == NULL)
		return;
	pSt = node_st(node);
	if (!pSt->is_scmi_node) {
		pSt->rates = NULL;
		return;
	}
	if (pSt->rates == NULL) {
		pSt->rates = malloc(sizeof(CLK_RATES_DATA));
		CLK_ASSERT(pSt->rates != NULL, 0xf1561354);
	}
	if (!clk_tree_enum_rates(node, pSt->rates)) {
		free(pSt->rates);
		pSt->rates = NULL;
		clk_assert_failed(0xf1561359);
	}
	CLK_ASSERT(!pSt->rates->is_khz, 0xf1561364);
}

/* --- rates ------------------------------------------------------------------ */

/*
 * Move a mux to a source already running at freq, other than the one it
 * is on; failing that, whether its current source is at freq.
 */
static bool look_for_input_freq(TREE_NODE *node, uint32_t freq)
{
	CLK_TREE_STRUCT *pSt = node_st(node), *cur, *src;
	uint32_t i, best = MUX_SRC_NUM;

	if (pSt->type != CLK_TYPE_MUX)
		return false;
	cur = node_st(node->parent);
	for (i = 0; i < MUX_SRC_NUM; i++) {
		if (!((pSt->mux.src_bitmap >> i) & 1))
			continue;
		src = clk_nodes_get(pSt->mux.src_clk[i]);
		if (src == cur)
			best = i;
		else if (tolerance_equal(src->actual_freq, freq))
			return clk_tree_mux_switch(node, i);
	}
	if (best >= MUX_SRC_NUM || !clk_tree_mux_switch(node, best))
		return false;
	return tolerance_equal(cur->actual_freq, freq);
}

/* A mux that follows its parent above 32.7 MHz is left to the parent */
static bool set_mux_output_freq(TREE_NODE *node, uint32_t freq)
{
	CLK_TREE_STRUCT *pSt = node_st(node);

	if (freq >= PLL_FREQ_MIN && pSt->mux.adjust_with_parent && pSt->actual_freq >= PLL_FREQ_MIN)
		return false;
	return look_for_input_freq(node, freq);
}

/* Make freq at this node if it can (true if it did or already does) */
static bool _set_freq_of_node(TREE_NODE *node, uint32_t freq)
{
	CLK_TREE_STRUCT *pSt = node_st(node), *parent = node_st(node->parent);
	CLK_PLL_ATTR *pll = pSt->pll;
	uint32_t old = pSt->desired_freq;

	pSt->desired_freq = freq;
	if (tolerance_equal(freq, clk_tree_get_freq(node)))
		return true;

	switch (pSt->type) {
	case CLK_TYPE_PLL:
		if (pll->pll_base == 0 || pll->is_static)
			return false;
		if (freq < PLL_FREQ_MIN || freq > PLL_FREQ_MAX) {
			pSt->desired_freq = old;
			return false;
		}
		if (g_enable_pll_policy)
			pll_policy_set_freq(freq, clk_struct_to_idx(pSt));
		else
			pll_ctrl_set_freq(freq, PLL_REGS(pll));
		return true;
	case CLK_TYPE_PLL_VCO:
		if (pll->pll_base == 0 || freq < PLL_VCO_MIN || pll->is_static)
			return false;
		if (g_enable_pll_policy)
			pll_policy_set_FoutVCO(freq, clk_struct_to_idx(pSt));
		else
			pll_ctrl_set_FoutVCO(freq, PLL_REGS(pll));
		return true;
	case CLK_TYPE_PLL_POSTDIV:
		if (pll->pll_base == 0 || pll->is_static)
			return false;
		if (g_enable_pll_policy)
			pll_policy_set_FoutPOSTDIV(freq, clk_struct_to_idx(pSt));
		else
			pll_ctrl_set_FoutPOSTDIV(freq, PLL_REGS(pll));
		return true;
	case CLK_TYPE_DIV:
		if (pSt->duty_div.is_static)
			return false;
		if (parent->actual_freq) {
			div_ctrl_set_divisor(pSt->duty_div.div_reg, pSt->duty_div.div_mask,
					     div_ctrl_calc_best_divisor(parent->actual_freq, freq,
									pSt->duty_div.round_mode,
									pSt->duty_div.div_bitmap));
			pSt->duty_div.has_pending_freq_req = 0;
		} else {
			/* input off: slowest until it runs (_set_state_of_node) */
			div_ctrl_set_divisor(pSt->duty_div.div_reg, pSt->duty_div.div_mask,
					     div_ctrl_get_maxinum_div(pSt->duty_div.div_bitmap));
			pSt->duty_div.has_pending_freq_req = 1;
		}
		return true;
	case CLK_TYPE_MUX:
		return set_mux_output_freq(node, freq);
	case CLK_TYPE_CUSTOM:
		if (pSt->cus.opr == NULL || pSt->cus.is_static)
			return false;
		return pSt->cus.opr(clk_op_set_freq, pSt->cus.params, (void *)(uintptr_t)freq);
	default:
		return false;
	}
}

/*
 * Set a node's rate: on the node if it can make it, else up the tree, the
 * rate scaled by each static divider passed.  The subtree of the node that
 * took it is then read back.
 */
bool clk_tree_set_freq(TREE_NODE *node, uint32_t freq)
{
	uint32_t req = freq;
	CLK_TREE_STRUCT *pSt;
	uint32_t idx;

	for (; node && node->parent; node = node->parent) {
		if (_set_freq_of_node(node, freq))
			goto done;
		pSt = node_st(node);
		switch (pSt->type) {
		case CLK_TYPE_ROOT:
		case CLK_TYPE_GATE:
		case CLK_TYPE_MUX:
			break;
		case CLK_TYPE_PLL_POSTDIV:
			if (!pSt->pll->is_static)
				return false;
			freq *= pll_ctrl_get_postdiv_num(PLL_REGS(pSt->pll));
			break;
		case CLK_TYPE_DIV:
			if (!pSt->duty_div.is_static)
				return false;
			freq *= div_ctrl_get_divisor(pSt->duty_div.div_reg, pSt->duty_div.div_mask);
			break;
		case CLK_TYPE_FIXED_DIV:
			freq *= pSt->fixed_div.div_factor;
			break;
		default:
			return false;
		}
	}
	return false;

done:
	idx = clk_nodeptr_to_idx(node);
	if (idx == CLK_TREE_NPU_SYSCLK || idx == CLK_TREE_NPU_DIV)
		mux_ctrl_set_src(NPU_FREQ_SEL_REG, NPU_FREQ_SEL_MASK, req > NPU_FREQ_SEL_ABOVE);
	if (is_pixel_mux_node(node))
		node = node->parent;
	tree_traverse(node, clk_tree_calc_state_of_node);
	return true;
}

/* Read a subtree back from the hardware */
void clk_tree_state_refresh(uint32_t subtree)
{
	uint32_t f = clk_lock();
	CLK_TREE_STRUCT *pSt = clk_nodes_get((CLK_TREE_ENUM_IDX)(uint16_t)subtree);

	tree_traverse(&pSt->clk_tree_node, clk_tree_calc_state_of_node);
	clk_unlock(f);
}

/* Drop a reference on a node and those above it, without switching */
void clk_tree_decrease_ref_cnt(TREE_NODE *node)
{
	CLK_TREE_STRUCT *pSt;

	for (; node; node = node->parent) {
		pSt = node_st(node);
		if (pSt->ref_count == 0)
			break;
		pSt->ref_count--;
	}
}

/* --- for SCMI ------------------------------------------------------------------ */

static CLK_RATES_DATA *scmi_rates(TREE_NODE *node)
{
	CLK_TREE_STRUCT *pSt;

	if (node == NULL || node->parent == NULL)
		return NULL;
	pSt = node_st(node);
	if (!pSt->is_scmi_node)
		return NULL;
	return pSt->rates;
}

/* How many rates SCMI lists for a node (at least 1) */
uint32_t clk_tree_get_rates_num(TREE_NODE *node)
{
	CLK_RATES_DATA *r = scmi_rates(node);
	uint32_t num;

	if (r == NULL)
		return 0;
	if (r->mode != CLK_RATES_TYPE_CONINUTES)
		num = r->discrete.rates_num;
	else if (r->continuous.step)
		num = (r->continuous.max - r->continuous.min) / r->continuous.step + 1;
	else
		num = r->continuous.max != r->continuous.min ? 2 : 1;
	return num ? num : 1;
}

/* SCMI's rate_index-th rate of a node; past the 15th, the last */
uint32_t clk_tree_get_rate_from_index(TREE_NODE *node, uint32_t rate_index)
{
	CLK_RATES_DATA *r = scmi_rates(node);
	uint32_t f;

	if (r == NULL)
		return 0;
	if (r->mode != CLK_RATES_TYPE_CONINUTES) {
		if (rate_index >= r->discrete.rates_num || rate_index > 14)
			rate_index = r->discrete.rates_num - 1;
		return r->discrete.rates_array[rate_index];
	}
	if (r->continuous.step == 0)
		return r->continuous.min;
	f = r->continuous.min + r->continuous.step * rate_index;
	return f < r->continuous.max ? f : r->continuous.max;
}

/* A node's rates for SCMI: a range (step at least 5 Hz) or up to 16 rates */
bool clk_tree_get_rate_info(TREE_NODE *node, uint32_t *isContinue, uint32_t *min, uint32_t *max,
			    uint32_t *step, uint32_t *rate_count)
{
	CLK_RATES_DATA *r = scmi_rates(node);

	if (r == NULL)
		return false;
	if (r->mode != CLK_RATES_TYPE_CONINUTES) {
		if (isContinue)
			*isContinue = 0;
		if (step)
			*step = 0;
		if (min) {
			*min = r->discrete.rates_array[0];
			*max = r->discrete.rates_array[r->discrete.rates_num - 1];
		}
		if (rate_count)
			*rate_count = r->discrete.rates_num < 16 ? r->discrete.rates_num : 16;
		return true;
	}
	if (isContinue)
		*isContinue = 1;
	if (step)
		*step = r->continuous.step;
	if (min) {
		*min = r->continuous.min;
		*max = r->continuous.max;
	}
	if (rate_count)
		*rate_count = clk_tree_get_rates_num(node);
	if (*step < 5)
		*step = 5;
	return true;
}

/* --- the API ------------------------------------------------------------------ */

/*
 * Wait for a PLL policy task that has locked part of the subtree (it
 * retunes a PLL) to finish.  Returns with the clocks locked.
 */
static uint32_t clk_lock_wait_subtree(TREE_NODE *node, uint32_t code)
{
	uint32_t f = clk_lock(), id;

	while ((id = tree_is_any_node_locked(node)) != 0) {
		CLK_ASSERT((id & PLL_LOCK_ID_MASK) == PLL_LOCK_ID_TAG, code);
		clk_unlock(f);
		pll_policy_lock_entry(id & 0xffff);
		f = clk_lock();
		pll_policy_unlock_entry(id & 0xffff);
	}
	return f;
}

/* Tell the DDR frequency scaling whether the GPU, VPU, ISP and DPs allow it */
static void check_for_xpu_clk_update(TREE_NODE *node)
{
	uint32_t idx = clk_nodeptr_to_idx(node) & 0xffff;
	bool a, b;

	if ((idx & ~1u) == CLK_TREE_VPU_CLK)
		vpu_clk_freqMhz = ((CLK_TREE_STRUCT *)clk_nodes_get(CLK_TREE_VPU_APBCLK))->actual_freq / 1000000;
	if (!perf_soc_idle_switch_get_enable())
		return;
	if ((uint16_t)(idx - CLK_TREE_DP0_PIXEL0) < 10)
		dpdpu_trigger_ddr_dfs_compatible_checker();
	vpu_ddr_dfs_set_compatible(vpu_clk_freqMhz <= 480);
	if ((idx & ~3u) == CLK_TREE_ISP_A_DIV) {
		a = ((CLK_TREE_STRUCT *)clk_nodes_get(CLK_TREE_ISP_ACLK))->actual_freq < 1000000;
		b = ((CLK_TREE_STRUCT *)clk_nodes_get(CLK_TREE_ISP_SCLK))->actual_freq < 1000000;
		isp_ddr_dfs_set_compatible(a && b);
	} else if ((uint16_t)(idx - CLK_TREE_GPUCORE_PLL) < 3) {
		gpu_ddr_dfs_set_compatible(
			((CLK_TREE_STRUCT *)clk_nodes_get(CLK_TREE_GPU_CORE_CLK))->actual_freq < 1000000);
	}
}

bool clk_api_set_state(TREE_NODE *node, bool enable)
{
	uint32_t f = clk_lock_wait_subtree(node, 0xf1562499);
	bool ok = clk_tree_set_state(node, enable);

	check_for_xpu_clk_update(node);
	clk_unlock(f);
	return ok;
}

bool clk_api_set_freq(TREE_NODE *node, uint32_t freq)
{
	uint32_t f = clk_lock_wait_subtree(node, 0xf1562530);
	bool ok = clk_tree_set_freq(node, freq);

	check_for_xpu_clk_update(node);
	clk_unlock(f);
	return ok;
}

bool clk_api_get_state(TREE_NODE *node)
{
	uint32_t f = clk_lock();
	bool on = clk_tree_get_state(node);

	clk_unlock(f);
	return on;
}

/* For SCMI 3.0 (apply_to_scmi30) a node not running reports its desired rate */
uint32_t clk_api_get_freq(TREE_NODE *node, bool apply_to_scmi30)
{
	CLK_TREE_STRUCT *pSt = node_st(node);
	uint32_t freq, f;

	if (apply_to_scmi30)
		return pSt->actual_freq ? pSt->actual_freq : pSt->desired_freq;
	f = clk_lock();
	freq = clk_tree_get_freq(node);
	clk_unlock(f);
	return freq;
}

void clk_api_decrease_ref_cnt(TREE_NODE *node)
{
	uint32_t f = clk_lock();

	clk_tree_decrease_ref_cnt(node);
	clk_unlock(f);
}

/* Report a clock's changes to the perf stream; idx -1 with !enable: none */
bool clk_api_set_tracking(uint32_t clk_enum_idx, bool enable)
{
	CLK_TREE_STRUCT *pSt;
	uint32_t f;

	if (clk_enum_idx == 0xffffffff && !enable) {
		f = clk_lock();
		clk_nodes_clr_tracking_bit();
		clk_unlock(f);
		return true;
	}
	if (clk_enum_idx >= _clk_tree_nodes_num)
		return false;
	pSt = clk_nodes_get((CLK_TREE_ENUM_IDX)(uint16_t)clk_enum_idx);
	if (pSt == NULL)
		return false;
	f = clk_lock();
	pSt->tracking_node = enable;
	clk_unlock(f);
	return true;
}
