/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef RLOG_H
#define RLOG_H

#include <stdint.h>

typedef struct {
	void (*lock)(void);
	void (*unlock)(void);
	uint32_t (*get_tick)(void);
	void (*flush)(void);
} ramlog_ops;

void rlog_init(char *buf, uint32_t size, const ramlog_ops *ops);
void rlog_set_enable_disable(uint8_t enable);
uint8_t is_rlog_enable(void);

#endif
