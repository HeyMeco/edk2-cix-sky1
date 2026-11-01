/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * FreeRTOS on CSU_PM (src/rtos.c has the tasks, rtos/port the port).
 *
 * Co-operative: a task runs until it blocks or yields, so the code that
 * was written for SCP-firmware's single event loop (the framework, the
 * DPM, the board services) still runs to completion between the points
 * where a task waits.  Only the I2C workers wait in the middle of their
 * work (for a transfer); nothing else blocks.
 *
 * Interrupts stay deferred (arch/vectors.S, scp/src/arch_interrupt.c):
 * no FreeRTOS call is made from a vector, the tick included.  The tick
 * (CCOMPARE1, interrupt 7, as CIX's) only runs while a task waits with a
 * time-out (rtos/port/port.c).
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#define configUSE_PREEMPTION			0
#define configUSE_TIME_SLICING			0
#define configUSE_PORT_OPTIMISED_TASK_SELECTION	0
#define configUSE_TICKLESS_IDLE			0
#define configCPU_CLOCK_HZ			400000000u
#define configTICK_RATE_HZ			1000u
#define configMAX_PRIORITIES			5
#define configMINIMAL_STACK_SIZE		256u		/* words */
#define configMAX_TASK_NAME_LEN			8
#define configTICK_TYPE_WIDTH_IN_BITS		TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD			1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES	2
#define configQUEUE_REGISTRY_SIZE		0
#define configUSE_QUEUE_SETS			0
#define configUSE_NEWLIB_REENTRANT		0
#define configENABLE_BACKWARD_COMPATIBILITY	0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS	0
#define configUSE_MINI_LIST_ITEM		1
#define configSTACK_DEPTH_TYPE			uint32_t
#define configMESSAGE_BUFFER_LENGTH_TYPE	size_t
#define configHEAP_CLEAR_MEMORY_ON_FREE		0
#define configUSE_TASK_NOTIFICATIONS		1
#define configUSE_MUTEXES			1
#define configUSE_RECURSIVE_MUTEXES		0
#define configUSE_COUNTING_SEMAPHORES		0
#define configUSE_ALTERNATIVE_API		0
#define configUSE_CO_ROUTINES			0
#define configUSE_TIMERS			0
#define configUSE_EVENT_GROUPS			0
#define configUSE_STREAM_BUFFERS		0

/* all memory static (bss, below the DDR retention's borrowed blocks) */
#define configSUPPORT_STATIC_ALLOCATION		1
#define configSUPPORT_DYNAMIC_ALLOCATION	0
#define configKERNEL_PROVIDED_STATIC_MEMORY	0

#define configUSE_IDLE_HOOK			1
#define configUSE_TICK_HOOK			0
#define configUSE_MALLOC_FAILED_HOOK		0
#define configUSE_DAEMON_TASK_STARTUP_HOOK	0
#define configUSE_SB_COMPLETED_CALLBACK		0
/* the high-water marks of the stacks (filled with 0xa5 when created) */
#define configCHECK_FOR_STACK_OVERFLOW		2
#define configGENERATE_RUN_TIME_STATS		0
#define configUSE_TRACE_FACILITY		0
#define configUSE_STATS_FORMATTING_FUNCTIONS	0
#define configRECORD_STACK_HIGH_ADDRESS		1

#define INCLUDE_vTaskPrioritySet		0
#define INCLUDE_uxTaskPriorityGet		0
#define INCLUDE_vTaskDelete			0
#define INCLUDE_vTaskSuspend			0
#define INCLUDE_xTaskDelayUntil			0
#define INCLUDE_vTaskDelay			1
#define INCLUDE_xTaskGetSchedulerState		1
#define INCLUDE_xTaskGetCurrentTaskHandle	1
#define INCLUDE_uxTaskGetStackHighWaterMark	1
#define INCLUDE_xTaskGetIdleTaskHandle		0
#define INCLUDE_eTaskGetState			0
#define INCLUDE_xTaskAbortDelay			0
#define INCLUDE_xTaskGetHandle			0
#define INCLUDE_xTaskResumeFromISR		0

#ifndef __ASSEMBLER__
void rtos_assert_failed(const char *file, int line);
#endif
#define configASSERT(x)	do { if (!(x)) rtos_assert_failed(__FILE__, __LINE__); } while (0)

#endif
