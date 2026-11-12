/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Orion O6 EC firmware: shared declarations */
#ifndef EC_H
#define EC_H

#include <stdint.h>
#include "it5570.h"

#define VERSION_STRING	"V1.07-open"

/* ---- pins (Radxa Orion O6 V1.2 schematic, sheet 35) ---- */
#define PA 0
#define PB 1
#define PC 2
#define PD 3
#define PE 4
#define PF 5
#define PG 6
#define PH 7
#define PI 8
#define PJ 9

#define PIN_GET(reg, bit)	(((reg) >> (bit)) & 1)
#define PIN_HI(reg, bit)	((reg) |= (uint8_t)(1u << (bit)))
#define PIN_LO(reg, bit)	((reg) &= (uint8_t)~(1u << (bit)))

/* inputs */
#define PWRBTN_PRESSED()	(!PIN_GET(GPDRB, 3))	/* PWRBTN_L_HW */
#define SOC_SLP5()		PIN_GET(GPDRB, 5)	/* SoC SLP_S5_L, inverted */
#define SOC_SLP3()		PIN_GET(GPDRH, 0)	/* SoC SLP_S3_L, inverted */
#define SOC_RESET_OUT()		PIN_GET(GPDRD, 1)
#define LID_SW_L()		PIN_GET(GPDRC, 0)
#define SYS_ALW_PG()		PIN_GET(GPDRJ, 3)
#define SYS_S0_PG()		PIN_GET(GPDRH, 4)
#define SYS_S3_PG()		PIN_GET(GPDRH, 6)	/* DRAM power good */
#define THERMTRIP_IN()		PIN_GET(GPDRH, 2)
#define EC_CSU_INT()		PIN_GET(GPDRF, 5)

/* outputs */
#define EC_ALW_EN(v)		((v) ? PIN_HI(GPDRD, 7) : PIN_LO(GPDRD, 7))
#define S5_1V8_EN(v)		((v) ? PIN_HI(GPDRH, 5) : PIN_LO(GPDRH, 5))
#define RSM_RST(v)		((v) ? PIN_HI(GPDRE, 2) : PIN_LO(GPDRE, 2))
#define TP89(v)			((v) ? PIN_HI(GPDRJ, 5) : PIN_LO(GPDRJ, 5))
#define PWRBTN_EC(v)		((v) ? PIN_HI(GPDRJ, 4) : PIN_LO(GPDRJ, 4))
#define GPJ7(v)			((v) ? PIN_HI(GPDRJ, 7) : PIN_LO(GPDRJ, 7))
#define EC_SLP_S3_L(v)		((v) ? PIN_HI(GPDRD, 0) : PIN_LO(GPDRD, 0))
#define PMIC_EN_GATE_L(v)	((v) ? PIN_HI(GPDRH, 3) : PIN_LO(GPDRH, 3))
#define TP91(v)			((v) ? PIN_HI(GPDRE, 5) : PIN_LO(GPDRE, 5))
#define DEPOP_MUTE_L(v)		((v) ? PIN_HI(GPDRC, 6) : PIN_LO(GPDRC, 6))
#define EC_ACPI_INT(v)		((v) ? PIN_HI(GPDRD, 3) : PIN_LO(GPDRD, 3))
#define THERMTRIP_EC(v)		((v) ? PIN_HI(GPDRI, 7) : PIN_LO(GPDRI, 7))
#define EC_PWRON(v)		((v) ? PIN_HI(GPDRB, 4) : PIN_LO(GPDRB, 4))

/* ---- time ---- */
extern volatile uint16_t ms_now;	/* 1 ms tick (wraps) */
void delay_ms(uint16_t n);
#define after(t)		((int16_t)(ms_now - (t)) >= 0)

/* ---- uart ---- */
void uart_init(void);
void putch(char c);
void puts_(const char *s);
void puthex(uint8_t v);

/* ---- gpio ---- */
void gpio_init(void);

/* ---- power ---- */
enum { PS_G3, PS_S5, PS_S0, PS_SD, PS_STR };
extern uint8_t power_state;
extern uint8_t auto_power_on;		/* AC-loss policy: 1 = power on */
extern uint8_t poweroff_reason;
void power_init(void);
void power_1ms(void);
void power_2s(void);			/* wake-up interrupt: SoC signals, button */
void power_force_reset(void);
uint8_t power_busy(void);		/* host command 3E19 */

/* ---- events for the host (EC_ACPI_INT) ---- */
#define EVT_POWER_BUTTON	0x02
extern uint8_t host_events;
void host_event(uint8_t e);

/* ---- smbus ---- */
void smbus_init(void);
int8_t i2c_xfer(uint8_t ch, uint8_t addr, const __xdata uint8_t *wr, uint8_t wn,
		__xdata uint8_t *rd, uint8_t rn);

/* ---- host interface (SMBus B slave) ---- */
void host_init(void);
void host_isr(void);
void host_poll(void);
void host_poll_100ms(void);

/* ---- thermal / fan ---- */
extern uint8_t fan_mode;
extern uint8_t fan_duty;		/* percent */
extern uint8_t soc_temp;
extern uint8_t trip_flag;
void fan_init(void);
void fan_poll_2s(void);
void fan_resume(void);
void fan_s0(void);
void fan_set_duty(uint16_t v);
uint16_t fan_rpm(void);
extern uint8_t rpm_mode;
extern uint16_t rpm_target;
extern uint8_t rpm_target_hi;

/* ---- PD controllers ---- */
void pd_init_all(void);
void pd_init_all_status(void);
extern __xdata uint8_t pd_ver[2];
void pd_poll(void);
uint8_t pd_busy(void);
uint8_t pd_setting_up(void);

/* ---- e-flash ---- */
uint8_t eflash_read(uint32_t addr);
void eflash_write_byte(uint32_t addr, uint8_t v);
void eflash_write_reason(uint8_t r);
void mirror_reset(void);

/* ---- led ---- */
void led_1ms(void);

#endif
