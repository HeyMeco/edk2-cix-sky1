/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * ACPI CPPC through PCC (CIX's Source/common/pcc/pcc.c): the OS reads each
 * CPU core's performance range and counters from a PCC subspace in DRAM
 * (0x83bf1280, through a secure hub window) and writes the levels it
 * wants there; it rings mailbox 0's fast channel 1 with a command.  The
 * subspace is an ACPI generic communications channel: signature, command,
 * status, then a PCC_CORE_REGS per DPM element (cores 0-11) in the shared
 * memory.  The levels are the DVFS levels of the core's cluster.
 *
 * CIX's PCC_CORE_REGS is accessed byte by byte, as a packed structure;
 * so is it here.
 */
#include "dpm/dpm.h"
#include "log/log.h"
#include "mbox.h"
#include "pcc.h"
#include "rtt/rtt.h"

/* CIX's signature: "PC" 0x40 0, where ACPI's would be 0x50434300 | the
 * subspace id */
#define PCC_SIGNATURE		0x50434000u

/* The command and status halves of the word at +4 */
#define PCC_CMD_UPDATE		0	/* counters and levels to the OS */
#define PCC_CMD_SET_DESIRED	1	/* the OS's desired levels to DVFS */
#define PCC_ST_CMD_CMPLT	BIT(16 + 0)
#define PCC_ST_ERROR		BIT(16 + 2)

#define PCC_CLUSTERS		5	/* EDP domains of the CPU clusters */
#define PCC_MAX_OPPS		16

typedef struct {
	uint32_t signature;
	volatile uint32_t cmd_stats;	/* ACPI_PCC_CMD command, ACPI_PCC_STATUS status */
	uint8_t shared_memory[1016];
} ACPI_PCC_SHM;

typedef struct __attribute__((packed)) {
	uint64_t referencePerfCounter;
	uint64_t deliveredPerfCounter;
	uint32_t desiredPerfLevel;
	uint32_t highestPerfLevel;
	uint32_t nominalPerfLevel;
	uint32_t lowestNonlinearPerfLevel;
	uint32_t lowestPerfLevel;
	uint32_t guaranteedPerfLevel;
	uint32_t perfLevelLimited;
	uint32_t referencePerf;
	uint32_t lowestFreq;		/* MHz */
	uint32_t nominalFreq;		/* MHz */
	uint32_t deliveredPerfLevel;
	uint32_t reserved[1];
} PCC_CORE_REGS;

_Static_assert(sizeof(PCC_CORE_REGS) == 64, "PCC_CORE_REGS");

/* CIX's dpm_ele_to_dxs_map (cfg_dpm.c), the CPU cores' part */
static const uint8_t dpm_ele_to_dxs_map[DPM_ELE_DSU] = {
	DPM_DxS_CORE_LIT, DPM_DxS_CORE_LIT, DPM_DxS_CORE_LIT, DPM_DxS_CORE_LIT,
	DPM_DxS_CORE_GM0, DPM_DxS_CORE_GM0, DPM_DxS_CORE_GM1, DPM_DxS_CORE_GM1,
	DPM_DxS_CORE_GB0, DPM_DxS_CORE_GB0, DPM_DxS_CORE_GB1, DPM_DxS_CORE_GB1,
};

/* CIX's dpm_elem_name, the CPU cores' part (for the log) */
static const char *const dpm_elem_name[DPM_ELE_DSU] = {
	"CPU_L0", "CPU_L1", "CPU_L2", "CPU_L3", "CPU_M0", "CPU_M1",
	"CPU_M2", "CPU_M3", "CPU_B0", "CPU_B1", "CPU_B2", "CPU_B3",
};

/* The OS's performance fast channels per cluster (EDP order: little,
 * medium 0 and 1, big 0 and 1): level set in mailbox 0, level get in
 * mailbox 1 (channel = DVFS element index) */
static const uint8_t cluster_fc[PCC_CLUSTERS] = {
	DVFS_ELEMENT_IDX_LITTLE, DVFS_ELEMENT_IDX_MID_G0, DVFS_ELEMENT_IDX_MID_G1,
	DVFS_ELEMENT_IDX_BIG_G0, DVFS_ELEMENT_IDX_BIG_G1,
};

static ACPI_PCC_SHM *_s_pcc_ptr;

static PCC_CORE_REGS *core_regs(uint32_t ele)
{
	return (PCC_CORE_REGS *)_s_pcc_ptr->shared_memory + ele;
}

/* What does not change: each core's levels (its cluster's lowest and
 * highest OPP; highest is also nominal and guaranteed), the reference
 * level, the lowest and nominal frequencies */
static void _update_pcc_constants(void)
{
	uint32_t opp_levels[PCC_MAX_OPPS], opp_num;

	if (_s_pcc_ptr == NULL)
		vAssertCalledWithTRC(0xf1550024);
	for (uint32_t edp = 0; edp < PCC_CLUSTERS; edp++) {
		for (uint32_t i = 0; i < dpm_edp_to_elem_map[edp].elem_num; i++) {
			uint32_t ele = dpm_edp_to_elem_map[edp].elem0 + i;
			uint32_t dxs = dpm_ele_to_dxs_map[ele];
			PCC_CORE_REGS *r = core_regs(ele);

			opp_num = PCC_MAX_OPPS;
			if (!copy_config_dvfs_opp_levels(dxs, opp_levels, &opp_num))
				continue;
			r->highestPerfLevel = opp_levels[opp_num - 1];
			r->nominalPerfLevel = opp_levels[opp_num - 1];
			r->lowestNonlinearPerfLevel = opp_levels[0];
			r->lowestPerfLevel = opp_levels[0];
			r->referencePerf = get_reference_perf_level(dxs);
			r->lowestFreq = get_dvfs_freq_from_level(dxs, opp_levels[0]) / 1000;
			r->nominalFreq = get_dvfs_freq_from_level(dxs, r->nominalPerfLevel) / 1000;
			fwlog_write(FWLOG_PCC,
				    "  PCC-C: elem[%2d] %-8s, level [%4d, %4d, %4d, %4d] ref %4d, low %4d Mhz, nom %4d Mhz",
				    (int)ele, dpm_elem_name[ele], (int)r->highestPerfLevel,
				    (int)r->nominalPerfLevel, (int)r->lowestNonlinearPerfLevel,
				    (int)r->lowestPerfLevel, (int)r->referencePerf, (int)r->lowestFreq,
				    (int)r->nominalFreq);
			r->guaranteedPerfLevel = r->nominalPerfLevel;
			r->perfLevelLimited = 0;
		}
	}
}

/* What changes: the AMU counters of the cores whose clock runs (0 for
 * the others), the cluster's level (from the OS's level-get fast channel)
 * as desired and delivered level */
static void _update_pcc_variables(void)
{
	uint32_t perf_level[PCC_CLUSTERS];

	if (_s_pcc_ptr == NULL)
		vAssertCalledWithTRC(0xf1550074);
	for (uint32_t edp = 0; edp < PCC_CLUSTERS; edp++)
		perf_level[edp] = REG32(MBOX_FAST(1, cluster_fc[edp]));
	for (uint32_t edp = 0; edp < PCC_CLUSTERS; edp++) {
		for (uint32_t i = 0; i < dpm_edp_to_elem_map[edp].elem_num; i++) {
			uint32_t ele = dpm_edp_to_elem_map[edp].elem0 + i;
			PCC_CORE_REGS *r = core_regs(ele);

			if (ap_is_clk_on(ele)) {
				r->referencePerfCounter = amu_read(ele, 1);
				r->deliveredPerfCounter = amu_read(ele, 0);
			} else {
				r->referencePerfCounter = 0;
				r->deliveredPerfCounter = 0;
			}
			r->desiredPerfLevel = perf_level[edp];
			r->guaranteedPerfLevel = r->nominalPerfLevel;
			r->perfLevelLimited = 0;
			r->deliveredPerfLevel = perf_level[edp];
		}
	}
	fwlog_write(FWLOG_PCC, "  PCC-R: lit %4d, gm0 %4d, gm1 %4d, gb0 %4d, gb1 %4d",
		    (int)perf_level[0], (int)perf_level[1], (int)perf_level[2], (int)perf_level[3],
		    (int)perf_level[4]);
}

/* Each cluster's highest desired level of its running cores to the OS's
 * level-set fast channel, when it is a level and a change */
static void set_desired_levels(void)
{
	uint32_t perf_level[PCC_CLUSTERS] = { 0 };
	uint32_t ps = log_lock();

	for (uint32_t edp = 0; edp < PCC_CLUSTERS; edp++) {
		uint32_t fc = MBOX_FAST(0, cluster_fc[edp]);
		uint32_t old = REG32(fc), max = 0;

		for (uint32_t i = 0; i < dpm_edp_to_elem_map[edp].elem_num; i++) {
			uint32_t ele = dpm_edp_to_elem_map[edp].elem0 + i;

			if (ap_is_clk_on(ele) && core_regs(ele)->desiredPerfLevel > max)
				max = core_regs(ele)->desiredPerfLevel;
		}
		if (max != 0 && max != old)
			REG32(fc) = max;
		perf_level[edp] = max;
	}
	fwlog_write(FWLOG_PCC, "  PCC-W: lit %4d, gm0 %4d, gm1 %4d, gb0 %4d, gb1 %4d",
		    (int)perf_level[0], (int)perf_level[1], (int)perf_level[2], (int)perf_level[3],
		    (int)perf_level[4]);
	log_unlock(ps);
}

/* The command done (or, for an unknown one, the error bit set and the
 * command not marked complete, as CIX's does) */
void pcc_cmd_handler(void)
{
	uint32_t cmd_stats, status;

	if (_s_pcc_ptr == NULL)
		vAssertCalledWithTRC(0xf1550177);
	cmd_stats = _s_pcc_ptr->cmd_stats;
	fwlog_write(FWLOG_PCC, "  PCC-0: cmd_stats [%08X] = %08X",
		    (unsigned int)(uintptr_t)&_s_pcc_ptr->cmd_stats, (unsigned int)cmd_stats);
	switch (cmd_stats & 0xff) {
	case PCC_CMD_UPDATE:
		_update_pcc_variables();
		status = PCC_ST_CMD_CMPLT;
		break;
	case PCC_CMD_SET_DESIRED:
		set_desired_levels();
		status = PCC_ST_CMD_CMPLT;
		break;
	default:
		status = PCC_ST_ERROR;
		break;
	}
	_s_pcc_ptr->cmd_stats = (cmd_stats & 0xffff) | status;
	fwlog_write(FWLOG_PCC, "  PCC-1: cmd_stats [%08X] = %08X",
		    (unsigned int)(uintptr_t)&_s_pcc_ptr->cmd_stats,
		    (unsigned int)((cmd_stats & 0xffff) | status));
	fwlog_write(FWLOG_PCC, "  PCC-2: readback %08X", (unsigned int)_s_pcc_ptr->cmd_stats);
}

void pcc_init(void)
{
	if (_s_pcc_ptr == NULL) {
		_s_pcc_ptr = (ACPI_PCC_SHM *)(uintptr_t)tlb_ctrl_dram_to_syshub(PCC_DRAM_BASE, true);
		tlb_ctrl_lock_entry(tlb_ctrl_get_entry_idx_by_dram_base(PCC_DRAM_BASE, true));
		_s_pcc_ptr->signature = PCC_SIGNATURE;
		_s_pcc_ptr->cmd_stats = 0;
		memset(_s_pcc_ptr->shared_memory, 0, sizeof(_s_pcc_ptr->shared_memory));
	}
	_update_pcc_constants();
	_update_pcc_variables();
	_s_pcc_ptr->cmd_stats |= PCC_ST_CMD_CMPLT;
}
