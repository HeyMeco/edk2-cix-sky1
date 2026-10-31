/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Boot flash: SE QSPI (driver/qspi.c) or FCH xSPI (driver/xspi.c) */
#ifndef QSPI_H
#define QSPI_H

#include <stdint.h>

typedef enum { QSPI_READ_1_1_1, QSPI_READ_1_1_2, QSPI_READ_1_1_4, QSPI_READ_JEDEC_ID } qspi_cmd_t;
typedef enum { QSPI_USER_SE, QSPI_USER_AP } qspi_user_t;

/* Per-flash read parameters from the bootloader1 header (cbff_img_header.qspi_config) */
typedef struct {
	uint32_t jedec_id;
	uint8_t qspi_bus_clk;
	uint8_t read_cmd;		/* qspi_cmd_t */
	uint8_t qspi_numdc;
	uint8_t qspi_rxdly;
	uint8_t read_cmd_opt;
	uint8_t qspi_numdc_opt;
	uint8_t reserved[3];
} __attribute__((aligned(4))) qspi_param_t;

uint32_t drv_qspi_flash_read(qspi_cmd_t cmd, uint32_t addr, void *data, uint32_t len);
uint32_t drv_qspi_flash_fast_read(const qspi_param_t *p, uint32_t addr, void *data, uint32_t len);
uint32_t drv_qspi_read_sr(uint8_t *sr);
uint32_t drv_qspi_flash_erase(uint32_t addr, uint32_t len);
uint32_t drv_qspi_flash_write(uint32_t addr, const uint8_t *data, uint32_t len);
uint32_t drv_qspi_init_sec_pa(uint8_t sec_no);
uint32_t drv_qspi_read_jedec_id(uint8_t *id);

uint32_t drv_xspi_init(void);
uint32_t drv_xspi_flash_read(uint32_t from, void *data, uint32_t len);
uint32_t hal_stor_qspi_user_switch(qspi_user_t user);

#endif
