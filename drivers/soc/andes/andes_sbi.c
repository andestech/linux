// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Andes Technology Corporation.
 */

#include <soc/andes/sbi.h>

void sbi_andes_set_ppma(void *arg)
{
	phys_addr_t phys_addr = ((struct ppma_arg_t *)arg)->phys_addr;
	unsigned long va_addr = ((struct ppma_arg_t *)arg)->va_addr;
	size_t size = ((struct ppma_arg_t *)arg)->size;

	sbi_ecall(SBI_EXT_ANDES, SBI_EXT_ANDES_SET_PPMA,
		  phys_addr, va_addr, size, 0, 0, 0);
}

void sbi_andes_free_ppma(void *addr)
{
	sbi_ecall(SBI_EXT_ANDES, SBI_EXT_ANDES_FREE_PPMA,
		  (unsigned long)addr, 0, 0, 0, 0, 0);
}

long sbi_andes_probe_ppma(void)
{
	struct sbiret ret;

	ret = sbi_ecall(SBI_EXT_ANDES, SBI_EXT_ANDES_PROBE_PPMA,
			0, 0, 0, 0, 0, 0);
	return ret.value;
}

/* trigger module support debug application with gdbserver */
void sbi_andes_set_trigger(unsigned int type, uintptr_t data, int enable)
{
	sbi_ecall(SBI_EXT_ANDES, SBI_EXT_ANDES_TRIGGER,
		  type, data, enable, 0, 0, 0);
}
EXPORT_SYMBOL(sbi_andes_set_trigger);

/* PowerBrake */
void sbi_andes_write_powerbrake(unsigned int val)
{
	sbi_ecall(SBI_EXT_ANDES, SBI_EXT_ANDES_WRITE_POWERBRAKE,
		  val, 0, 0, 0, 0, 0);
}
EXPORT_SYMBOL(sbi_andes_write_powerbrake);

long sbi_andes_read_powerbrake(void)
{
	struct sbiret ret;

	ret = sbi_ecall(SBI_EXT_ANDES, SBI_EXT_ANDES_READ_POWERBRAKE,
			0, 0, 0, 0, 0, 0);
	return ret.value;
}
EXPORT_SYMBOL(sbi_andes_read_powerbrake);
