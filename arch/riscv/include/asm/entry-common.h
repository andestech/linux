/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _ASM_RISCV_ENTRY_COMMON_H
#define _ASM_RISCV_ENTRY_COMMON_H

#include <asm/stacktrace.h>
#include <linux/soc/andes/csr.h>
#include <linux/soc/andes/trigger_module.h>

void handle_page_fault(struct pt_regs *regs);
void handle_break(struct pt_regs *regs);

#ifdef CONFIG_ARCH_ANDES
static __always_inline void arch_exit_to_user_mode_work(struct pt_regs *regs,
							unsigned long ti_work)
{
	if (static_branch_unlikely(&trigger_module_single_step_key)) {
		if (ti_work & _TIF_SINGLESTEP) {
			sbi_andes_set_trigger(TRIGGER_TYPE_ICOUNT, ICOUNT, 1);
			csr_write(CSR_SCONTEXT, 1);
		} else {
			csr_write(CSR_SCONTEXT, 0);
		}
	}
}
#define arch_exit_to_user_mode_work arch_exit_to_user_mode_work
#endif
#endif /* _ASM_RISCV_ENTRY_COMMON_H */
