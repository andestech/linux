/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 Andes Technology Corporation.
 */

#ifndef _ASM_ANDES_SMU_H
#define _ASM_ANDES_SMU_H

#include <asm/sbi.h>
#define MAX_PCS_SLOT	7

#define PCS0_WE_OFF     0x90
#define PCS0_CTL_OFF    0x94
#define PCS0_STATUS_OFF 0x98

/*
 * PCS0 --> Always on power domain, includes the JTAG tap and
 *          DMI_AHB bus in NCEJDTM200
 * PCS1 --> Power domain for debug subsystem
 * PCS2 --> Main power domain, includes the system bus and
 *          AHB, APB peripheral IPs
 * PCS3 --> Power domain for Core0 and L2C
 * PCSN --> Power domain for Core (N-3)
 */

#define PCSN_WE_OFF(n)          (n * 0x20 + PCS0_WE_OFF)
#define CN_PCS_WE_OFF(n)        ((n + 3) * 0x20 + PCS0_WE_OFF)
#define CN_PCS_STATUS_OFF(n)    ((n + 3) * 0x20 + PCS0_STATUS_OFF)
#define CN_PCS_CTL_OFF(n)       ((n + 3) * 0x20 + PCS0_CTL_OFF)

struct atcsmu {
	void __iomem *base;
	struct resource *res;
	spinlock_t lock;
};

extern unsigned int *wake_event;

/* main hart (drivers/soc/andes/pm.c) */
void atcsmu_suspend2standby(void);
void atcsmu_suspend2ram(void);

/* other hart (arch/riscv/kernel/cpu-hotplug.c) */
void atcsmu_set_suspend_mode(void);

#define NORMAL_MODE                 0
#define LIGHT_SLEEP_MODE            1
#define DEEP_SLEEP_MODE             2
#define CPUHOTPLUG_DEEP_SLEEP_MODE  3

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
