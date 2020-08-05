/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 * Copyright (C) 2017 SiFive
 */

#ifndef _ASM_RISCV_IRQ_H
#define _ASM_RISCV_IRQ_H

#include <linux/interrupt.h>
#include <linux/linkage.h>

#define NR_IRQS         72

/*
 * Use this value to indicate lack of interrupt
 * capability
 */
#ifndef NO_IRQ
#define NO_IRQ  ((unsigned int)(-1))
#endif

#define INTERRUPT_CAUSE_PMU        274

void riscv_software_interrupt(void);

#include <asm-generic/irq.h>

#endif /* _ASM_RISCV_IRQ_H */
