/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 Andes Technology Corporation.
 */

#ifndef __LINUX_SOC_ANDES_TRIGGER_MODULE_H
#define __LINUX_SOC_ANDES_TRIGGER_MODULE_H

#include <linux/soc/andes/sbi.h>

#define TRIGGER_TYPE_ICOUNT 3
#define ICOUNT 1

DECLARE_STATIC_KEY_FALSE(trigger_module_single_step_key);

void user_enable_single_step(struct task_struct *);
void user_disable_single_step(struct task_struct *);

#endif /* !__LINUX_SOC_ANDES_TRIGGER_MODULE_H */
