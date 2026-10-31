/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The S3 boot script (CIX's Source/common/s3script/s3script.c): register
 * writes UEFI records (edk2-platforms' S3SaveStateDxe) and registers with
 * the PM at ExitBootServices (PM message 0x62: its DRAM address and
 * checksum); the PM runs it after each resume from suspend.
 */
#ifndef S3SCRIPT_H
#define S3SCRIPT_H

#include "pm.h"

bool s3script_register_table(uint32_t base_lo, uint32_t base_hi, uint32_t crc_lo, uint32_t crc_hi);
void s3script_unregister_table(void);
bool s3script_has_table(void);
bool s3script_check_table(void);
bool s3script_execute_table(void);

#endif
