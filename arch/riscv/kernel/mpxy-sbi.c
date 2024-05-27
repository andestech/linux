// SPDX-License-Identifier: GPL-2.0-only
/*
 * RISC-V RPMI Proxy (MPXY) Helper functions
 *
 * Copyright (C) 2024 Ventana Micro Systems Inc.
 */

#define pr_fmt(fmt) "riscv-mpxy: " fmt

#include <linux/jump_label.h>
#include <linux/percpu.h>
#include <linux/mm.h>
#include <linux/cpu.h>
#include <linux/smp.h>
#include <asm/sbi.h>

struct sbi_mpxy {
	void *shmem;
	phys_addr_t shmem_phys_addr;
	bool active;
};

DEFINE_PER_CPU(struct sbi_mpxy, sbi_mpxy);
DEFINE_STATIC_KEY_FALSE(sbi_mpxy_available);

#define sbi_mpxy_available() \
	static_branch_unlikely(&sbi_mpxy_available)

static int sbi_mpxy_exit(unsigned int cpu)
{
	struct sbiret sret;
	struct sbi_mpxy *mpxy;

	if (!sbi_mpxy_available())
		return -ENODEV;

	mpxy = per_cpu_ptr(&sbi_mpxy, cpu);
	if (!mpxy->shmem)
		return -ENOMEM;

	free_pages((unsigned long)mpxy->shmem, get_order(PAGE_SIZE));

	sret = sbi_ecall(SBI_EXT_MPXY, SBI_EXT_MPXY_SET_SHMEM,
			 0, -1U, -1U, 0, 0, 0);

	if (sret.error) {
		pr_err("Shared memory disabling failed for cpu-%d\n", cpu);
		return sbi_err_map_linux_errno(sret.error);
	}

	pr_info("Shared memory disabled for cpu-%d\n", cpu);

	mpxy->shmem = NULL;
	mpxy->shmem_phys_addr = 0;
	mpxy->active = false;

	return 0;
}

/**
 * Setup shared memory for a CPU, For linux clients
 * this function will automatically be called by the
 * MPXY interface to setup per cpu shared memory.
 * For non-linux clients(eg EFI runtime) separate
 * MPXY SBI function needs to be called.
 */
static int __sbi_mpxy_setup_shmem(unsigned int cpu)
{
	struct sbiret sret;
	struct page *shmem_page;
	struct sbi_mpxy *mpxy;

	if (!sbi_mpxy_available())
		return -ENODEV;

	mpxy = per_cpu_ptr(&sbi_mpxy, cpu);
	if (mpxy->active)
		return -EINVAL;

	shmem_page = alloc_pages(GFP_KERNEL | __GFP_ZERO,
				 get_order(PAGE_SIZE));
	if (!shmem_page) {
		sbi_mpxy_exit(cpu);
		pr_err("Shared memory setup failed for cpu-%d\n", cpu);
		return -ENOMEM;
	}
	mpxy->shmem = page_to_virt(shmem_page);
	mpxy->shmem_phys_addr = page_to_phys(shmem_page);

	/**
	 * Linux setup of shmem is done in mpxy OVERWRITE mode.
	 * flags[1:0] = 00b
	 **/
	sret = sbi_ecall(SBI_EXT_MPXY, SBI_EXT_MPXY_SET_SHMEM,
			 PAGE_SIZE, mpxy->shmem_phys_addr, 0, 0, 0, 0);
	if (sret.error) {
		sbi_mpxy_exit(cpu);
		return sbi_err_map_linux_errno(sret.error);
	}

	mpxy->active = true;

	return 0;
}

int sbi_mpxy_read_attrs(u32 channelid, u32 base_attrid, u32 attr_count,
			void *attrs_buf)
{
	struct sbiret sret;
	struct sbi_mpxy *mpxy = this_cpu_ptr(&sbi_mpxy);

	if (!sbi_mpxy_available() || !mpxy->active)
		return -ENODEV;

	if (!attr_count || !attrs_buf)
		return -EINVAL;

	get_cpu();
	sret = sbi_ecall(SBI_EXT_MPXY, SBI_EXT_MPXY_READ_ATTRS,
			 channelid, base_attrid, attr_count, 0, 0, 0);
	if (!sret.error) {
		memcpy(attrs_buf, mpxy->shmem, attr_count * sizeof(u32));
	}
	put_cpu();

	return sbi_err_map_linux_errno(sret.error);
}

int sbi_mpxy_write_attrs(u32 channelid, u32 base_attrid, u32 attr_count,
			void *attrs_buf)
{
	struct sbiret sret;
	struct sbi_mpxy *mpxy = this_cpu_ptr(&sbi_mpxy);

	if (!sbi_mpxy_available() || !mpxy->active)
		return -ENODEV;

	if (!attr_count || !attrs_buf)
		return -EINVAL;

	get_cpu();
	memcpy(mpxy->shmem, attrs_buf, attr_count * sizeof(u32));

	sret = sbi_ecall(SBI_EXT_MPXY, SBI_EXT_MPXY_WRITE_ATTRS,
			 channelid, base_attrid, attr_count, 0, 0, 0);
	put_cpu();

	return sbi_err_map_linux_errno(sret.error);
}

int sbi_mpxy_send_message_withresp(u32 channelid, u32 msgid,
				   void *tx, unsigned long tx_msglen,
				   void *rx, unsigned long *rx_msglen)
{
	struct sbiret sret;
	struct sbi_mpxy *mpxy = this_cpu_ptr(&sbi_mpxy);

	if (!sbi_mpxy_available() || !mpxy->active)
		return -ENODEV;

	get_cpu();
	/**
	 * Message protocols allowed to have no data in
	 * messages
	 */
	if (tx_msglen)
		memcpy(mpxy->shmem, tx, tx_msglen);

	sret = sbi_ecall(SBI_EXT_MPXY, SBI_EXT_MPXY_SEND_MSG_WITH_RESP,
			 channelid, msgid, tx_msglen, 0, 0, 0);

	if (rx && !sret.error) {
		memcpy(rx, mpxy->shmem, sret.value);
		if (rx_msglen)
			*rx_msglen = sret.value;
	}

	put_cpu();

	return sbi_err_map_linux_errno(sret.error);
}

int sbi_mpxy_send_message_noresp(u32 channelid, u32 msgid,
				 void *tx, unsigned long tx_msglen)
{
	struct sbiret sret;
	struct sbi_mpxy *mpxy = this_cpu_ptr(&sbi_mpxy);

	if (!sbi_mpxy_available() || !mpxy->active)
		return -ENODEV;

	get_cpu();
	/**
	 * Message protocols allowed to have no data in
	 * messages.
	 */
	if (tx_msglen)
		memcpy(mpxy->shmem, tx, tx_msglen);

	sret = sbi_ecall(SBI_EXT_MPXY, SBI_EXT_MPXY_SEND_MSG_NO_RESP,
			 channelid, msgid, tx_msglen, 0, 0, 0);

	put_cpu();

	return sbi_err_map_linux_errno(sret.error);
}

int sbi_mpxy_get_notifications(u32 channelid, void *rx,
			       unsigned long *rx_msglen)
{
	struct sbiret sret;
	struct sbi_mpxy *mpxy = this_cpu_ptr(&sbi_mpxy);

	if (!sbi_mpxy_available() || !mpxy->active)
		return -ENODEV;

	if (!rx)
		return -EINVAL;

	get_cpu();
	sret = sbi_ecall(SBI_EXT_MPXY, SBI_EXT_MPXY_GET_NOTIFICATIONS,
			 channelid, 0, 0, 0, 0, 0);
	if (!sret.error) {
		memcpy(rx, mpxy->shmem, sret.value);
		if (rx_msglen)
			*rx_msglen = sret.value;
	}
	put_cpu();

	return sbi_err_map_linux_errno(sret.error);
}

static int __init sbi_mpxy_init(void)
{
	if ((sbi_spec_version < sbi_mk_version(1, 0)) ||
		sbi_probe_extension(SBI_EXT_MPXY) <= 0) {
		pr_info("SBI MPXY extension missing\n");
		return -ENODEV;
	}

	static_branch_enable(&sbi_mpxy_available);
	pr_info("SBI MPXY extension detected\n");
	/*
	 * Setup CPUHP notifier to setup shared
	 * memory on all CPUs
	 */
	cpuhp_setup_state(CPUHP_AP_ONLINE_DYN,
			  "riscv/mpxy-sbi:cpu-shmem-init",
			  __sbi_mpxy_setup_shmem,
			  sbi_mpxy_exit);
	return 0;
}

arch_initcall(sbi_mpxy_init);
