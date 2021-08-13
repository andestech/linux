/*
 *  Copyright (C) 2020 Andes Technology Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <asm/andesv5/csr.h>
#include <asm/andesv5/proc.h>
#include <asm/sbi.h>

void sbi_suspend_prepare(char main_core, char enable)
{
	sbi_ecall(SBI_EXT_ANDES, SBI_EXT_ANDES_SUSPEND_PREPARE, main_core, enable, 0, 0, 0, 0);
}
EXPORT_SYMBOL(sbi_suspend_prepare);

void sbi_suspend_mem(void)
{
	sbi_ecall(SBI_EXT_ANDES, SBI_EXT_ANDES_SUSPEND_MEM, 0, 0, 0, 0, 0, 0);
}
EXPORT_SYMBOL(sbi_suspend_mem);

void sbi_restart(int cpu_num)
{
	sbi_ecall(SBI_EXT_ANDES, SBI_EXT_ANDES_RESTART, cpu_num, 0, 0, 0, 0, 0);
}
EXPORT_SYMBOL(sbi_restart);

void sbi_write_powerbrake(int val)
{
  sbi_ecall(SBI_EXT_ANDES, SBI_EXT_ANDES_WRITE_POWERBRAKE, val, 0, 0, 0, 0, 0);
}
EXPORT_SYMBOL(sbi_write_powerbrake);

int sbi_read_powerbrake(void)
{
  struct sbiret ret;
  ret = sbi_ecall(SBI_EXT_ANDES, SBI_EXT_ANDES_READ_POWERBRAKE, 0, 0, 0, 0, 0, 0);
  return ret.value;
}
EXPORT_SYMBOL(sbi_read_powerbrake);

void sbi_set_suspend_mode(int suspend_mode)
{
	sbi_ecall(SBI_EXT_ANDES, SBI_EXT_ANDES_SET_SUSPEND_MODE, suspend_mode, 0, 0, 0, 0, 0);
}
EXPORT_SYMBOL(sbi_set_suspend_mode);

void sbi_set_reset_vec(int val)
{
	sbi_ecall(SBI_EXT_ANDES, SBI_EXT_ANDES_SET_RESET_VEC, val, 0, 0, 0, 0, 0);
}
EXPORT_SYMBOL(sbi_set_reset_vec);

void sbi_set_pma(void *arg)
{
	phys_addr_t offset = ((struct pma_arg_t*)arg)->offset;
	unsigned long vaddr = ((struct pma_arg_t*)arg)->vaddr;
	size_t size = ((struct pma_arg_t*)arg)->size;
	size_t entry_id = ((struct pma_arg_t*)arg)->entry_id;
	sbi_ecall(SBI_EXT_ANDES, SBI_EXT_ANDES_SET_PMA, offset, vaddr, size, entry_id, 0, 0);
}
EXPORT_SYMBOL(sbi_set_pma);

void sbi_free_pma(unsigned long entry_id)
{
	sbi_ecall(SBI_EXT_ANDES, SBI_EXT_ANDES_FREE_PMA, entry_id, 0, 0, 0, 0, 0);
}
EXPORT_SYMBOL(sbi_free_pma);

long sbi_probe_pma(void)
{
	struct sbiret ret;
	ret = sbi_ecall(SBI_EXT_ANDES, SBI_EXT_ANDES_PROBE_PMA, 0, 0, 0, 0, 0, 0);
	return ret.value;
}
EXPORT_SYMBOL(sbi_probe_pma);

void sbi_set_trigger(unsigned int type, uintptr_t data, int enable)
{
	sbi_ecall(SBI_EXT_ANDES, SBI_EXT_ANDES_TRIGGER, type, data, enable, 0, 0, 0);
}
EXPORT_SYMBOL(sbi_set_trigger);

long sbi_get_marchid(void)
{
	struct sbiret ret;
	ret = sbi_ecall(SBI_EXT_BASE, SBI_EXT_BASE_GET_MARCHID, 0, 0, 0, 0, 0, 0);
	return ret.value;
}
EXPORT_SYMBOL(sbi_get_marchid);

long sbi_get_micm_cfg(void)
{
	struct sbiret ret;
	ret = sbi_ecall(SBI_EXT_ANDES, SBI_EXT_ANDES_GET_MICM_CFG,
			0, 0, 0, 0, 0, 0);
	return ret.value;
}
EXPORT_SYMBOL(sbi_get_micm_cfg);

long sbi_get_mdcm_cfg(void)
{
	struct sbiret ret;
	ret = sbi_ecall(SBI_EXT_ANDES, SBI_EXT_ANDES_GET_MDCM_CFG,
			0, 0, 0, 0, 0, 0);
	return ret.value;
}
EXPORT_SYMBOL(sbi_get_mdcm_cfg);
