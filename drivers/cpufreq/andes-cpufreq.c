// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Andes Technology Corporation
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/timex.h>
#include <linux/of_platform.h>

#include <soc/andes/sbi.h>

#define ANDES_PB_LV_MASK	0xF0
#define ANDES_PB_LV_OFF	4

#define ANDES_PB_NUM_LV	16
#define ANDES_PB_LOW_LV	15
#define ANDES_PB_HIGH_LV	0
#define ANDES_PB_PERIOD	(policy->cpuinfo.max_freq / ANDES_PB_NUM_LV)

void read_powerbrake(void *arg)
{
	long ret;
	unsigned int *val = arg;

	ret = sbi_andes_read_powerbrake();
	*val = (ret & ANDES_PB_LV_MASK) >> ANDES_PB_LV_OFF;
}

void write_powerbrake(void *arg)
{
	unsigned int *val = arg;

	sbi_andes_write_powerbrake(*val);
}

static unsigned int andes_cpufreq_get(unsigned int cpu)
{
	unsigned int val, max;
	struct cpufreq_policy *policy = cpufreq_cpu_get(cpu);

	smp_call_function_single(cpu, read_powerbrake, &val, 1);

	max = (ANDES_PB_LOW_LV - val + 1) * ANDES_PB_PERIOD;
	return max;
}

static int andes_cpufreq_set_policy(struct cpufreq_policy *policy)
{
	int i;
	unsigned int cpu, val;

	cpu = policy->cpu;
	if (!policy)
		return -EINVAL;

	if (policy->min == 0 && policy->max == 0) {
		pr_err("Cannot set zero freq\n");
		return -EINVAL;
	}
	val = (policy->min + policy->max) / 2;
	switch (policy->policy) {
	case CPUFREQ_POLICY_POWERSAVE:
		val = (val + policy->min) / 2;
		break;
	case CPUFREQ_POLICY_PERFORMANCE:
		val = (val + policy->max) / 2;
		break;
	default:
		pr_err("Not Support this governor\n");
		break;
	}

	if (val < 0) {
		pr_err("The freq is valid\n");
		return -EINVAL;
	}

	/*
	 * PowerBrake register has 16 level,
	 * so we divide the xxxkHZ into 16 parts.
	 *
	 * EX: 100MHZ->100*1000kHZ
	 *	|    |    |........|
	 * Mhz	0  6250 12500    16*6250
	 */

	// transfer MHZ to kHZ, and divided to 16 level
	for (i = 1; i <= ANDES_PB_NUM_LV; i++) {
		if (val <= i * ANDES_PB_PERIOD) {
			val = ANDES_PB_LOW_LV - (i - 1);
			break;
		}
	}
	val = val << ANDES_PB_LV_OFF;

	return smp_call_function_single(cpu, write_powerbrake, &val, 1);
}

static int andes_cpufreq_verify_policy(struct cpufreq_policy_data *policy)
{
	if (!policy)
		return -EINVAL;

	cpufreq_verify_within_cpu_limits(policy);

	return 0;
}

static int andes_get_policy(struct cpufreq_policy *policy)
{
	unsigned int val;

	if (!policy)
		return -EINVAL;

	smp_call_function_single(policy->cpu, read_powerbrake, &val, 1);

	/*
	 * PowerBrake register has 16 level,
	 * so we divide the xxxkHZ into 16 parts.
	 *
	 * EX: 100MHZ->100*1000kHZ
	 *	|    |    |........|
	 * Mhz	0  6250 12500    16*6250
	 */
	policy->min = (ANDES_PB_LOW_LV - val) * ANDES_PB_PERIOD;
	policy->max = (ANDES_PB_LOW_LV - val + 1) * ANDES_PB_PERIOD;
	policy->policy = CPUFREQ_POLICY_PERFORMANCE;
	return 0;
}

static int andes_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	int ret;
	u32 max_freq;
	struct device_node *cpu;

	if (!policy)
		return -EINVAL;

	cpu = of_get_cpu_node(policy->cpu, NULL);

	if (!cpu)
		return -ENODEV;

	pr_debug("init cpufreq on CPU %d\n", policy->cpu);
	if (of_property_read_u32(cpu, "clock-frequency", &max_freq)) {
		pr_err("%s missing clock-frequency\n", cpu->name);
		return -EINVAL;
	}
	of_node_put(cpu);

	policy->cpuinfo.min_freq = 0;
	policy->cpuinfo.max_freq = max_freq / 1000; /* kHZ */
	ret = andes_get_policy(policy);
	if (ret)
		return ret;

	return 0;
}

static struct cpufreq_driver andes_cpufreq_driver = {
	.flags		= CPUFREQ_CONST_LOOPS,
	.verify		= andes_cpufreq_verify_policy,
	.setpolicy	= andes_cpufreq_set_policy,
	.get		= andes_cpufreq_get,
	.init		= andes_cpufreq_cpu_init,
	.name		= "andes_cpufreq",
};

static int __init andes_cpufreq_init(void)
{
	return cpufreq_register_driver(&andes_cpufreq_driver);
}

static void __exit andes_cpufreq_exit(void)
{
	cpufreq_unregister_driver(&andes_cpufreq_driver);
}

MODULE_DESCRIPTION("PowerBrake driver for Andes processors");
MODULE_LICENSE("GPL");
module_init(andes_cpufreq_init);
module_exit(andes_cpufreq_exit);
