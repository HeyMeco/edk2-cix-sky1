/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Services (service/) and the top level (framework/main.c, service/main.c) */
#ifndef SERVICES_H
#define SERVICES_H

#include <stdbool.h>
#include <stdint.h>

#ifndef SE_FW_VERSION
#define SE_FW_VERSION		"beta2.0-open"
#endif

uint32_t fw_init(void);
uint32_t fw_log_init(void);
void print_bannar(void);
uint32_t fw_sec_debug_check(void);

uint32_t services_init(void);
uint32_t service_boot_entry(void);
uint32_t service_runtime_entry(void);
uint32_t service_secure_boot_entry(void);
uint32_t service_debug_entry(void);
uint32_t service_idle_entry(void);
uint32_t service_str_boot_entry(void);
void service_str_entry(void);
uint32_t service_crypto_entry(void);
uint32_t service_fuse_entry(void);
uint32_t service_memrepair_entry(void);

/* The request being served came in on the TEE mailbox */
bool ipc_request_from_tee(void);

#endif
