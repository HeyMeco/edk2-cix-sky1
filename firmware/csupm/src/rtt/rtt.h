/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The PM's way into DRAM and the SEGGER-style RTT control block the AP
 * reads there (CIX's Source/common/tlbmgmt/tlb_ctrl.c,
 * Source/common/rtt/rtt.c, Source/common/utilities/u_rrq.c,
 * Source/projects/sky1/rtt/cfg_rtt.c).  CIX's types and names.
 *
 * The PM sees DRAM through the hub window at 0x60000000: eight 128 MB
 * windows, window i at 0x60000000 + 128 MB * i, each mapped to a 128 MB
 * aligned DRAM block by the LCRU's HUB_TLB registers (src/lcru.h), secure
 * or not.  tlb_ctrl hands the windows out by DRAM block.
 *
 * In DRAM (Sky1's layout, edk2-platforms' Sky1.dec and AcpiPlatform.h):
 *   0x83400000  RTT up channel 1, 1 MB (CIX: performance streams)
 *   0x83600000  RTT up channel 2, 32 KB (the PM's log, when the BIOS asks)
 *   0x83bf1280  PCC tables (src/pcc), through a secure window
 *   0x83bf3680  RTT control block; down channel 0 at 0x83bf3700 (256 bytes),
 *               up channel 0 at 0x83bf3800 (50 KB) (CIX: debug console)
 *   0x83db8000  RAM log, the previous boot's at 0x83dba000 (src/log)
 *   0x83e01180  firmware version (src/log/md_fwver.c)
 */
#ifndef RTT_H
#define RTT_H

#include "pm.h"

/* --- TLB windows (tlb_ctrl.c) ----------------------------------------------- */

#define TLB_CTRL_ENTRIES	8
#define HUB_WINDOW		0x60000000u
#define HUB_WINDOW_SHIFT	27		/* 128 MB windows */

typedef struct {
	uint64_t dram_base;		/* 128 MB aligned */
	uint32_t tlb_addr;		/* what HUB_TLB holds */
	uint32_t pm_base;		/* CIX's: always 0 */
	union {
		uint32_t tlb_attr;
		struct {
			uint32_t secure_bit : 1;
			uint32_t allocated_bit : 1;
			uint32_t locked_bit : 1;
		};
	};
} TLB_CTRL_TLB_ENTRY;

uint32_t tlb_ctrl_get_entry_idx_by_dram_base(uint64_t dram_base, bool axport_1);
uint32_t tlb_ctrl_dram_to_syshub_addr(uint64_t dram_addr, bool axport_1);
void tlb_ctrl_setup_entry(uint64_t dram_base, uint32_t tlb_idx, bool axport_1);
uint32_t tlb_ctrl_dram_to_syshub_by_specific_entry(uint64_t dram_addr, uint32_t tlb_idx,
						   bool axport_1);
bool tlb_ctrl_alloc_entry(uint64_t dram_base, bool axport_1);
void tlb_ctrl_lock_entry(uint32_t tlb_idx);
bool tlb_ctrl_is_entry_free(uint32_t tlb_idx);
void tlb_ctrl_free_entry(uint32_t tlb_idx);
/* The hub address of a DRAM address, a window allocated if none maps it;
 * 0 when none is free */
uint32_t tlb_ctrl_dram_to_syshub(uint64_t dram_addr, bool axport_1);
/* The windows again, after the LCRU lost them (resume) */
void tlb_ctrl_restore(void);

/* --- ring queues (u_rrq.c) -------------------------------------------------- */

#define U_RRQ_MAX		12
#define U_RRQ_INVALID		0xff

typedef enum {
	U_RRQ_E_NOLOCK = 0,
	U_RRQ_E_U_MUTEX = 1,
	U_RRQ_E_RTOS_SEMPH = 2,
	U_RRQ_E_CUSTOMIZED = 3,
} U_RRQ_LOCK_TYPE;

typedef bool (*U_RRQ_LOCKER)(bool lock, bool producer);

void u_rrq_init(void);
uint8_t u_rrq_setup(void *pBuf, uint32_t length, bool overrideOnFull, U_RRQ_LOCK_TYPE lockType);
void u_rrq_reset(uint8_t rrqId);
uint32_t u_rrq_put(uint8_t rrqId, const void *pBuf, uint32_t length);
uint32_t u_rrq_get(uint8_t rrqId, void *pBuf, uint32_t length);
uint32_t u_rrq_bytesInQueue(uint8_t rrqId);
uint32_t u_rrq_getHeadIdx(uint8_t rrqId);
uint32_t u_rrq_getTailIdx(uint8_t rrqId);
void u_rrq_SET_HeadIdx(uint8_t rrqId, uint32_t new_index);
void u_rrq_SET_TailIdx(uint8_t rrqId, uint32_t new_index);

/* --- RTT (rtt.c) -------------------------------------------------------------- */

#define RTT_UP_CHANNELS		3
#define RTT_DOWN_CHANNELS	1

/* The control block in DRAM, as the host sees it: offsets are DRAM
 * addresses */
typedef struct {
	uint32_t sNameJLINK;
	uint32_t pBufferJLINK;
	uint32_t SizeOfBuffer;
	uint32_t WrOff;
	volatile uint32_t RdOff;	/* the host moves it */
	uint32_t Flags;
} RTT_BUFFER_UP;

typedef struct {
	uint32_t sNameJLINK;
	uint32_t pBufferJLINK;
	uint32_t SizeOfBuffer;
	volatile uint32_t WrOff;	/* the host moves it */
	uint32_t RdOff;
	uint32_t Flags;
} RTT_BUFFER_DOWN;

typedef struct {
	char acID[16];
	uint32_t MaxNumUpBuffers;
	uint32_t MaxNumDownBuffers;
	RTT_BUFFER_UP aUp[RTT_UP_CHANNELS];
	RTT_BUFFER_DOWN aDown[RTT_DOWN_CHANNELS];
} RTT_CB;

/* A channel: its buffer in DRAM (base + offset) and as the PM sees it */
typedef struct {
	uintptr_t sNameJLINK;
	uintptr_t pBufferJLINKBase;
	uintptr_t pBufferPM;
	uint32_t Offset;
	uint32_t SizeOfBuffer;
	uint32_t Flags;
	bool isUp;
	int index;
} RTT_CHANNEL;

void RTT_Init(RTT_CB *p, const RTT_CHANNEL *channels, int NumOfChan);
uint32_t RTT_Write(uint32_t upChannel, const void *data, uint32_t len);
uint32_t RTT_Read(uint32_t downChannel, uint8_t *data, uint32_t len);
uint32_t RTT_BytesInQueue(uint32_t chID, bool isUp);
void RTT_Reset(uint32_t chID, bool isUp);

/* Sky1's channels and control block (cfg_rtt.c), at power-on */
void sky1_rtt_init(void);

#endif
