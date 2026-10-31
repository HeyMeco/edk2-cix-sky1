/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The hub window's eight 128 MB windows into DRAM (CIX's
 * Source/common/tlbmgmt/tlb_ctrl.c).  An entry is allocated for a DRAM
 * block and a security (axport_1: secure); a locked entry stays as it is.
 * tlb_ctrl_unlock_entry, _free_entry_by_dram_base, _syshub_to_dram and
 * _save are not in CIX's image (unused) and not here.
 */
#include "log/log.h"
#include "lcru.h"
#include "rtt.h"

#define DRAM_BLOCK_MASK		0xfff8000000ull		/* 40-bit DRAM, 128 MB blocks */
#define DRAM_OFFSET_MASK	0x07ffffffu

static TLB_CTRL_TLB_ENTRY _s_tlb_entry[TLB_CTRL_ENTRIES];

static bool entry_maps(const TLB_CTRL_TLB_ENTRY *e, uint64_t dram_block, bool axport_1)
{
	return e->dram_base == dram_block && e->allocated_bit && e->secure_bit == axport_1;
}

uint32_t tlb_ctrl_get_entry_idx_by_dram_base(uint64_t dram_base, bool axport_1)
{
	uint32_t i;

	for (i = 0; i < TLB_CTRL_ENTRIES; i++) {
		if (entry_maps(&_s_tlb_entry[i], dram_base & DRAM_BLOCK_MASK, axport_1))
			break;
	}
	return i;
}

/* The hub address of dram_addr through an entry that maps it, else 0 */
uint32_t tlb_ctrl_dram_to_syshub_addr(uint64_t dram_addr, bool axport_1)
{
	uint32_t off = (uint32_t)dram_addr & DRAM_OFFSET_MASK;

	for (uint32_t i = 0; i < TLB_CTRL_ENTRIES; i++) {
		if (entry_maps(&_s_tlb_entry[i], dram_addr & DRAM_BLOCK_MASK, axport_1))
			return HUB_WINDOW + (i << HUB_WINDOW_SHIFT) + off;
	}
	return 0;
}

/* Window tlb_idx onto dram_base's block: HUB_TLB holds address bits
 * [39:27]; the window is made valid and, for axport_1, secure */
void tlb_ctrl_setup_entry(uint64_t dram_base, uint32_t tlb_idx, bool axport_1)
{
	uint64_t block = dram_base & DRAM_BLOCK_MASK;
	uint32_t tlb_addr = (uint32_t)(block >> HUB_WINDOW_SHIFT);
	uint32_t priv;

	if (tlb_idx >= TLB_CTRL_ENTRIES)
		vAssertCalledWithTRC(0xf17f0105);
	REG32(LCRU_HUB_TLB(tlb_idx)) = tlb_addr;
	priv = REG32(LCRU_HUB_TLB_PRIVATE);
	if (axport_1)
		priv |= 0x101u << tlb_idx;
	else
		priv = (priv & ~(0x100u << tlb_idx)) | (1u << tlb_idx);
	REG32(LCRU_HUB_TLB_PRIVATE) = priv;
	_s_tlb_entry[tlb_idx] = (TLB_CTRL_TLB_ENTRY){
		.dram_base = block,
		.tlb_addr = tlb_addr,
		.pm_base = 0,
		.secure_bit = axport_1,
		.allocated_bit = 1,
	};
}

/* Window tlb_idx onto dram_addr's block, whatever it showed (unless
 * locked); the hub address, 0 if the entry cannot be used */
uint32_t tlb_ctrl_dram_to_syshub_by_specific_entry(uint64_t dram_addr, uint32_t tlb_idx,
						   bool axport_1)
{
	if (tlb_idx >= TLB_CTRL_ENTRIES || _s_tlb_entry[tlb_idx].locked_bit)
		return 0;
	tlb_ctrl_setup_entry(dram_addr & DRAM_BLOCK_MASK, tlb_idx, axport_1);
	return ((uint32_t)dram_addr & DRAM_OFFSET_MASK) + (tlb_idx << HUB_WINDOW_SHIFT) + HUB_WINDOW;
}

/* An entry for dram_base's block: the one there is or the first free */
bool tlb_ctrl_alloc_entry(uint64_t dram_base, bool axport_1)
{
	if (tlb_ctrl_get_entry_idx_by_dram_base(dram_base, axport_1) < TLB_CTRL_ENTRIES)
		return true;
	for (uint32_t i = 0; i < TLB_CTRL_ENTRIES; i++) {
		if (!_s_tlb_entry[i].allocated_bit) {
			tlb_ctrl_setup_entry(dram_base, i, axport_1);
			return true;
		}
	}
	fwlog_write(FWLOG_WARN, "!!No free TLB entry!");
	return false;
}

void tlb_ctrl_lock_entry(uint32_t tlb_idx)
{
	if (tlb_idx >= TLB_CTRL_ENTRIES)
		vAssertCalledWithTRC(0xf17f0204);
	if (_s_tlb_entry[tlb_idx].allocated_bit)
		_s_tlb_entry[tlb_idx].locked_bit = 1;
}

bool tlb_ctrl_is_entry_free(uint32_t tlb_idx)
{
	if (tlb_idx >= TLB_CTRL_ENTRIES)
		vAssertCalledWithTRC(0xf17f0239);
	return !_s_tlb_entry[tlb_idx].allocated_bit;
}

/* The window off (unless locked) */
void tlb_ctrl_free_entry(uint32_t tlb_idx)
{
	if (tlb_idx >= TLB_CTRL_ENTRIES)
		vAssertCalledWithTRC(0xf17f0252);
	if (_s_tlb_entry[tlb_idx].locked_bit)
		return;
	REG32(LCRU_HUB_TLB(tlb_idx)) = 0;
	REG32(LCRU_HUB_TLB_PRIVATE) &= ~(0x101u << tlb_idx);
	memset(&_s_tlb_entry[tlb_idx], 0, sizeof(_s_tlb_entry[tlb_idx]));
}

uint32_t tlb_ctrl_dram_to_syshub(uint64_t dram_addr, bool axport_1)
{
	uint32_t addr = tlb_ctrl_dram_to_syshub_addr(dram_addr, axport_1);

	if (addr != 0)
		return addr;
	if (tlb_ctrl_alloc_entry(dram_addr, axport_1))
		return tlb_ctrl_dram_to_syshub_addr(dram_addr, axport_1);
	fwlog_write(FWLOG_WARN, "!!Failed to allocate TLB entry for dram_base %llx",
		    (unsigned long long)dram_addr);
	return 0;
}

void tlb_ctrl_restore(void)
{
	for (uint32_t i = 0; i < TLB_CTRL_ENTRIES; i++) {
		if (_s_tlb_entry[i].allocated_bit)
			tlb_ctrl_setup_entry(_s_tlb_entry[i].dram_base, i,
					     _s_tlb_entry[i].secure_bit);
	}
}
