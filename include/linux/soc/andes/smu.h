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

struct atcsmu {
	void __iomem *base;
};

extern unsigned long *andes_wake_event;

void __iomem *atcsmu_get_address(void);

#endif
