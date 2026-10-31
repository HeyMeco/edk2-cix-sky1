/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * CIX's div_ctrl.c translated: the register side of the dividers, gates
 * and muxes of the clock tree.
 *
 * A field is given by its mask; dividers hold divisor - 1, except that
 * divisor bit positions 16 and up of a node's bitmap stand for "special"
 * divisors (17 is 100).  Registers of subsystems the boot straps say are
 * fused off are left alone (reg_pre_chk).
 */
#include "clk.h"

#include "plat.h"

/* True if reg belongs to a subsystem fused off in this package */
static bool reg_pre_chk(uint32_t reg)
{
	uint32_t blk = reg & 0x1fff0000;
	uint32_t b1, b2, b3;

	b1 = (plat_id_4 >> 8) & 0xff;
	b2 = (plat_id_4 >> 16) & 0xff;
	b3 = plat_id_4 >> 24;

	if ((reg & 0x1f000000) <= 0x14ffffff && blk >= 0x14270000 &&
	    (plat_id_0 & 0x60000) == 0x20000 && !(b2 & 3))
		return true;
	if (blk >= 0x14250000 && (reg & 0x1ff00000) <= 0x1426ffff)
		return !(b2 & 0xc);
	if (blk == 0x09310000)
		return !(b1 & 6);
	if (blk - 0x0c000000 <= 0x60000 || blk - 0x0c0a0000 <= 0x60000)
		return (b3 >> 1) & 1;
	return false;
}

static uint32_t reg_read32(uint32_t addr)
{
	if (is_smn_space(addr))
		return SocRegRead32(addr);
	return REG32(addr);
}

static void reg_write32(uint32_t addr, uint32_t val)
{
	if (is_smn_space(addr))
		SocRegWrite32(addr, val);
	else
		REG32(addr) = val;
}

void div_ctrl_set_divisor(uint32_t div_reg, uint32_t div_mask, uint32_t div)
{
	uint32_t val = div ? div - 1 : 0;
	bool absent = reg_pre_chk(div_reg);
	uint32_t shift, v;

	if (div_mask == 0 || div_reg == 0 || absent)
		return;
	shift = u_misc_u32lowestOneBit(div_mask, 0);
	CLK_ASSERT((div_mask >> shift) >= val, 0xf1570070);

	smn_rcsu_lock(div_reg);
	v = reg_read32(div_reg);
	reg_write32(div_reg, (v & ~div_mask) | ((val << shift) & div_mask));
	smn_rcsu_unlock(div_reg);
}

uint32_t div_ctrl_get_divisor(uint32_t div_reg, uint32_t div_mask)
{
	uint32_t shift;

	if (reg_pre_chk(div_reg) || div_reg == 0 || div_mask == 0)
		return 1;
	shift = u_misc_u32lowestOneBit(div_mask, 0);
	return ((reg_read32(div_reg) & div_mask) >> shift) + 1;
}

uint32_t div_ctrl_get_special_div(uint32_t divisor)
{
	CLK_ASSERT(divisor > 16, 0xf1570105);
	return divisor == 17 ? 100 : divisor;
}

/* The largest divisor in the bitmap (bit n is divisor n + 1) */
uint32_t div_ctrl_get_maxinum_div(uint32_t supported_divisors_bitmap)
{
	uint32_t div;

	CLK_ASSERT(supported_divisors_bitmap != 0, 0xf1570119);
	for (div = 32; div >= 1; div--) {
		if (supported_divisors_bitmap & BIT(div - 1))
			return div >= 17 ? div_ctrl_get_special_div(div) : div;
	}
	return 1;
}

/*
 * round_policy 0: the closest output at or below desired, 1: at or above,
 * else (or if there is none) the closest overall.
 */
uint32_t div_ctrl_calc_best_divisor(uint32_t input_freq, uint32_t desired_freq,
				    uint32_t round_policy, uint32_t supported_divisors_bitmap)
{
	uint32_t up_div = 0, up_err = ~0u, down_div = 0, down_err = ~0u;
	uint32_t near_div, near_err = ~0u;
	uint32_t pos = 0, bit, div, freq, err;

	CLK_ASSERT(supported_divisors_bitmap != 0, 0xf1570136);
	near_div = u_misc_u32lowestOneBit(supported_divisors_bitmap, 0) + 1;

	while (pos < 32) {
		bit = u_misc_u32lowestOneBit(supported_divisors_bitmap, pos);
		if (bit & 0x80)
			break;
		pos = bit + 1;
		div = bit >= 16 ? div_ctrl_get_special_div(pos) : pos;
		freq = input_freq / div;
		err = freq > desired_freq ? freq - desired_freq : desired_freq - freq;
		if (freq >= desired_freq && err <= up_err) {
			up_div = div;
			up_err = err;
		}
		if (freq <= desired_freq && err <= down_err) {
			down_div = div;
			down_err = err;
		}
		if (err < near_err) {
			near_div = div;
			near_err = err;
		}
	}

	if (round_policy == 1)
		return up_div ? up_div : near_div;
	if (round_policy == 0)
		return down_div ? down_div : near_div;
	return near_div;
}

/* Gates in 0x0c000300-0x0c060300 and 0x0c0a0300-0x0c100300 are active low */
static bool gate_is_inverted(uint32_t reg)
{
	return reg - 0x4c0a0300 < 0x60001 || reg - 0x4c000300 < 0x60001;
}

void gate_ctrl_set_state(uint32_t reg, uint32_t mask, bool state)
{
	bool set;
	uint32_t v;

	if (reg_pre_chk(reg) || reg == 0 || mask == 0)
		return;
	set = state ^ gate_is_inverted(reg);

	smn_rcsu_lock(reg);
	v = reg_read32(reg);
	reg_write32(reg, set ? v | mask : v & ~mask);
	smn_rcsu_unlock(reg);
}

bool gate_ctrl_get_state(uint32_t reg, uint32_t mask)
{
	bool on = true;

	if (reg_pre_chk(reg))
		return false;
	if (reg && mask)
		on = (reg_read32(reg) & mask) != 0;
	return on ^ gate_is_inverted(reg);
}

/*
 * With psc_reg_addr, the clock ramp request of the PLL's power supply
 * controller is raised right before the switch.
 */
void mux_ctrl_set_src_with_psc_ramp_up(uint32_t reg, uint32_t mask, uint32_t source_idx,
				       uint32_t psc_reg_addr)
{
	uint32_t shift, max, v;

	if (reg_pre_chk(reg))
		return;
	shift = u_misc_u32lowestOneBit(mask, 0) & 31;	/* SAR takes 5 bits */
	max = mask >> shift;
	CLK_ASSERT((max & (max + 1)) == 0, 0xf1570262);
	CLK_ASSERT(max >= source_idx, 0xf1570263);
	if (reg == 0 || mask == 0)
		return;

	v = (REG32(reg) & ~mask) | (source_idx << shift);
	if (psc_reg_addr) {
		uint32_t f = clk_lock();

		REG32(psc_reg_addr) = 1;
		REG32(reg) = v;
		clk_unlock(f);
	} else {
		REG32(reg) = v;
	}
}

void mux_ctrl_set_src(uint32_t reg, uint32_t mask, uint32_t source_idx)
{
	mux_ctrl_set_src_with_psc_ramp_up(reg, mask, source_idx, 0);
}

uint32_t mux_ctrl_get_src(uint32_t reg, uint32_t mask)
{
	uint32_t shift, src;

	if (reg_pre_chk(reg))
		return 0;
	shift = u_misc_u32lowestOneBit(mask, 0);
	if (reg == 0 || mask == 0)
		return 0;
	src = (REG32(reg) & mask) >> shift;
	CLK_ASSERT(src < 6, 0xf1570298);
	return src;
}
