/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Voltage/frequency table selection (CIX driver/vf_sel/drv_pvt.c).
 *
 * The memory configuration may name a V/F table; otherwise the die's PVT
 * (process) average picks it.  The average comes from the eFuse or, for
 * early dies without it, from CIX's table of measured dies by wafer and
 * position (g_soc_pvt_data, CIX data taken from their firmware as is).
 */
#include "se.h"
#include "efuse.h"
#include "memcfg.h"
#include "pvt.h"

#define PVT_ENTRIES	0xa6c

/* wafer[5:0], x[13:6], y[21:14], average[31:22] */
extern const uint32_t g_soc_pvt_data[PVT_ENTRIES];

static PVT_INFO_S pvt_info_s;

static uint32_t pvt_from_table(uint32_t wafer, uint32_t x, uint32_t y)
{
	uint32_t i, e;

	if (wafer == 0 && x == 0 && y == 0)
		return 0;
	for (i = 0; i < PVT_ENTRIES; i++) {
		e = g_soc_pvt_data[i];
		if ((e & 0x3f) == wafer && ((e >> 6) & 0xff) == x && ((e >> 14) & 0xff) == y)
			return e >> 22;
	}
	return 0;
}

uint32_t drv_pvt_get_vf_ver(uint32_t *major, uint32_t *minor)
{
	uint32_t m = 0, s = 0, wafer = 0, x = 0, y = 0, avg = 0, err;
	int configured = 0;

	memset(&pvt_info_s, 0, sizeof(pvt_info_s));
	if (major == NULL || minor == NULL)
		return 0xffffffff;
	err = drv_memcfg_get_vf_ver(&m, &s);
	if (err) {
		SE_ERR(err);
		return err;
	}
	hal_printf_level(LOG_INFO, "%s [%d]: major=%d, minor=%d \n", __func__, __LINE__, (int)m, (int)s);
	if (m != 0xff && s != 0xff) {
		*major = m;
		*minor = s;
		configured = 1;
	}
	err = sec_efuse_read(NULL, 0x1d5, 4, 5, &wafer);
	if (!err)
		err = sec_efuse_read(NULL, 0x1d6, 1, 9, &x);
	if (!err)
		err = sec_efuse_read(NULL, 0x1d7, 2, 9, &y);
	if (err) {
		SE_ERR(err);
		return err;
	}
	pvt_info_s.wafer = wafer;
	pvt_info_s.x_axis = x;
	pvt_info_s.y_axis = y;
	hal_printf_level(LOG_INFO, "%s [%d]: wafer_id=%d, x_axis=%d, y_axis=%d \n", __func__, __LINE__,
			 (int)wafer, (int)x, (int)y);
	err = sec_efuse_read(NULL, 0x308, 7, 10, &avg);
	if (err) {
		SE_ERR(err);
		return err;
	}
	if (avg == 0) {
		hal_printf_level(LOG_INFO, "%s [%d]: efuse pvt_average is empty \n", __func__, __LINE__);
		avg = pvt_from_table(wafer, x, y);
	} else {
		avg = avg * 11 / 10;
	}
	pvt_info_s.average = avg;
	hal_printf_level(LOG_INFO, "%s [%d]: pvt_average=%d \n", __func__, __LINE__, (int)avg);
	if (!configured) {
		if (avg >= 435) {
			*major = 4, *minor = 1;
		} else if (avg >= 415) {
			*major = 4, *minor = 0;
		} else if (avg >= 400) {
			*major = 4, *minor = 2;
		} else {
			*major = 1, *minor = 0;
		}
		if (wafer == 7 && x == 13 && y == 6) {
			*major = 4, *minor = 0;
			hal_printf_level(LOG_INFO, "%s [%d]: the chip is in the special list \n",
					 __func__, __LINE__);
		}
	}
	pvt_info_s.vf_m_ver = *major;
	pvt_info_s.vf_s_ver = *minor;
	return 0;
}

const PVT_INFO_S *drv_pvt_get_vf_info(void)
{
	return &pvt_info_s;
}
