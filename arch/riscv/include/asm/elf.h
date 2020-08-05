/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2003 Matjaz Breskvar <phoenix@bsemi.com>
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 * Copyright (C) 2012 Regents of the University of California
 */

#ifndef _ASM_RISCV_ELF_H
#define _ASM_RISCV_ELF_H

#include <uapi/asm/elf.h>
#include <asm/auxvec.h>
#include <asm/byteorder.h>
#include <asm/cacheinfo.h>

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_ARCH	EM_RISCV

#ifdef CONFIG_64BIT
#define ELF_CLASS	ELFCLASS64
#else
#define ELF_CLASS	ELFCLASS32
#endif

#define ELF_DATA	ELFDATA2LSB

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(x) ((x)->e_machine == EM_RISCV)

#define CORE_DUMP_USE_REGSET
#define ELF_EXEC_PAGESIZE	(PAGE_SIZE)

/*
 * This is the location that an ET_DYN program is loaded if exec'ed.  Typical
 * use of this is to invoke "./ld.so someprog" to test out a new version of
 * the loader.  We need to make sure that it is out of the way of the program
 * that it will "exec", and that there is sufficient room for the brk.
 */
#define ELF_ET_DYN_BASE		((TASK_SIZE / 3) * 2)

/*
 * Andes Support: ELF attribute checking
 */
#define ELF_HWCAP	(elf_hwcap)
#define ELF_HWCAP2	(elf_hwcap2)
extern unsigned long elf_hwcap;
extern unsigned long elf_hwcap2;
#define ELF_PLATFORM	(elf_platform)
extern const char *elf_platform;

#ifdef CONFIG_MMU
#define ARCH_DLINFO						\
do {								\
	NEW_AUX_ENT(AT_SYSINFO_EHDR,				\
		(elf_addr_t)current->mm->context.vdso);		\
	NEW_AUX_ENT(AT_L1I_CACHESIZE,				\
		get_cache_size(1, CACHE_TYPE_INST));		\
	NEW_AUX_ENT(AT_L1I_CACHEGEOMETRY,			\
		get_cache_geometry(1, CACHE_TYPE_INST));	\
	NEW_AUX_ENT(AT_L1D_CACHESIZE,				\
		get_cache_size(1, CACHE_TYPE_DATA));		\
	NEW_AUX_ENT(AT_L1D_CACHEGEOMETRY,			\
		get_cache_geometry(1, CACHE_TYPE_DATA));	\
	NEW_AUX_ENT(AT_L2_CACHESIZE,				\
		get_cache_size(2, CACHE_TYPE_UNIFIED));		\
	NEW_AUX_ENT(AT_L2_CACHEGEOMETRY,			\
		get_cache_geometry(2, CACHE_TYPE_UNIFIED));	\
} while (0)
#define ARCH_HAS_SETUP_ADDITIONAL_PAGES
struct linux_binprm;
extern int arch_setup_additional_pages(struct linux_binprm *bprm,
	int uses_interp);
#endif /* CONFIG_MMU */

struct file;
#ifdef CONFIG_64BIT
struct elf64_phdr;
extern int arch_elf_pt_proc(void *ehdr, struct elf64_phdr *phdr, struct file *elf,
	bool is_interp, void *state);
#else
struct elf32_phdr;
extern int arch_elf_pt_proc(void *ehdr, struct elf32_phdr *phdr, struct file *elf,
	bool is_interp, void *state);
#endif

struct arch_elf_state {
};
#define INIT_ARCH_ELF_STATE { }
#define arch_check_elf(ehdr, interp, interp_ehdr, state) (0)
#endif /* _ASM_RISCV_ELF_H */
