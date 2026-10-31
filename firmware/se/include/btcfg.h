/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Boot-time configuration items (framework/boot_time_config.c) */
#ifndef BTCFG_H
#define BTCFG_H

#include <stdint.h>

typedef struct {
	char name[12];
	uint32_t value;
} btcfg_item_t;

/* "ap.coreid", "se.pblbde", "pb.tfabde", "se.memrbp"; handed to BL2 in the KM block */
extern btcfg_item_t mntr_boot_cfg_item_table[5];

uint32_t btcfg_get_value(const char *table, const char *item, uint32_t *value);
uint32_t btcfg_set_value(const char *table, const char *item, uint32_t value);

#endif
