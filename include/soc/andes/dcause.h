// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Andes Technology Corporation.
 */

#ifndef _ASM_RISCV_DCAUSE_H
#define _ASM_RISCV_DCAUSE_H

#define IRQ_IMPRECISE_ECC	0x110
#define IRQ_BUS_RW_TRANS	0x111
#define IRQ_PMOVI		0x112

#define SDCAUSE_DCAUSE_MASK	0x1f

static const char *const sdcause_fetch_access[] = {
	"Reserved",
	"ECC/Parity error",
	"PMP/Smepmp instruction access violation",
	"Bus error",
	"PMA empty hole access",
	"PMA attribute inconsistency",
	"PTW access device region",
	"Instruction cache multiple-hit",
};

static const char *const sdcause_illegal_instruction[] = {
	"Please parse stval CSR",
	"FP diabled exception",
	"ACE diabled exception",
	"RVV disabled exception",
	"CCTL filllock/unlock not supported",
	"Reserved CCTL command",
	"CCTL TLB command not supported",
	"Reserverd for CCTL",
	"CCTL BTB command not supported",
	"CCTL L1D_VA_WB* and L1*_VA_INVAL not supported",
	"CCTL ALL type (L1D_*_ALL) not supported",
	"CCTL L1D_IX_WB* and L1*_IX_INVAL not supported",
	"Reserved XCSR",
};

static const char *const sdcause_load_access[] = {
	"Reserved",
	"ECC/Parity error",
	"PMP/Smepmp load access violation",
	"Bus error",
	"Misaligned address",
	"PMA empty hole access",
	"PMA attribute inconsistency",
	"PMA NAMO exception",
	"PTW access device region",
	"Data cache multiple-hit",
};

static const char *const sdcause_store_access[] = {
	"Reserved",
	"ECC/Parity error",
	"PMP/Smepmp store access violation",
	"Bus error",
	"Misaligned address",
	"PMA empty hole access",
	"PMA attribute inconsistency",
	"PMA NAMO exception",
	"PTW access device region",
	"Data cache multiple-hit",
	"CBO.Zero hits device region",
};

static const char *const sdcause_fetch_page_fault[] = {
	"Reserved",
	"ITLB multiple-hit",
};

static const char *const sdcause_load_page_fault[] = {
	"Reserved",
	"DTLB multiple-hit",
};

static const char *const sdcause_store_page_fault[] = {
	"Reserved",
	"Smepmp violation",
	"DTLB multiple-hit",
	"ITLB multiple-hit",
};

static const char *const sdcause_imprecise_ECC[] = {
	"Reserved",
	"LM slave port ECC/Parity error",
	"Imprecise store ECC/Parity error",
	"Imprecise load ECC/Parity error",
};

static const char *const sdcause_bus_rw_transaction[] = {
	"Reserved",
	"Bus read error",
	"Bus write error",
	"Read PMP/Smepmp check error",
	"Write PMP/Smepmp check error",
	"Read PMA check error",
	"Write PMA check error",
};

void print_detailed_cause(long scause, unsigned long sdcause);

#endif /* _ASM_RISCV_DCAUSE_H */
