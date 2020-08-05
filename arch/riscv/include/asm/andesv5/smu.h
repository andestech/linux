#ifndef _ASM_RISCV_SMU_H
#define _ASM_RISCV_SMU_H

#include <asm/sbi.h>
#define MAX_PCS_SLOT    7

#define PCS0_WE_OFF     0x90
#define PCS0_CTL_OFF    0x94
#define PCS0_STATUS_OFF 0x98

/*
 * PCS0 --> Always on power domain, includes the JTAG tap and DMI_AHB bus in
 *  ncejdtm200.
 * PCS1 --> Power domain for debug subsystem
 * PCS2 --> Main power domain, includes the system bus and AHB, APB peripheral
 *  IPs.
 * PCS3 --> Power domain for Core0 and L2C.
 * PCSN --> Power domain for Core (N-3)
 */

#define PCSN_WE_OFF(n)          n * 0x20 + PCS0_WE_OFF
#define CN_PCS_WE_OFF(n)        (n + 3) * 0x20 + PCS0_WE_OFF
#define CN_PCS_STATUS_OFF(n)    (n + 3) * 0x20 + PCS0_STATUS_OFF
#define CN_PCS_CTL_OFF(n)       (n + 3) * 0x20 + PCS0_CTL_OFF


#define PD_TYPE_MASK    0x7
#define PD_STATUS_MASK  0xf8
#define GET_PD_TYPE(val)        val & PD_TYPE_MASK
#define GET_PD_STATUS(val)      (val & PD_STATUS_MASK) >> 3

// PD_type
#define ACTIVE  0
#define RESET   1
#define SLEEP   2
#define TIMEOUT 7

// PD_status for sleep type
#define LightSleep_STATUS       0
#define DeepSleep_STATUS        16

// param of PCS_CTL for sleep cmd
#define LightSleep_CTL          0
#define DeepSleep_CTL           1

// PCS_CTL
#define PCS_CTL_PARAM_OFF       3
#define SLEEP_CMD       3

// wakeup events source offset
#define PCS_WAKE_DBG_OFF	28
#define PCS_WAKE_MSIP_OFF	29

#define L2_CTL_OFF              0x8
#define L2_COMMAND_OFF(cpu)     0x40 + 0x10 * cpu
#define L2_STATUS_REG           0x80
#define L2_WBINVAL_COMMAND      0x12

extern unsigned int *wake_mask;
extern void __iomem *l2c_base;

void set_wakeup_enable(int cpu, unsigned int events);
void set_sleep(int cpu, unsigned char sleep);
void andes_suspend2standby(void);
void andes_suspend2ram(void);

static inline void sbi_suspend_prepare(char main_core, char enable)
{
	/* TODO */
	// SBI_CALL_2(SBI_SUSPEND_PREPARE, main_core, enable);
}

static inline void sbi_suspend_mem(void)
{
	/* TODO */
	// SBI_CALL_0(SBI_SUSPEND_MEM);
}
#endif
