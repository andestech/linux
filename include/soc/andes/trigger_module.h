/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 Andes Technology Corporation.
 */

#ifndef __SOC_ANDES_TRIGGER_MODULE_H
#define __SOC_ANDES_TRIGGER_MODULE_H

#include <soc/andes/sbi.h>

#define CNOP           0x0001
#define CNOP_SIZE      0x2
#define TRIGGER_TYPE_ICOUNT 3
#define ICOUNT 1

extern void bypass_singlestep(void);
void do_singlestep(void);

#endif /* !__SOC_ANDES_TRIGGER_MODULE_H */
