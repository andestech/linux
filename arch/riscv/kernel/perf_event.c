/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2008 Thomas Gleixner <tglx@linutronix.de>
 * Copyright (C) 2008-2009 Red Hat, Inc., Ingo Molnar
 * Copyright (C) 2009 Jaswinder Singh Rajput
 * Copyright (C) 2009 Advanced Micro Devices, Inc., Robert Richter
 * Copyright (C) 2008-2009 Red Hat, Inc., Peter Zijlstra
 * Copyright (C) 2009 Intel Corporation, <markus.t.metzger@intel.com>
 * Copyright (C) 2009 Google, Inc., Stephane Eranian
 * Copyright 2014 Tilera Corporation. All Rights Reserved.
 * Copyright (C) 2018 Andes Technology Corporation
 *
 * Perf_events support for RISC-V platforms.
 *
 * Since the spec. (as of now, Priv-Spec 1.10) does not provide enough
 * functionality for perf event to fully work, this file provides
 * the very basic framework only.
 *
 * For platform portings, please check Documentations/riscv/pmu.txt.
 *
 * The Copyright line includes x86 and tile ones.
 */

#include <linux/kprobes.h>
#include <linux/kernel.h>
#include <linux/kdebug.h>
#include <linux/mutex.h>
#include <linux/bitmap.h>
#include <linux/irq.h>
#include <linux/perf_event.h>
#include <linux/atomic.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <asm/perf_event.h>
#include <asm/sbi.h>
#include <asm/andesv5/csr.h>

#define PFMOVF_MASK	0x40000
#define L2C_EVSEL_MASK	0xff
#define EVSEL_MASK	0xf
#define UMODE_MASK	0x4
#define SMODE_MASK	0x2
#define MMODE_MASK	0x1

#define MHPEVENT_OFF	4
#define EVSEL_OFF	3
#define L2C_MARK_OFF	8
#define L2C_FLAG_OFF	(L2C_MARK_OFF + EVSEL_OFF)

#define INST_COMMIT	0
#define MEM_SYS		1
#define MICROARCH	2

#define INST_COMMIT_NUM	23
#define MEM_SYS_NUM	22
#define MICROARCH_NUM	3
#define HPM_FORMAT_ATTR(x,y) PMU_FORMAT_ATTR(x,#y)
static const struct riscv_pmu *riscv_pmu __read_mostly;
static DEFINE_PER_CPU(struct cpu_hw_events, cpu_hw_events);
typedef void (*perf_irq_t)(struct pt_regs *);
perf_irq_t perf_irq = NULL;
static cpumask_t pmu_cpu;
static struct l2c_hw_events l2c_hw_events;

/*
 * Hardware & cache maps and their methods
 */

static const int riscv_hw_event_map[] = {
	[PERF_COUNT_HW_CPU_CYCLES]		= RISCV_CYCLE_COUNT,
	[PERF_COUNT_HW_INSTRUCTIONS]		= RISCV_INSTRET,
	[PERF_COUNT_HW_CACHE_REFERENCES]	= DCACHE_ACCESS,
	[PERF_COUNT_HW_CACHE_MISSES]		= DCACHE_MISS,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS]	= RISCV_OP_UNSUPP,
	[PERF_COUNT_HW_BRANCH_MISSES]		= RISCV_OP_UNSUPP,
	[PERF_COUNT_HW_BUS_CYCLES]		= RISCV_OP_UNSUPP,
};

#define C(x) PERF_COUNT_HW_CACHE_##x
static const int riscv_cache_event_map[PERF_COUNT_HW_CACHE_MAX]
[PERF_COUNT_HW_CACHE_OP_MAX]
[PERF_COUNT_HW_CACHE_RESULT_MAX] = {
	[C(L1D)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = DCACHE_LOAD_ACCESS,
			[C(RESULT_MISS)] = DCACHE_LOAD_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = DCACHE_STORE_ACCESS,
			[C(RESULT_MISS)] = DCACHE_STORE_MISS,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
	},
	[C(L1I)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = ICACHE_ACCESS,
			[C(RESULT_MISS)] = ICACHE_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
	},
	[C(LL)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = L2C_C0_ACCESS,
			[C(RESULT_MISS)] = L2C_C0_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = L2C_C0_ACCESS,
			[C(RESULT_MISS)] = L2C_C0_MISS,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
	},
	[C(DTLB)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] =  RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] =  RISCV_OP_UNSUPP,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
	},
	[C(ITLB)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
	},
	[C(BPU)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
	},
	[C(NODE)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
	},
};

static bool is_l2c_event(u64 config)
{
	return ((config >> L2C_FLAG_OFF) == L2C_EVSEL_MASK);
}

static int riscv_map_raw_event(u64 config)
{
        u32 val = config & EVSEL_MASK;
        u32 event = config >> MHPEVENT_OFF;

	if ((config >> L2C_MARK_OFF) == L2C_EVSEL_MASK)
		return config;

        switch (val) {
                case INST_COMMIT:
                        if (event > INST_COMMIT_NUM)
                                return -EINVAL;
                        break;
                case MEM_SYS:
                        if (event > MEM_SYS_NUM)
                                return -EINVAL;
                        break;
                case MICROARCH:
                        if (event > MICROARCH_NUM)
                                return -EINVAL;
                        break;
                default:
                        return -EINVAL;
        }

        return config;
}

static int riscv_map_hw_event(u64 config)
{
	if (config >= riscv_pmu->max_events)
		return -EINVAL;

	return riscv_pmu->hw_events[config];
}

static int riscv_map_cache_event(u64 config)
{
	unsigned int type, op, result;
	int code;

	type = (config >> 0) & 0xff;
	op = (config >> 8) & 0xff;
	result = (config >> 16) & 0xff;

	if (!riscv_pmu->cache_events)
		return -ENOENT;

	if (type >= PERF_COUNT_HW_CACHE_MAX ||
	    op >= PERF_COUNT_HW_CACHE_OP_MAX ||
	    result >= PERF_COUNT_HW_CACHE_RESULT_MAX)
		return -EINVAL;

	code = (*riscv_pmu->cache_events)[type][op][result];
	if (code == RISCV_OP_UNSUPP)
		return -ENOENT;

	return code;
}

/*
 * Low-level functions: reading/writing counters
 */

static inline u64 read_counter(int idx)
{
	u64 val = 0;

	switch (idx) {
	case RISCV_CYCLE_COUNTER:
#if __riscv_xlen == 32
                val |= csr_read(CSR_CYCLE);
                val |= ((u64)csr_read(CSR_CYCLEH) << 32);
#elif __riscv_xlen == 64
                val = csr_read(CSR_CYCLE);
#endif
		break;
	case RISCV_INSTRET_COUNTER:
#if __riscv_xlen == 32
                val |= csr_read(CSR_INSTRET);
                val |= ((u64)csr_read(CSR_INSTRETH) << 32);
#elif __riscv_xlen == 64
                val = csr_read(CSR_INSTRET);
#endif
		break;
	case RISCV_HPM_MHPMCOUNTER3:
#if __riscv_xlen == 32
                val |= csr_read(0xC03);
                val |= ((u64)csr_read(0xC83) << 32);
#elif __riscv_xlen == 64
                val = csr_read(0xC03);
#endif
		break;
	case RISCV_HPM_MHPMCOUNTER4:
#if __riscv_xlen == 32
                val |= csr_read(0xC04);
                val |= ((u64)csr_read(0xC84) << 32);
#elif __riscv_xlen == 64
                val = csr_read(0xC04);
#endif
		break;
	case RISCV_HPM_MHPMCOUNTER5:
#if __riscv_xlen == 32
                val |= csr_read(0xC05);
                val |= ((u64)csr_read(0xC85) << 32);
#elif __riscv_xlen == 64
                val = csr_read(0xC05);
#endif
		break;
	case RISCV_HPM_MHPMCOUNTER6:
#if __riscv_xlen == 32
                val |= csr_read(0xC06);
                val |= ((u64)csr_read(0xC86) << 32);
#elif __riscv_xlen == 64
                val = csr_read(0xC06);
#endif
		break;
	default:
		WARN_ON_ONCE(idx < 0 ||	idx > RISCV_MAX_COUNTERS);
		return -EINVAL;
	}

	return val;
}

static inline void write_counter(int idx, u64 value)
{
        switch (idx) {
        case RISCV_CYCLE_COUNTER:
#if __riscv_xlen == 32
                csr_write(CSR_CYCLE, value);
                csr_write(CSR_CYCLEH, value >> 32);
#elif __riscv_xlen == 64
                csr_write(CSR_CYCLE, value);
#endif
                break;
        case RISCV_INSTRET_COUNTER:
#if __riscv_xlen == 32
                csr_write(CSR_INSTRET, value);
                csr_write(CSR_INSTRETH, value >> 32);
#elif __riscv_xlen == 64
                csr_write(CSR_INSTRET, value);
#endif
                break;
        case RISCV_HPM_MHPMCOUNTER3:
#if __riscv_xlen == 32
                csr_write(0xC03, value);
                csr_write(0xC83, value >> 32);
#elif __riscv_xlen == 64
                csr_write(0xC03, value);
#endif
                break;
        case RISCV_HPM_MHPMCOUNTER4:
#if __riscv_xlen == 32
                csr_write(0xC04, value);
                csr_write(0xC84, value >> 32);
#elif __riscv_xlen == 64
                csr_write(0xC04, value);
#endif
                break;
        case RISCV_HPM_MHPMCOUNTER5:
#if __riscv_xlen == 32
                csr_write(0xC05, value);
                csr_write(0xC85, value >> 32);
#elif __riscv_xlen == 64
                csr_write(0xC05, value);
#endif
                break;
        case RISCV_HPM_MHPMCOUNTER6:
#if __riscv_xlen == 32
                csr_write(0xC06, value);
                csr_write(0xC86, value >> 32);
#elif __riscv_xlen == 64
                csr_write(0xC06, value);
#endif
                break;
        default:
                WARN_ON_ONCE(idx < 0 || idx > RISCV_MAX_COUNTERS);
        }
}

/*
 * pmu->read: read and update the counter
 *
 * Other architectures' implementation often have a xxx_perf_event_update
 * routine, which can return counter values when called in the IRQ, but
 * return void when being called by the pmu->read method.
 */
static void riscv_pmu_read(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	u64 prev_raw_count, new_raw_count;
	u64 oldval;
	int idx = hwc->idx;
	u64 delta;

	do {
		prev_raw_count = local64_read(&hwc->prev_count);
		if (is_l2c_event(hwc->config))
			new_raw_count = l2c_read_counter(idx);
		else
			new_raw_count = read_counter(idx);

		oldval = local64_cmpxchg(&hwc->prev_count, prev_raw_count,
					 new_raw_count);
	} while (oldval != prev_raw_count);

	/*
	 * delta is the value to update the counter we maintain in the kernel.
	 */
	delta = (new_raw_count - prev_raw_count) &
		((1ULL << riscv_pmu->counter_width) - 1);
	local64_add(delta, &event->count);
	/*
	 * Something like local64_sub(delta, &hwc->period_left) here is
	 * needed if there is an interrupt for perf.
	 */
	local64_sub(delta, &hwc->period_left);
}

void riscv_pmu_disable_counter(int idx)
{
        u32 val = 1UL << idx;

        csr_set(CSR_SCOUNTINHIBIT, val);
}

void riscv_pmu_enable_counter(int idx)
{
        u32 val = 1UL << idx;

        csr_clear(CSR_SCOUNTINHIBIT, val);
}

void riscv_pmu_disable_interrupt(int idx)
{
        u32 val = 1UL << idx;

        csr_clear(CSR_SCOUNTERINTEN, val);
}

void riscv_pmu_enable_interrupt(int idx)
{
        u32 val = 1UL << idx;

        csr_set(CSR_SCOUNTERINTEN, val);
}

static inline void riscv_pmu_disable_event(struct perf_event *event)
{
        struct hw_perf_event *hwc = &event->hw;
        int idx = hwc->idx;

        if (idx == -1)
                return;

        // disable counter
        riscv_pmu_disable_counter(idx);

        // disable interrupt
        riscv_pmu_disable_interrupt(idx);
}

static inline void riscv_pmu_disable(void)
{
        // Disable all counter
        csr_set(CSR_SCOUNTINHIBIT, 0xfffffffd);
}

static inline void riscv_pmu_enable(struct perf_event *event)
{
        // Enable all counter
	csr_clear(CSR_SCOUNTINHIBIT, 0xfffffffd);
}

static int riscv_event_set_period(struct perf_event *event)
{
        struct hw_perf_event *hwc = &event->hw;
        int idx = hwc->idx;
        s64 left = local64_read(&hwc->period_left);
        s64 period = hwc->sample_period;
        int ret = 0;

        if (unlikely(left <= -period)) {
                left = period;
                local64_set(&hwc->period_left, left);
                hwc->last_period = period;
                ret = 1;
        }

        if (unlikely(left <= 0)) {
                left += period;
                local64_set(&hwc->period_left, left);
                hwc->last_period = period;
                ret = 1;
        }

        if (left > riscv_pmu->max_period)
                left = riscv_pmu->max_period;

        local64_set(&hwc->prev_count, (u64)-left);

	if (is_l2c_event(hwc->config))
		l2c_write_counter(idx, (u64)(-left));
	else
		write_counter(idx, (u64)(-left));

        perf_event_update_userpage(event);

        return ret;
}

static inline void riscv_pmu_event_enable(struct perf_event *event)
{
        struct hw_perf_event *hwc = &event->hw;
        int idx = hwc->idx;
        u32 value = 1UL << idx;
        u32 ev_config = hwc->config >> EVSEL_OFF;

        if (WARN_ON_ONCE(idx == -1))
                return;

        if(hwc->config & MMODE_MASK)
                csr_set(CSR_SCOUNTERMASK_M, value);
        if(hwc->config & SMODE_MASK)
                csr_set(CSR_SCOUNTERMASK_S, value);
        if(hwc->config & UMODE_MASK)
                csr_set(CSR_SCOUNTERMASK_U, value);

        if (idx < BASE_COUNTERS)
                return;

        switch (idx) {
                case RISCV_HPM_MHPMCOUNTER3:
                        csr_write(CSR_SHPMEVENT3, ev_config);
                        break;
                case RISCV_HPM_MHPMCOUNTER4:
                        csr_write(CSR_SHPMEVENT4, ev_config);
                        break;
                case RISCV_HPM_MHPMCOUNTER5:
                        csr_write(CSR_SHPMEVENT5, ev_config);
                        break;
                case RISCV_HPM_MHPMCOUNTER6:
                        csr_write(CSR_SHPMEVENT6, ev_config);
                        break;
                default:
                        pr_err("The number of counters are exceedied!\n");
                        break;
        }
}

static inline int riscv_get_counter_idx(u64 config)
{
        struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
        int idx;
        u64 val = config >> EVSEL_OFF;

        if (val == RISCV_CYCLE_COUNT) {
                if (test_bit(RISCV_CYCLE_COUNTER, cpuc->used_mask))
                        idx = find_next_zero_bit(cpuc->used_mask, RISCV_MAX_COUNTERS,
						 BASE_COUNTERS);
                else
                        idx = RISCV_CYCLE_COUNTER;
        } else if (val == RISCV_INSTRET) {
                if (test_bit(RISCV_INSTRET_COUNTER, cpuc->used_mask))
                        idx = find_next_zero_bit(cpuc->used_mask, RISCV_MAX_COUNTERS,
						 BASE_COUNTERS);
                else
                        idx = RISCV_INSTRET_COUNTER;
        } else
                idx = find_next_zero_bit(cpuc->used_mask, RISCV_MAX_COUNTERS,
					 BASE_COUNTERS);

        return idx;
}

/*
 * State transition functions:
 *
 * stop()/start() & add()/del()
 */

/*
 * pmu->stop: stop the counter
 */
static void riscv_pmu_stop(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	struct l2c_hw_events *l2c = &l2c_hw_events;
	int idx = hwc->idx;
	unsigned long irq_flags;

	if (is_l2c_event(hwc->config))
		goto l2c_event_stop;

	if (__test_and_clear_bit(idx, cpuc->active_mask)) {
		riscv_pmu_disable_event(event);
		WARN_ON_ONCE(hwc->state & PERF_HES_STOPPED);
		hwc->state |= PERF_HES_STOPPED;
	}

	if ((flags & PERF_EF_UPDATE) && !(hwc->state & PERF_HES_UPTODATE)) {
		riscv_pmu->pmu->read(event);
		hwc->state |= PERF_HES_UPTODATE;
	}

	// disable all mask
	if (cpuc->n_events == 0) {
		csr_write(CSR_SCOUNTERMASK_M, 0);
		csr_write(CSR_SCOUNTERMASK_S, 0);
		csr_write(CSR_SCOUNTERMASK_U, 0);
	}
	return;
l2c_event_stop:
	if (__test_and_clear_bit(idx, l2c->active_mask)) {
		raw_spin_lock_irqsave(&l2c->pmu_lock, irq_flags);
                l2c_pmu_disable_counter(hwc->idx);
		raw_spin_unlock_irqrestore(&l2c->pmu_lock, irq_flags);

                WARN_ON_ONCE(hwc->state & PERF_HES_STOPPED);
                hwc->state |= PERF_HES_STOPPED;
        }

	if ((flags & PERF_EF_UPDATE) && !(hwc->state & PERF_HES_UPTODATE)) {
		riscv_pmu->pmu->read(event);
		hwc->state |= PERF_HES_UPTODATE;
	}
}

/*
 * pmu->start: start the event.
 */
static void riscv_pmu_start(struct perf_event *event, int flags)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	struct l2c_hw_events *l2c = &l2c_hw_events;
	int idx = event->hw.idx;
	unsigned long irq_flags;

	if (WARN_ON_ONCE(!(event->hw.state & PERF_HES_STOPPED)))
		return;

	if (WARN_ON_ONCE(idx == -1))
		return;

	if (is_l2c_event(event->hw.config))
		goto l2c_event_start;

	riscv_pmu_disable_counter(idx);
	if (flags & PERF_EF_RELOAD) {
		WARN_ON_ONCE(!(event->hw.state & PERF_HES_UPTODATE));

		/*
		 * Set the counter to the period to the next interrupt here,
		 * if you have any.
		 */
		riscv_event_set_period(event);
	}

	event->hw.state = 0;
	cpuc->events[idx] = event;
	__set_bit(idx, cpuc->active_mask);

	riscv_pmu_enable_interrupt(idx);
	riscv_pmu_event_enable(event);
	riscv_pmu_enable_counter(idx);
	goto finish_start;
l2c_event_start:
	raw_spin_lock_irqsave(&l2c->pmu_lock, irq_flags);
	l2c_pmu_disable_counter(idx);
	raw_spin_unlock_irqrestore(&l2c->pmu_lock, irq_flags);

	if (flags & PERF_EF_RELOAD) {
		WARN_ON_ONCE(!(event->hw.state & PERF_HES_UPTODATE));

		/*
		 * Set the counter to the period to the next interrupt here,
		 * if you have any.
		 */
		riscv_event_set_period(event);
	}
	event->hw.state = 0;
	l2c->events[idx] = event;

	raw_spin_lock_irqsave(&l2c->pmu_lock, irq_flags);
	__set_bit(idx, l2c->active_mask);

	l2c_pmu_event_enable((event->hw.config >> EVSEL_OFF) & L2C_EVSEL_MASK,
				idx);
	raw_spin_unlock_irqrestore(&l2c->pmu_lock, irq_flags);
finish_start:
	perf_event_update_userpage(event);
}

/*
 * pmu->add: add the event to HPM.
 */
static int riscv_pmu_add(struct perf_event *event, int flags)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	struct hw_perf_event *hwc = &event->hw;
	struct l2c_hw_events *l2c = &l2c_hw_events;
	int idx = 0;
	unsigned long irq_flags;

	if (is_l2c_event(hwc->config))
		goto add_l2c_event;

	idx = riscv_get_counter_idx(hwc->config);

	if (idx > riscv_pmu->num_counters)
		return -ENOSPC;

	cpuc->events[idx] = event;
	cpuc->n_events++;

	__set_bit(idx, cpuc->used_mask);

	goto finish_add;

add_l2c_event:
	if (l2c->n_events == L2C_MAX_COUNTERS)
		return -ENOSPC;

	raw_spin_lock_irqsave(&l2c->pmu_lock, irq_flags);
	idx = cpu_l2c_get_counter_idx(l2c);
	if (WARN_ON_ONCE(idx == L2C_MAX_COUNTERS))
		return -ENOSPC;

	l2c->events[idx] = event;
	l2c->n_events++;

	__set_bit(idx, l2c->used_mask);
	raw_spin_unlock_irqrestore(&l2c->pmu_lock, irq_flags);
finish_add:
	hwc->idx = idx;
	hwc->state = PERF_HES_UPTODATE | PERF_HES_STOPPED;

        if (flags & PERF_EF_START)
                riscv_pmu->pmu->start(event, PERF_EF_RELOAD);

	return 0;
}

/*
 * pmu->del: delete the event from HPM.
 */
static void riscv_pmu_del(struct perf_event *event, int flags)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	struct l2c_hw_events *l2c = &l2c_hw_events;
	struct hw_perf_event *hwc = &event->hw;
	unsigned long irq_flags;

	if (is_l2c_event(hwc->config))
		goto l2c_events_del;

	cpuc->events[hwc->idx] = NULL;
	cpuc->n_events--;
	__clear_bit(hwc->idx, cpuc->used_mask);
	goto finish_del;

l2c_events_del:
	l2c->events[hwc->idx] = NULL;

	raw_spin_lock_irqsave(&l2c->pmu_lock, irq_flags);
	l2c->n_events--;
	raw_spin_unlock_irqrestore(&l2c->pmu_lock, irq_flags);

	__clear_bit(hwc->idx, l2c->used_mask);
finish_del:
	riscv_pmu->pmu->stop(event, PERF_EF_UPDATE);
	perf_event_update_userpage(event);
}

/*
 * Interrupt: a skeletion for reference.
 */

static DEFINE_MUTEX(pmc_reserve_mutex);

unsigned long riscv_pmu_get_overflow(void)
{
        return csr_read(CSR_SCOUNTEROVF);
}

void riscv_reset_overflow(unsigned long status)
{
        csr_write(CSR_SCOUNTEROVF ,status);
}

static atomic_t riscv_active_events = ATOMIC_INIT(0);

void riscv_base_pmu_handle_irq(struct pt_regs *regs)
{
        struct perf_sample_data data;
        struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
        struct perf_event *event;
        unsigned long status;
        int bit;

        if (!atomic_read(&riscv_active_events))
                return;

        if (!cpuc->n_events)
                return;

        status = riscv_pmu_get_overflow();
        riscv_reset_overflow(status);

        riscv_pmu_disable();
        for_each_set_bit(bit, &status, RISCV_MAX_COUNTERS) {
                event = cpuc->events[bit];

                if (!event)
                        continue;

                if (!test_bit(bit, cpuc->active_mask))
                        continue;

                riscv_pmu_read(event);

                perf_sample_data_init(&data, 0, event->hw.last_period);
                if (!riscv_event_set_period(event))
                        continue;

                if (perf_event_overflow(event, &data, regs))
                        riscv_pmu_stop(event, 0);
        }
        riscv_pmu_enable(event);
}

void riscv_perf_interrupt(struct pt_regs *regs)
{
        if (!perf_irq)
                panic("Unexpected Perf interrupt\n");

        perf_irq(regs);
		sbi_ecall(SBI_EXT_ANDES, SBI_EXT_ANDES_SET_PFM, 0, 0, 0, 0, 0, 0);
}

perf_irq_t reserve_pmc_hardware(perf_irq_t new_perf_irq)
{
        return cmpxchg(&perf_irq, NULL, new_perf_irq);
}

void release_pmc_hardware(void)
{
        perf_irq = NULL;
}

/*
 * Event Initialization/Finalization
 */

static void riscv_event_destroy(struct perf_event *event)
{
	if (atomic_dec_return(&riscv_active_events) == 0)
		release_pmc_hardware();
}

extern void __iomem *l2c_base;

static int riscv_event_init(struct perf_event *event)
{
	struct perf_event_attr *attr = &event->attr;
	struct hw_perf_event *hwc = &event->hw;
	perf_irq_t err = NULL;
	int code;

	switch (event->attr.type) {
	case PERF_TYPE_HARDWARE:
		code = riscv_pmu->map_hw_event(attr->config);
		break;
	case PERF_TYPE_HW_CACHE:
		code = riscv_pmu->map_cache_event(attr->config);
		break;
	case PERF_TYPE_RAW:
		code = riscv_pmu->map_raw_event(attr->config);
		break;
	default:
		return -ENOENT;
	}

	if (atomic_inc_return(&riscv_active_events) == 1) {
		err = reserve_pmc_hardware(riscv_base_pmu_handle_irq);

		if (err) {
			pr_warn("PMC hardware not available\n");
			atomic_dec(&riscv_active_events);
			return -EBUSY;
		}
	}

	if ((code >> L2C_MARK_OFF) == L2C_EVSEL_MASK && !l2c_base)
		code = -EINVAL;

	event->destroy = riscv_event_destroy;
	if (code < 0) {
		event->destroy(event);
		return code;
	}

        /*
         * Design of hwc->config:
         *      The lower 3 bits :
         *      _ _ _
         *      U S M
         *
         * Each bit present each mode for each counter.
         * The rest of bits are for event code.
         */

        if (attr->exclude_user)
                hwc->config |= UMODE_MASK;

        if (attr->exclude_kernel)
                hwc->config |= SMODE_MASK;

        if (attr->exclude_machine)
                hwc->config |= MMODE_MASK;


        if (!hwc->sample_period) {
                hwc->sample_period = riscv_pmu->max_period;
                hwc->last_period = hwc->sample_period;
                local64_set(&hwc->period_left, hwc->sample_period);
        }

	/*
	 * idx is set to -1 because the index of a general event should not be
	 * decided until binding to some counter in pmu->add().
	 */
	hwc->config |= (code << EVSEL_OFF);
	hwc->idx = -1;

	return 0;
}

/*
 * Initialization
 */
HPM_FORMAT_ATTR(event, "config:0-63");

static struct attribute *riscv_arch_formats_attr[] = {
        &format_attr_event.attr,
        NULL,
};

static struct attribute_group riscv_pmu_format_group = {
        .name = "format",
        .attrs = riscv_arch_formats_attr,
};

static ssize_t riscv_pmu_cpumask_show(struct device *dev,
                                      struct device_attribute *attr,
                                      char *buf)
{
        return cpumap_print_to_pagebuf(true, buf, &pmu_cpu);
}

static DEVICE_ATTR(cpus, 0444, riscv_pmu_cpumask_show, NULL);

static struct attribute *riscv_pmu_common_attrs[] = {
        &dev_attr_cpus.attr,
        NULL,
};

static struct attribute_group riscv_pmu_common_group = {
        .attrs = riscv_pmu_common_attrs,
};

static const struct attribute_group *riscv_pmu_attr_groups[] = {
        &riscv_pmu_format_group,
        &riscv_pmu_common_group,
        NULL,
};

static struct pmu min_pmu = {
#ifdef CONFIG_ANDES_HPM
	.name		= "andes-base",
#elif defined CONFIG_RISCV_BASE_HPM
	.name		= "riscv-base",
#endif
	.attr_groups    = riscv_pmu_attr_groups,
	.event_init	= riscv_event_init,
	.add		= riscv_pmu_add,
	.del		= riscv_pmu_del,
	.start		= riscv_pmu_start,
	.stop		= riscv_pmu_stop,
	.read		= riscv_pmu_read,
};

static const struct riscv_pmu riscv_base_pmu = {
	.pmu = &min_pmu,
	.max_events = ARRAY_SIZE(riscv_hw_event_map),
	.map_hw_event = riscv_map_hw_event,
	.hw_events = riscv_hw_event_map,
	.map_cache_event = riscv_map_cache_event,
	.map_raw_event = riscv_map_raw_event,
	.cache_events = &riscv_cache_event_map,
	.counter_width = 63,
	.num_counters = RISCV_MAX_COUNTERS - 1,
	.handle_irq = &riscv_base_pmu_handle_irq,
	.max_period = 0xFFFFFFFF,
	/* This means this HPM has no IRQ. */
	.irq = -1,
};

static const struct of_device_id riscv_pmu_of_ids[] = {
	{.compatible = "riscv,base-pmu",	.data = &riscv_base_pmu},
	{.compatible = "riscv,andes-pmu",	.data = &riscv_base_pmu},
	{ /* sentinel value */ }
};

void init_cpu_pmu(void *arg)
{
	// enable S-mode local interrupt and M-mode interrupt
	csr_write(CSR_SLIE, PFMOVF_MASK);
	sbi_ecall(SBI_EXT_ANDES, SBI_EXT_ANDES_SET_PFM, 0, 0, 0, 0, 0, 0);
}

int __init init_hw_perf_events(void)
{
	struct device_node *node = of_find_node_by_type(NULL, "pmu");
	const struct of_device_id *of_id;
	int cpu;

	pr_info("Perf: Legacy Andes perf implementation is available\n");
	raw_spin_lock_init(&l2c_hw_events.pmu_lock);
	l2c_hw_events.n_events = 0;
	riscv_pmu = &riscv_base_pmu;
	cpumask_setall(&pmu_cpu);
	/* The second bit we don't use*/
	for_each_cpu(cpu, &pmu_cpu) {
		struct cpu_hw_events *cpuc = per_cpu_ptr(&cpu_hw_events, cpu);

		__set_bit(1, cpuc->active_mask);
		__set_bit(1, cpuc->used_mask);
		smp_call_function_single(cpu, init_cpu_pmu, NULL, 1);
	}

	if (node) {
		of_id = of_match_node(riscv_pmu_of_ids, node);

		if (of_id)
			riscv_pmu = of_id->data;
		of_node_put(node);
	}
#ifdef CONFIG_ANDES_HPM
	perf_pmu_register(riscv_pmu->pmu, "andes-base", PERF_TYPE_RAW);
#elif defined CONFIG_RISCV_BASE_HPM
	perf_pmu_register(riscv_pmu->pmu, "riscv-base", PERF_TYPE_RAW);
#endif

	return 0;
}
arch_initcall(init_hw_perf_events);
