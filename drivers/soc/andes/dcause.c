// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Andes Technology Corporation.
 */

#include <linux/printk.h>
#include <asm/csr.h>
#include <soc/andes/dcause.h>

void print_detailed_cause(long scause, unsigned long sdcause)
{
	if (scause >= 0) {
		switch (scause) {
		case EXC_INST_ACCESS:
			pr_info("The detailed trap cause: %s\n",
				sdcause_fetch_access[sdcause]);
			break;
		case EXC_INST_ILLEGAL:
			pr_info("The detailed trap cause: %s\n",
				sdcause_illegal_instruction[sdcause]);
			break;
		case EXC_LOAD_ACCESS:
			pr_info("The detailed trap cause: %s\n",
				sdcause_load_access[sdcause]);
			break;
		case EXC_STORE_ACCESS:
			pr_info("The detailed trap cause: %s\n",
				sdcause_store_access[sdcause]);
			break;
		case EXC_INST_PAGE_FAULT:
			pr_info("The detailed trap cause: %s\n",
				sdcause_fetch_page_fault[sdcause]);
			break;
		case EXC_LOAD_PAGE_FAULT:
			pr_info("The detailed trap cause: %s\n",
				sdcause_load_page_fault[sdcause]);
			break;
		case EXC_STORE_PAGE_FAULT:
			pr_info("The detailed trap cause: %s\n",
				sdcause_store_page_fault[sdcause]);
			break;
		default:
			pr_info("The detailed trap cause: %s\n",
				"sdcause does not support this exception");
		}
	} else {
		scause &= ~CAUSE_IRQ_FLAG;
		switch (scause) {
		case IRQ_IMPRECISE_ECC:
			pr_info("The detailed trap cause: %s\n",
				sdcause_imprecise_ECC[sdcause]);
			break;
		case IRQ_BUS_RW_TRANS:
			pr_info("The detailed trap cause: %s\n",
				sdcause_bus_rw_transaction[sdcause]);
			break;
		case IRQ_PMOVI:
			pr_info("The detailed trap cause: %s\n",
				"Performance monitor overflow interrupt");
			break;
		default:
			pr_info("The detailed trap cause: %s\n",
				"sdcause does not support this interrupt");
		}
	}
}
