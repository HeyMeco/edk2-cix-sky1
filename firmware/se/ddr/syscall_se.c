/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The DDR driver's interface to the rest of the SE firmware (CIX
 * driver/ddr/src/syscall/syscall_se.c): logging, delays, post codes, SMN
 * register access, eFuse, the memory configuration image in flash, GPIOs,
 * and the controllers' interrupts (the "init monitor" that catches each
 * channel's INIT_COMP while training runs).  Translated from CIX's SE
 * firmware debug build, function by function.
 */
#include "mem_types.h"
#include "se.h"
#include "arch.h"
#include "efuse.h"
#include "storage.h"

/* driver/gpio.c, driver/mem_config.c (memcfg.h has its own copy of the
 * memory configuration types) */
uint32_t drv_gpio_get_value(uint32_t index, uint32_t *value);
uint32_t drv_memcfg_flash_write(uint32_t offset, const uint8_t *buf, uint32_t len);

/*
 * CIX keeps the per-channel flags in one block: ddr_init_status[ch] is set
 * by the init-monitor interrupt, DdrcIrqFlag[ch] (right after it) says the
 * monitor is registered.
 */
static uint8_t ddr_init_status[4];
uint8_t DdrcIrqFlag[4];
/* the interrupt handlers' context: the channel number */
uint8_t ChannelId[4] = { 0, 1, 2, 3 };
/* each channel controller's interrupt (IRQn_Type; CIX builds with short enums) */
int16_t ddrc_irq_type[4] = { IRQn_Ddrc0, IRQn_Ddrc1, IRQn_Ddrc2, IRQn_Ddrc3 };

/* ddrc_init_st_hdr: init monitor interrupt: INIT_COMP of the channel */
static void ddrc_init_st_hdr(void *ctx)
{
	uint8_t ch = *(uint8_t *)ctx;

	if (MemDdrcRegRead(0x59c, ch) & 0x200) {	/* INT_STATUS: INIT_COMP */
		ddr_init_status[ch] = 1;
		MemDdrcRegWrite(0x5c0, ch, 0x200);	/* INT_ACK */
	}
}

/* log_print: the driver's messages, filtered by CheckLogFlag (flag 0
 * always prints); bit 14 of flag makes it an error.  Ten word arguments
 * at most. */
void log_print(uint32_t flag, const char *fmt, ...)
{
	uint32_t arg[10], i;
	va_list args;

	if (flag != 0 && !CheckLogFlag(flag))
		return;
	va_start(args, fmt);
	for (i = 0; i < 10; i++)
		arg[i] = va_arg(args, uint32_t);
	va_end(args);
	hal_printf_level((flag & 0x4000) ? LOG_ERR : LOG_INFO, fmt, arg[0], arg[1], arg[2], arg[3], arg[4],
			 arg[5], arg[6], arg[7], arg[8], arg[9]);
}

/* get_memcfg_img: the memory configuration image; true when read */
uint8_t get_memcfg_img(uint8_t *img, uint32_t *outlen)
{
	return hal_stor_get_sub_img(MEMORY_CONFIG_IMG_DATA_TYPE, img, outlen) == 0;
}

/* udelay */
void udelay(uint32_t microseconds)
{
	hal_udelay(microseconds);
}

/* postcode */
void postcode(uint32_t pc)
{
	hal_pc_trace(pc);
}

/* SmnRegWrite32: SMN address space, seen by the SE at 0x40000000 */
void SmnRegWrite32(uint32_t addr, uint32_t val)
{
	REG32(addr + 0x40000000) = val;
}

/* SmnRegRead32 */
uint32_t SmnRegRead32(uint32_t addr)
{
	return REG32(addr + 0x40000000);
}

/* efuse_get_ddr_info: the DDR eFuse field, ddr_config_size bytes; true when read */
uint8_t efuse_get_ddr_info(uint8_t *ddr_info, uint32_t ddr_config_size)
{
	return fw_efuse_get_ddr_config_info(ddr_info, ddr_config_size << 3) == 0;
}

/* efuse_get_sw_harveset: the harvesting word of the fuse SRAM (offset 0x4cc) */
uint32_t efuse_get_sw_harveset(void)
{
	return REG32(0xb0040000 + 0x4cc);
}

/* enable_ddr_init_monitor: unmask INIT_COMP (and master bit 13) of the
 * channel and take its interrupt */
void enable_ddr_init_monitor(uint8_t ch)
{
	if (DdrcIrqFlag[ch])
		return;
	MemDdrcRegWrite(0x578, ch, ~0x2000u);
	MemDdrcRegWrite(0x5e4, ch, ~0x200u);
	ddr_init_status[ch] = 0;
	core_irq_register(ddrc_irq_type[ch], ddrc_init_st_hdr, &ChannelId[ch]);
	core_irq_enable(ddrc_irq_type[ch]);
	DdrcIrqFlag[ch] = 1;
}

/* get_ddr_init_status: INIT_COMP seen since the last call */
uint8_t get_ddr_init_status(uint8_t ch)
{
	uint8_t ret = ddr_init_status[ch];

	if (ret)
		ddr_init_status[ch] = 0;
	return ret;
}

/* disable_ddr_init_monitor */
void disable_ddr_init_monitor(uint8_t ch)
{
	if (!DdrcIrqFlag[ch])
		return;
	core_irq_disable(ddrc_irq_type[ch]);
	core_irq_unregister(ddrc_irq_type[ch]);
	MemDdrcRegWrite(0x578, ch, 0);
	MemDdrcRegWrite(0x5e4, ch, 0);
	DdrcIrqFlag[ch] = 0;
}

/* sys_register_ddrc_irq: irq_handler on the interrupts of the channels in chmsk */
void sys_register_ddrc_irq(uint8_t chmsk, void *irq_handler)
{
	uint8_t ch;

	for (ch = 0; ch < 4; ch++) {
		if (chmsk >> ch & 1) {
			core_irq_register(ddrc_irq_type[ch], (void (*)(void *))irq_handler, &ChannelId[ch]);
			core_irq_enable(ddrc_irq_type[ch]);
		}
	}
}

/* get_soc_gpio: level of a SoC GPIO (board ID straps) */
uint8_t get_soc_gpio(uint8_t GpioIndex)
{
	uint32_t val;

	drv_gpio_get_value(GpioIndex, &val);
	return val & 1;
}

/* set_memcfg_img: write len bytes at offset of the memory configuration image */
void set_memcfg_img(uint8_t *img, uint32_t offset, uint32_t len)
{
	firmware_entry sub_img_info;

	hal_stor_get_firmware_entry(MEMORY_CONFIG_IMG_DATA_TYPE, &sub_img_info);
	drv_memcfg_flash_write(sub_img_info.img_addr + offset, img, len);
}
