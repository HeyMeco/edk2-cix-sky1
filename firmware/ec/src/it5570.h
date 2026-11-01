/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * ITE IT5570E registers used by the Orion O6 EC firmware.  Names follow
 * ITE's datasheet naming; addresses as the vendor EC V1.07 image uses them
 * (cross-checked with public IT5570 register lists).
 */
#ifndef IT5570_H
#define IT5570_H

#include <stdint.h>

#define XREG(a)		(*(volatile __xdata uint8_t *)(a))

/* 8051 SFRs */
__sfr __at(0x80) P0;
__sfr __at(0x87) PCON;
__sfr __at(0x88) TCON;
__sfr __at(0x89) TMOD;
__sfr __at(0x8A) TL0;
__sfr __at(0x8B) TL1;
__sfr __at(0x8C) TH0;
__sfr __at(0x8D) TH1;
__sfr __at(0x90) P1;
__sfr __at(0xA8) IE;
__sfr __at(0xB8) IP;
__sbit __at(0x8C) TR0;
__sbit __at(0x8D) TF0;
__sbit __at(0x8E) TR1;
__sbit __at(0x8F) TF1;
__sbit __at(0xA9) ET0;
__sbit __at(0xAA) EX1;
__sbit __at(0xAB) ET1;
__sbit __at(0xAF) EA;

/* SMFI: flash follow mode */
#define ECINDAR0	XREG(0x103B)
#define ECINDAR1	XREG(0x103C)
#define ECINDAR2	XREG(0x103D)
#define ECINDAR3	XREG(0x103E)
#define ECINDDR		XREG(0x103F)
#define SMFI_1063	XREG(0x1063)	/* bit 0 set at boot; 0x18 before a mirror reset */

/* INTC */
#define ISR(n)		XREG((n) < 4 ? 0x1100 + (n) : 0x1114 + 4 * ((n) - 4))
#define IER(n)		XREG((n) < 4 ? 0x1104 + (n) : 0x1115 + 4 * ((n) - 4))
#define IELMR(n)	XREG((n) < 4 ? 0x1108 + (n) : 0x1116 + 4 * ((n) - 4))
#define IPOLR(n)	XREG((n) < 4 ? 0x110C + (n) : 0x1117 + 4 * ((n) - 4))
#define IVECT		XREG(0x1110)

/* GPIO */
#define GCR		XREG(0x1600)
#define GPDRA		XREG(0x1601)
#define GPDRB		XREG(0x1602)
#define GPDRC		XREG(0x1603)
#define GPDRD		XREG(0x1604)
#define GPDRE		XREG(0x1605)
#define GPDRF		XREG(0x1606)
#define GPDRG		XREG(0x1607)
#define GPDRH		XREG(0x1608)
#define GPDRI		XREG(0x1609)
#define GPDRJ		XREG(0x160A)
#define GPDRM		XREG(0x160D)
#define GPDR(port)	XREG(0x1601 + (port))		/* A = 0 .. J = 9 */
#define GPCR(port, bit)	XREG(0x1610 + 8 * (port) + (bit))

/* PWM */
#define C0CPRS		XREG(0x1800)
#define CTR0		XREG(0x1801)
#define DCR0		XREG(0x1802)
#define DCR1		XREG(0x1803)
#define DCR2		XREG(0x1804)
#define DCR3		XREG(0x1805)
#define PWMPOL		XREG(0x180A)
#define PCSSGL		XREG(0x180B)
#define PCSSGH		XREG(0x180C)
#define CR256PCSSG	XREG(0x180D)
#define PCSGR		XREG(0x180F)
#define F1TLRR		XREG(0x181E)
#define F1TMRR		XREG(0x181F)
#define ZTIER		XREG(0x1823)
#define C4CPRS		XREG(0x1827)
#define C4MCPRS		XREG(0x1828)
#define C6CPRS		XREG(0x182B)
#define C6MCPRS		XREG(0x182C)
#define C7CPRS		XREG(0x182D)
#define C7MCPRS		XREG(0x182E)
#define CTR1		XREG(0x1841)
#define CTR2		XREG(0x1842)
#define CTR3		XREG(0x1843)

/* ADC */
#define ADCSTS		XREG(0x1900)
#define ADCCFG		XREG(0x1901)
#define ADCCTL		XREG(0x1902)
#define VCH0CTL		XREG(0x1904)
#define KDCTL		XREG(0x1905)
#define VCH1CTL		XREG(0x1906)
#define VCH2CTL		XREG(0x1909)
#define VCH3CTL		XREG(0x190C)
#define VCH4CTL		XREG(0x1938)
#define VCH5CTL		XREG(0x193B)
#define ADCGCR		XREG(0x1955)

/* WUC */
#define WUEMR(g)	XREG((g) <= 4 ? 0x1B00 + (g) - 1 : (g) == 5 ? 0x1B0C : 0x1B10 + 4 * ((g) - 6))
#define WUESR(g)	XREG((g) <= 4 ? 0x1B04 + (g) - 1 : (g) == 5 ? 0x1B0D : 0x1B11 + 4 * ((g) - 6))

/* SMBus host channels: register offsets differ between A/B, C/D and E/F */
#define SMB_A		0
#define SMB_B		1
#define SMB_C		2
#define SMB_D		3
#define SMB_E		4
#define SMB_F		5

/* SMBus slave (channels A, B) */
#define RESLADRA	XREG(0x1C08)
#define SLDAA		XREG(0x1C09)
#define SLSTAA		XREG(0x1C0B)
#define SICRA		XREG(0x1C0C)
#define RESLADR2A	XREG(0x1C3F)
#define RESLADRB	XREG(0x1C19)
#define SLDAB		XREG(0x1C1A)
#define SLSTAB		XREG(0x1C1C)
#define SICRB		XREG(0x1C1D)
#define RESLADR2B	XREG(0x1C44)

/* External timer / watchdog */
#define ETWCFG		XREG(0x1F01)
#define EWDKEYR		XREG(0x1F07)

/* GCTRL */
#define RSTS		XREG(0x2006)

/* UART1 (16550) */
#define UART_RBR	XREG(0x2700)
#define UART_THR	XREG(0x2700)
#define UART_DLL	XREG(0x2700)
#define UART_IER	XREG(0x2701)
#define UART_DLM	XREG(0x2701)
#define UART_FCR	XREG(0x2702)
#define UART_LCR	XREG(0x2703)
#define UART_MCR	XREG(0x2704)
#define UART_LSR	XREG(0x2705)
#define UART_2708	XREG(0x2708)

#endif
