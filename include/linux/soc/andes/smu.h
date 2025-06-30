/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 Andes Technology Corporation.
 */

#ifndef _LINUX_SOC_ANDES_SMU_H
#define _LINUX_SOC_ANDES_SMU_H

#include <linux/suspend.h>

/*
 * PCS0 --> Always on power domain, includes the JTAG tap and
 *          DMI_AHB bus in NCEJDTM200
 * PCS1 --> Power domain for debug subsystem
 * PCS2 --> Main power domain, includes the system bus and
 *          AHB, APB peripheral IPs
 * PCS3 --> Power domain for Core0 and L2C
 * PCSm --> Power domain for Core (m-3)
 */

#define PCS0_SCRATCH_OFF	0x84
#define PCS0_WE_OFF		0x90
#define PCS0_CTL_OFF		0x94
#define PCS0_STATUS_OFF		0x98

#define PCSm_SCRATCH_OFF(n)	((n + 3) * 0x20 + PCS0_SCRATCH_OFF)
#define PCSm_WE_OFF(n)		((n + 3) * 0x20 + PCS0_WE_OFF)
#define PCSm_STATUS_OFF(n)	((n + 3) * 0x20 + PCS0_STATUS_OFF)
#define PCSm_CTL_OFF(n)		((n + 3) * 0x20 + PCS0_CTL_OFF)

/* Clock related info */
#define ROOT_CLK_RATE	60000000
#define OSC_CLK_32K	32768
#define SPI_CLK_RATE	66000000
#define UART_CLK_RATE	20000000

#define SMU_CLK_ENABLE		0x20
#define SMU_CLK_ENABLE_CORE	0
#define SMU_CLK_ENABLE_AHB	1
#define SMU_CLK_ENABLE_APB	2
#define SMU_CLK_ENABLE_AXI	11

#define SMU_CLK_RATIO		0x24
#define SMU_CLK_RATIO_MASK	GENMASK(3, 1)

#define ENUM_TO_STR(x) case x: return __stringify(x)

enum ae350_clk {
	ROOT,
	CLK_32K,
	SPI,
	UART,
	CORE,
	AXI,
	AHB,
	APB,
	AHB_GATE,
	APB_GATE,
	MAXCLKS,
};

enum sbi_ext_susp_ae350_sleep_type {
	SBI_SUSP_AE350_LIGHT_SLEEP = 0x80000001,
	SBI_SUSP_AE350_DEEP_SLEEP,
};

struct atcsmu {
	void __iomem *base;
};

extern unsigned long *andes_wake_event;
extern void __iomem *atcsmu_get_address(void);

/* SMU per hart scratch reg could be used to store sleep type */
void atcsmu_set_sleep_type(unsigned long sleep_type);
void atcsmu_set_wake(unsigned long wake_event);

/* for watchdog */
#define FLASH_BASE                  0x80000000
#define SMUCR_OFF                   0x14
#define SMUCR_RESET                 0x3c
#define SMU_RESET_VEC_LO_OFF        0x50
#define SMU_RESET_VEC_HI_OFF        0x60
#define SMU_HART_RESET_VEC_LO(n)    (SMU_RESET_VEC_LO_OFF + (n * 0x4))
#define SMU_HART_RESET_VEC_HI(n)    (SMU_RESET_VEC_HI_OFF + (n * 0x4))
#define PCS_RESET                   0x1
#define RESET_CMD                   0x1

#endif
