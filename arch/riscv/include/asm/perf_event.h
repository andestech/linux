/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 SiFive
 * Copyright (C) 2018 Andes Technology Corporation
 *
 */

#ifndef _ASM_RISCV_PERF_EVENT_H
#define _ASM_RISCV_PERF_EVENT_H

#include <linux/perf_event.h>
#include <linux/ptrace.h>
#include <linux/interrupt.h>

#ifdef CONFIG_RISCV_BASE_PMU
#define RISCV_BASE_COUNTERS	2

/*
 * The RISCV_MAX_COUNTERS parameter should be specified.
 */
#ifdef CONFIG_ANDES_PMU
#define RISCV_MAX_COUNTERS	7
#else
#define RISCV_MAX_COUNTERS	2
#endif	/* CONFIG_ANDES_PMU */

#define L2C_MAX_COUNTERS	32
#define BASE_COUNTERS		3

/*
 * These are the indexes of bits in counteren register *minus* 1,
 * except for cycle.  It would be coherent if it can directly mapped
 * to counteren bit definition, but there is a *time* register at
 * counteren[1].  Per-cpu structure is scarce resource here.
 *
 * According to the spec, an implementation can support counter up to
 * mhpmcounter31, but many high-end processors has at most 6 general
 * PMCs, we give the definition to MHPMCOUNTER8 here.
 */
#define RISCV_PMU_CYCLE		0
#define RISCV_PMU_INSTRET	1
#define RISCV_PMU_MHPMCOUNTER3	2
#define RISCV_PMU_MHPMCOUNTER4	3
#define RISCV_PMU_MHPMCOUNTER5	4
#define RISCV_PMU_MHPMCOUNTER6	5
#define RISCV_PMU_MHPMCOUNTER7	6
#define RISCV_PMU_MHPMCOUNTER8	7

#define RISCV_OP_UNSUPP		(-EOPNOTSUPP)

/* Event code for instruction commit events */
#define RISCV_CYCLE_COUNT               0x10
#define RISCV_INSTRET                   0x20
#define INT_LOAD_INST                   0x30
#define INT_STORE_INST                  0x40
#define ATOMIC_MEM_OP                   0x50
#define SYS_INST                        0x60
#define INT_COMPUTE_INST                0x70
#define CONDITION_BR                    0x80
#define TAKEN_CONDITION_BR              0x90
#define JAL_INST                        0xA0
#define JALR_INST                       0xB0
#define RET_INST                        0xC0
#define CONTROL_TRANS_INST              0xD0
#define EX9_INST                        0xE0
#define INT_MUL_INST                    0xF0
#define INT_DIV_REMAINDER_INST          0x100
#define FLOAT_LOAD_INST                 0x110
#define FLOAT_STORE_INST                0x120
#define FLOAT_ADD_SUB_INST              0x130
#define FLOAT_MUL_INST                  0x140
#define FLOAT_FUSED_MULADD_INST         0x150
#define FLOAT_DIV_SQUARE_ROOT_INST      0x160
#define OTHER_FLOAT_INST                0x170

/* Event code for memory system events */
#define ILM_ACCESS                      0x01
#define DLM_ACCESS                      0x11
#define ICACHE_ACCESS                   0x21
#define ICACHE_MISS                     0x31
#define DCACHE_ACCESS                   0x41
#define DCACHE_MISS                     0x51
#define DCACHE_LOAD_ACCESS              0x61
#define DCACHE_LOAD_MISS                0x71
#define DCACHE_STORE_ACCESS             0x81
#define DCACHE_STORE_MISS               0x91
#define DCACHE_WB                       0xA1
#define CYCLE_WAIT_ICACHE_FILL          0xB1
#define CYCLE_WAIT_DCACHE_FILL          0xC1
#define UNCACHED_IFETCH_FROM_BUS        0xD1
#define UNCACHED_LOAD_FROM_BUS          0xE1
#define CYCLE_WAIT_UNCACHED_IFETCH      0xF1
#define CYCLE_WAIT_UNCACHED_LOAD        0x101
#define MAIN_ITLB_ACCESS                0x111
#define MAIN_ITLB_MISS                  0x121
#define MAIN_DTLB_ACCESS                0x131
#define MAIN_DTLB_MISS                  0x141
#define CYCLE_WAIT_ITLB_FILL            0x151
#define PIPE_STALL_CYCLE_DTLB_MISS      0x161

/* Event code for microarchitecture events */
#define MISPREDICT_CONDITION_BR         0x02
#define MISPREDICT_TAKE_CONDITION_BR    0x12
#define MISPREDICT_TARGET_RET_INST      0x22
/* LAS: load after store, SAS: store after store */
#define REPLAY_LAS_SAS                  0x32

/* Event code for L2c */
#define L2C_CORE_OFF			0x10
#define TOTAL_C0_ACCESS			0xff00
#define L2C_C0_ACCESS			0xff01
#define L2C_C0_MISS			0xff02
#define TRANS_SNOOP_DATA		0xff03
#define RECV_SNOOP_DATA			0xff04
#define TOTAL_M4_ACCESS			0xff40
#define L2C_M4_ACCESS			0xff41
#define L2C_M4_MISS			0xff42
#define M4_RECV_SNOOP			0xff44
#define SYS_BUS_ACCESS			0xff50
#define L2_WAY_0_EVICTION_COUNT		0xff70
#define L2_WAY_1_EVICTION_COUNT		0xff71
#define L2_WAY_2_EVICTION_COUNT		0xff72
#define L2_WAY_3_EVICTION_COUNT		0xff73
#define L2_WAY_4_EVICTION_COUNT		0xff74
#define L2_WAY_5_EVICTION_COUNT		0xff75
#define L2_WAY_6_EVICTION_COUNT		0xff76
#define L2_WAY_7_EVICTION_COUNT		0xff77
#define L2_WAY_8_EVICTION_COUNT		0xff78
#define L2_WAY_9_EVICTION_COUNT		0xff79
#define L2_WAY_10_EVICTION_COUNT	0xff7a
#define L2_WAY_11_EVICTION_COUNT	0xff7b
#define L2_WAY_12_EVICTION_COUNT	0xff7c
#define L2_WAY_13_EVICTION_COUNT	0xff7d
#define L2_WAY_14_EVICTION_COUNT	0xff7e
#define L2_WAY_15_EVICTION_COUNT	0xff7f

#define CN_RECV_SNOOP_DATA(x)	\
	(RECV_SNOOP_DATA + (x * L2C_CORE_OFF))

struct cpu_hw_events {
	/* # currently enabled events*/
	int			n_events;
	/* currently enabled events */
	struct perf_event	*events[RISCV_MAX_COUNTERS];

	unsigned long           active_mask[BITS_TO_LONGS(RISCV_MAX_COUNTERS)];
	unsigned long           used_mask[BITS_TO_LONGS(RISCV_MAX_COUNTERS)];
	/* vendor-defined HPM data */
	void			*platform;
};

struct l2c_hw_events {
	int n_events;
	struct perf_event	*events[32];

	unsigned long		active_mask[BITS_TO_LONGS(32)];
	unsigned long		used_mask[BITS_TO_LONGS(32)];

	raw_spinlock_t          pmu_lock;
};

struct riscv_pmu {
	struct pmu	*pmu;

	/* generic hw/cache events table */
	const int	*hw_events;
	const int	(*cache_events)[PERF_COUNT_HW_CACHE_MAX]
				       [PERF_COUNT_HW_CACHE_OP_MAX]
				       [PERF_COUNT_HW_CACHE_RESULT_MAX];
	/* method used to map hw/cache events */
	int		(*map_hw_event)(u64 config);
	int		(*map_cache_event)(u64 config);
	int             (*map_raw_event)(u64 config);

	/* max generic hw events in map */
	int		max_events;
	/* number total counters, 2(base) + x(general) */
	int		num_counters;
	/* the width of the counter */
	int		counter_width;

	/* vendor-defined PMU features */
	void		*platform;

	irqreturn_t	(*handle_irq)(int irq_num, void *dev);
	int		irq;
};

#endif
#ifdef CONFIG_PERF_EVENTS
#define perf_arch_bpf_user_pt_regs(regs) (struct user_regs_struct *)regs
#endif

#endif /* _ASM_RISCV_PERF_EVENT_H */
