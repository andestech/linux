/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 Andes Technology Corporation.
 */
#ifndef __SOC_ANDES_CSR_H
#define __SOC_ANDES_CSR_H

#include <linux/types.h>
/* mdcm_cfg: Data Cache/Memory Configuration Register */
#define MDCM_CFG_DEST_OFFSET		0
#define MDCM_CFG_DWAY_OFFSET		3
#define MDCM_CFG_DSZ_OFFSET		6
#define MDCM_CFG_DLCK_OFFSET		9
#define MDCM_CFG_DC_ECC_OFFSET		10
#define MDCM_CFG_DLMB_OFFSET		12
#define MDCM_CFG_DLMSZ_OFFSET		15
#define MDCM_CFG_ULM_2BANK_OFFSET	20
#define MDCM_CFG_DLM_ECC_OFFSET		21

#define MDCM_CFG_DEST_MASK	(0x7  << MDCM_CFG_DEST_OFFSET)
#define MDCM_CFG_DWAY_MASK	(0x7  << MDCM_CFG_DWAY_OFFSET)
#define MDCM_CFG_DSZ_MASK	(0x7  << MDCM_CFG_DSZ_OFFSET)
#define MDCM_CFG_DLCK_MASK	(0x1  << MDCM_CFG_DLCK_OFFSET)
#define MDCM_CFG_DC_ECC_MASK	(0x3  << MDCM_CFG_DC_ECC_OFFSET)
#define MDCM_CFG_DLMB_MASK	(0x7  << MDCM_CFG_DLMB_OFFSET)
#define MDCM_CFG_DLMSZ_MASK	(0x1f << MDCM_CFG_DLMSZ_OFFSET)
#define MDCM_CFG_ULM_2BANK_MASK	(0x1  << MDCM_CFG_ULM_2BANK_OFFSET)
#define MDCM_CFG_DLM_ECC_MASK	(0x3  << MDCM_CFG_DLM_ECC_OFFSET)

/* User mode control registers */
#define CSR_UITB					   0x800
#define CSR_UCODE					   0x801
#define CSR_UDCAUSE					   0x809
#define CCTL_REG_UCCTLBEGINADDR_NUM    0x80b
#define CCTL_REG_UCCTLCOMMAND_NUM      0x80c
#define CSR_WFE						   0x810
#define CSR_SLEEPVALUE				   0x811
#define CSR_TXEVT					   0x812

#define custom_csr_write(csr_num, val) csr_write(csr_num, val)
/* ucctlcommand */
/* D-cache operation */
#define CCTL_L1D_VA_INVAL	0
#define CCTL_L1D_VA_WB		1
#define CCTL_L1D_VA_WBINVAL	2

/* non-blocking & write around */
#define MMISC_CTL_NON_BLOCKING_ENABLE  (0x1  << MMISC_CTL_NON_BLOCKING_OFFSET)
#define MMISC_CTL_NON_BLOCKING_OFFSET  0x8

#define MCACHE_CTL_L1I_PREFETCH_OFFSET  9
#define MCACHE_CTL_L1D_PREFETCH_OFFSET  10
#define MCACHE_CTL_DC_WAROUND_OFFSET_1  13
#define MCACHE_CTL_DC_WAROUND_OFFSET_2  14
#define MCACHE_CTL_L1I_PREFETCH_EN  (0x1  << MCACHE_CTL_L1I_PREFETCH_OFFSET)
#define MCACHE_CTL_L1D_PREFETCH_EN  (0x1  << MCACHE_CTL_L1D_PREFETCH_OFFSET)
#define MCACHE_CTL_DC_WAROUND_1_EN  (0x1  << MCACHE_CTL_DC_WAROUND_OFFSET_1)
#define MCACHE_CTL_DC_WAROUND_2_EN  (0x1  << MCACHE_CTL_DC_WAROUND_OFFSET_2)
#define WRITE_AROUND_ENABLE  (MCACHE_CTL_L1I_PREFETCH_EN | MCACHE_CTL_L1D_PREFETCH_EN | MCACHE_CTL_DC_WAROUND_1_EN)

/* L1 I-cache , D-cache */
#define CACHE_CTL_offIC_EN  0   /* Enable I-cache */
#define CACHE_CTL_offDC_EN  1   /* Enable D-cache */
#define CACHE_CTL_mskIC_EN  (0x1  << CACHE_CTL_offIC_EN)
#define CACHE_CTL_mskDC_EN  (0x1  << CACHE_CTL_offDC_EN)

/* L2 cache */
#define L2_CACHE_CTL_mskCEN 1
/* L2 cache registers */
#define L2C_REG_CFG_OFFSET	0
#define L2C_REG_CTL_OFFSET	0x8
#define L2C_HPM_C0_CTL_OFFSET	0x10
#define L2C_HPM_C1_CTL_OFFSET	0x18
#define L2C_HPM_C2_CTL_OFFSET	0x20
#define L2C_HPM_C3_CTL_OFFSET	0x28
#define L2C_REG_C0_CMD_OFFSET	0x40
#define L2C_REG_C0_ACC_OFFSET	0x48
#define L2C_REG_C1_CMD_OFFSET	0x50
#define L2C_REG_C1_ACC_OFFSET	0x58
#define L2C_REG_C2_CMD_OFFSET	0x60
#define L2C_REG_C2_ACC_OFFSET	0x68
#define L2C_REG_C3_CMD_OFFSET	0x70
#define L2C_REG_C3_ACC_OFFSET	0x78
#define L2C_REG_C0_STATUS_OFFSET	0x80
#define L2C_REG_C0_HPM_OFFSET	0x200

/* L2 cache configuration register */
#define V5_L2C_CFG_MAP_OFFSET       20
#define V5_L2C_CFG_MAP_MASK         (1UL << V5_L2C_CFG_MAP_OFFSET)

/* L2 CCTL status */
#define CCTL_L2_STATUS_IDLE	0
#define CCTL_L2_STATUS_PROCESS	1
#define CCTL_L2_STATUS_ILLEGAL	2
/* L2 CCTL status cores mask */
#define CCTL_L2_STATUS_C0_MASK	0xF
#define CCTL_L2_STATUS_C1_MASK	0xF0
#define CCTL_L2_STATUS_C2_MASK	0xF00
#define CCTL_L2_STATUS_C3_MASK	0xF000

/* L2 cache operation */
#define CCTL_L2_PA_INVAL	0x8
#define CCTL_L2_PA_WB		0x9
#define CCTL_L2_PA_WBINVAL	0xA
#define CCTL_L2_WBINVAL_ALL	0x12

#define L2C_HPM_PER_CORE_OFFSET		0x8
#ifndef __ASSEMBLER__
extern u32 L2C_REG_PER_CORE_OFFSET;
extern u32 CCTL_L2_STATUS_PER_CORE_OFFSET;
#endif
#define L2C_REG_CN_CMD_OFFSET(n)	\
	(L2C_REG_C0_CMD_OFFSET + (n * L2C_REG_PER_CORE_OFFSET))
#define L2C_REG_CN_ACC_OFFSET(n)	\
	(L2C_REG_C0_ACC_OFFSET + (n * L2C_REG_PER_CORE_OFFSET))
#define CCTL_L2_STATUS_CN_MASK(n)	\
	(CCTL_L2_STATUS_C0_MASK << (n * CCTL_L2_STATUS_PER_CORE_OFFSET))
#define L2C_HPM_CN_CTL_OFFSET(n)	\
	(L2C_HPM_C0_CTL_OFFSET + (n * L2C_HPM_PER_CORE_OFFSET))
#define L2C_REG_CN_HPM_OFFSET(n)	\
	(L2C_REG_C0_HPM_OFFSET + (n * L2C_HPM_PER_CORE_OFFSET))

/* Debug/Trace Registers (shared with Debug Mode) */
#define CSR_SCONTEXT            0x7aa

/* Supervisor trap registers */
#define CSR_SLIE				0x9c4
#define CSR_SLIP				0x9c5
#define CSR_SDCAUSE				0x9c9

/* Supervisor counter registers */
#define CSR_SCOUNTERINTEN		0x9cf
#define CSR_SCOUNTERMASK_M		0x9d1
#define CSR_SCOUNTERMASK_S		0x9d2
#define CSR_SCOUNTERMASK_U		0x9d3
#define CSR_SCOUNTEROVF			0x9d4
#define CSR_SCOUNTINHIBIT		0x9e0
#define CSR_SHPMEVENT3			0x9e3
#define CSR_SHPMEVENT4			0x9e4
#define CSR_SHPMEVENT5			0x9e5
#define CSR_SHPMEVENT6			0x9e6

/* Supervisor control registers */
#define CSR_SCCTLDATA			0x9cd
#define CSR_SMISC_CTL			0x9d0

#define IRQ_HPM_OVF		18
#define SLIP_PMOVI		(_AC(0x1, UL) << IRQ_HPM_OVF)

#endif /* !__SOC_ANDES_CSR_H */
