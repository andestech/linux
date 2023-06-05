/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 */

#ifndef _ASM_RISCV_SWITCH_TO_H
#define _ASM_RISCV_SWITCH_TO_H

#include <linux/jump_label.h>
#include <linux/sched/task_stack.h>
#include <soc/andes/csr.h>
#include <asm/hwcap.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/csr.h>

#ifdef CONFIG_FPU
extern void __fstate_save(struct task_struct *save_to);
extern void __fstate_restore(struct task_struct *restore_from);

static inline void __fstate_clean(struct pt_regs *regs)
{
	regs->status = (regs->status & ~SR_FS) | SR_FS_CLEAN;
}

static inline void fstate_off(struct task_struct *task,
			      struct pt_regs *regs)
{
	regs->status = (regs->status & ~SR_FS) | SR_FS_OFF;
}

static inline void fstate_save(struct task_struct *task,
			       struct pt_regs *regs)
{
	if ((regs->status & SR_FS) == SR_FS_DIRTY) {
		__fstate_save(task);
		__fstate_clean(regs);
	}
}

static inline void fstate_restore(struct task_struct *task,
				  struct pt_regs *regs)
{
	if ((regs->status & SR_FS) != SR_FS_OFF) {
		__fstate_restore(task);
		__fstate_clean(regs);
	}
}

static inline void __switch_to_fpu(struct task_struct *prev,
				   struct task_struct *next)
{
	struct pt_regs *regs;

	regs = task_pt_regs(prev);
	if (unlikely(regs->status & SR_SD))
		fstate_save(prev, regs);
	fstate_restore(next, task_pt_regs(next));
}

static __always_inline bool has_fpu(void)
{
	return static_branch_likely(&riscv_isa_ext_keys[RISCV_ISA_EXT_KEY_FPU]);
}
#else
static __always_inline bool has_fpu(void) { return false; }
#define fstate_save(task, regs) do { } while (0)
#define fstate_restore(task, regs) do { } while (0)
#define __switch_to_fpu(__prev, __next) do { } while (0)
#endif /* CONFIG_FPU */

#ifdef CONFIG_DSP
static inline void dspstate_save(struct task_struct *task)
{
	task->thread.dspstate.ucode = csr_read(CSR_UCODE);
}

static inline void dspstate_restore(struct task_struct *task)
{
	csr_write(CSR_UCODE, task->thread.dspstate.ucode);
}

static inline void __switch_to_dsp(struct task_struct *prev,
				   struct task_struct *next)
{
	dspstate_save(prev);
	dspstate_restore(next);
}

static __always_inline bool has_dsp(void)
{
	return static_branch_likely(&riscv_isa_ext_keys[ANDES_ISA_EXT_KEY_DSP]);
}
#else
static __always_inline bool has_dsp(void) { return false; }
#define dspstate_save(task) do { } while (0)
#define dspstate_restore(task) do { } while (0)
#define __switch_to_dsp(__prev, __next) do { } while (0)
#endif /* CONFIG_DSP */

extern struct task_struct *__switch_to(struct task_struct *,
				       struct task_struct *);

#define switch_to(prev, next, last)			\
do {							\
	struct task_struct *__prev = (prev);		\
	struct task_struct *__next = (next);		\
	if (has_fpu())			\
		__switch_to_fpu(__prev, __next);	\
	if (has_dsp())			\
		__switch_to_dsp(__prev, __next);	\
	((last) = __switch_to(__prev, __next));		\
} while (0)

#endif /* _ASM_RISCV_SWITCH_TO_H */
