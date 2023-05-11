/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 Andes Technology Corporation.
 */

#ifndef __SOC_ANDES_PPMA_H
#define __SOC_ANDES_PPMA_H

#include <linux/types.h>

struct ppma_arg_t {
	phys_addr_t phys_addr;
	unsigned long va_addr;
	size_t size;
};

extern struct ppma_arg_t ppma_arg;

#ifdef CONFIG_ANDES_PPMA
void andes_set_ppma(phys_addr_t phys_addr,
		    void *va_addr,
		    size_t size);
void andes_free_ppma(void *addr);
long andes_probe_ppma(void);
#else
static inline void andes_set_ppma(phys_addr_t phys_addr,
				  void *va_addr,
				  size_t size) {}
static inline void andes_free_ppma(void *addr) {}
static inline long andes_probe_ppma(void)
{
	return 0;
}
#endif /* CONFIG_ANDES_PPMA */

#endif /* !__SOC_ANDES_PPMA_H */
