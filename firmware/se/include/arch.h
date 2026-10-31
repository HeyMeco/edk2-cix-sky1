/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Cortex-M33 core: system control space, interrupts, MPU */
#ifndef ARCH_H
#define ARCH_H

#include <stdint.h>

#define SE_IRQ_COUNT		255

#define SYST_CSR		0xe000e010u
#define SYST_RVR		0xe000e014u
#define SYST_CVR		0xe000e018u
#define NVIC_ISER		0xe000e100u
#define NVIC_ICER		0xe000e180u
#define NVIC_ISPR		0xe000e200u
#define NVIC_ICPR		0xe000e280u
#define NVIC_IPR		0xe000e400u
#define SCB_CPUID		0xe000ed00u
#define SCB_ICSR		0xe000ed04u
#define SCB_VTOR		0xe000ed08u
#define SCB_AIRCR		0xe000ed0cu
#define SCB_CCR			0xe000ed14u
#define SCB_SHPR		0xe000ed18u	/* SHPR1..3, byte per exception from 4 */
#define SCB_SHCSR		0xe000ed24u
#define SCB_CFSR		0xe000ed28u
#define SCB_HFSR		0xe000ed2cu
#define SCB_MMFAR		0xe000ed34u
#define SCB_BFAR		0xe000ed38u
#define SCB_CPACR		0xe000ed88u
#define MPU_TYPE		0xe000ed90u
#define MPU_CTRL		0xe000ed94u
#define MPU_RNR			0xe000ed98u
#define MPU_RBAR		0xe000ed9cu
#define MPU_RLAR		0xe000eda0u
#define MPU_MAIR0		0xe000edc0u
#define MPU_MAIR1		0xe000edc4u

void core_init(void);
void core_irq_register(int irq, void (*isr)(void *), void *ctx);
void core_irq_unregister(int irq);
void core_irq_enable(int irq);
void core_irq_disable(int irq);
void core_irq_set_priority(int irq, uint32_t prio);
void core_irq_clear(int irq);
uint32_t core_irq_save(void);
void core_irq_restore(uint32_t primask);

void sys_clk_init(void);
uint32_t sys_clk_get_systick_clk(void);
uint32_t sys_clk_get_systick_ratio(void);

#endif
