// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copied from arch/arm64/kernel/cpufeature.c
 *
 * Copyright (C) 2015 ARM Ltd.
 * Copyright (C) 2017 SiFive
 */

#include <linux/bitmap.h>
#include <linux/of.h>
#include <asm/processor.h>
#include <asm/hwcap.h>
#include <asm/smp.h>
#include <asm/switch_to.h>

unsigned long elf_hwcap __read_mostly;
unsigned long elf_hwcap2 __read_mostly;

const char *elf_platform;
EXPORT_SYMBOL(elf_platform);

/* Host ISA bitmap */
static DECLARE_BITMAP(riscv_isa, RISCV_ISA_EXT_MAX) __read_mostly;

#ifdef CONFIG_FPU
bool has_fpu __read_mostly;
#endif
#ifdef CONFIG_DSP
bool has_dsp __read_mostly;
#endif


/**
 * riscv_isa_extension_base() - Get base extension word
 *
 * @isa_bitmap: ISA bitmap to use
 * Return: base extension word as unsigned long value
 *
 * NOTE: If isa_bitmap is NULL then Host ISA bitmap will be used.
 */
unsigned long riscv_isa_extension_base(const unsigned long *isa_bitmap)
{
	if (!isa_bitmap)
		return riscv_isa[0];
	return isa_bitmap[0];
}
EXPORT_SYMBOL_GPL(riscv_isa_extension_base);

/**
 * __riscv_isa_extension_available() - Check whether given extension
 * is available or not
 *
 * @isa_bitmap: ISA bitmap to use
 * @bit: bit position of the desired extension
 * Return: true or false
 *
 * NOTE: If isa_bitmap is NULL then Host ISA bitmap will be used.
 */
bool __riscv_isa_extension_available(const unsigned long *isa_bitmap, int bit)
{
	const unsigned long *bmap = (isa_bitmap) ? isa_bitmap : riscv_isa;

	if (bit >= RISCV_ISA_EXT_MAX)
		return false;

	return test_bit(bit, bmap) ? true : false;
}
EXPORT_SYMBOL_GPL(__riscv_isa_extension_available);

void riscv_fill_hwcap(void)
{
	struct device_node *node;

	for_each_of_cpu_node(node) {
		if (riscv_of_processor_hartid(node) < 0)
			continue;

		if (of_property_read_string(node, "riscv,isa", &elf_platform)) {
			pr_warn("Unable to find \"riscv,isa\" devicetree entry\n");
			continue;
		}
		pr_info("elf_platform is %s", elf_platform);
	}

	elf_hwcap =  (strstr(elf_platform, "i2p0")) ? 1 : 0;

#ifdef CONFIG_FPU
	if (strstr(elf_platform, "f") && strstr(elf_platform, "d"))
		has_fpu = true;
#endif
#ifdef CONFIG_DSP
       if (strstr(elf_platform, "xdsp"))
               has_dsp = true;
#endif
}
