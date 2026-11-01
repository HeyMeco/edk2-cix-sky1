/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The DDR driver's log filter (CIX driver/ddr/src/Lib/MemLogLib.c): a mask
 * of message classes log_print lets through, set from the memory
 * configuration's quick configuration.  Translated from CIX's SE firmware
 * debug build, function by function.
 */
#include "mem_types.h"

/* CIX keeps the saved filter and its valid flag together */
static uint32_t mCustomFilter = 0xffffffff;
static uint32_t mSavedFilter;
static uint32_t mSvFlag;

/* SaveMemLogFilter */
void SaveMemLogFilter(void)
{
	mSavedFilter = mCustomFilter;
	mSvFlag = 1;
}

/* RestoreMemLogFilter */
void RestoreMemLogFilter(void)
{
	if (mSvFlag)
		mCustomFilter = mSavedFilter;
}

/* SetMemLogFilter */
void SetMemLogFilter(uint32_t filter)
{
	mCustomFilter = filter;
}

/* CheckLogFlag */
bool CheckLogFlag(uint32_t flag)
{
	return (flag & mCustomFilter) != 0;
}
