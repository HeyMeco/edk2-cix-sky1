/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The SoC's hardware mutexes as CIX's u_mutex.c uses them: 100 words at
 * 0xa0000900; a mutex is free at 0, taken by writing one's id (and reading
 * it back), given back by writing the id again.
 */
#include "perf.h"

#define HW_MUTEX(i)		(PM_LOCAL + 0x900 + 4 * (i))
#define HW_MUTEXES		100

bool hw_mutex_try_lock(uint32_t mutex_idx, uint8_t entity_id)
{
	uint32_t v;

	if (mutex_idx >= HW_MUTEXES)
		return false;
	v = REG32(HW_MUTEX(mutex_idx));
	if (v == entity_id)
		return true;
	if (v)
		return false;
	REG32(HW_MUTEX(mutex_idx)) = entity_id;
	return REG32(HW_MUTEX(mutex_idx)) == entity_id;
}

bool hw_mutex_unlock(uint32_t mutex_idx, uint8_t entity_id)
{
	if (mutex_idx >= HW_MUTEXES || REG32(HW_MUTEX(mutex_idx)) != entity_id)
		return false;
	REG32(HW_MUTEX(mutex_idx)) = entity_id;
	return true;
}
