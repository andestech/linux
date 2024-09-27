// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Andes Technology Corporation.
 */
#include <asm/cacheflush.h>
#include <linux/soc/andes/csr.h>
#include <linux/soc/andes/trigger_module.h>

DEFINE_STATIC_KEY_FALSE(trigger_module_single_step_key);

void user_enable_single_step(struct task_struct *child)
{
	if (static_branch_unlikely(&trigger_module_single_step_key)) {
		if (!test_tsk_thread_flag(child, TIF_SINGLESTEP))
			static_branch_disable(&trigger_module_single_step_key);
	} else {
		static_branch_enable(&trigger_module_single_step_key);
	}

	set_tsk_thread_flag(child, TIF_SINGLESTEP);
}

void user_disable_single_step(struct task_struct *child)
{
	if (static_branch_unlikely(&trigger_module_single_step_key) &&
	    test_tsk_thread_flag(child, TIF_SINGLESTEP))
		clear_tsk_thread_flag(child, TIF_SINGLESTEP);
}
