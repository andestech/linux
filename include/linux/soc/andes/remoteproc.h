/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Andes Technology Corporation.
 *
 * Authors:
 *   Locus Wei-Han Chen <locus84@andestech.com>
 */

#ifndef _LINUX_SOC_ANDES_REMOTEPROC_H
#define _LINUX_SOC_ANDES_REMOTEPROC_H

#define SP_BASE_HI			0x0
#define SP_BASE_LO			0x3f000000
#define MBOX_MSG			0x3fffc000
#define MBOX_MSG_SIZE			0x4
#define MBOX_SET_MSG			-1
#define MBOX_SP_RUN			0

#define MBOX_OFF			(offsetof(struct swmsg_box, sp_mbox))
#define SBI_OFF				(offsetof(struct swmsg_box, opensbi_mbox))
#define MP_ST_OFF			(offsetof(struct swmsg_box, mp_status))
#define SP_ST_OFF			(offsetof(struct swmsg_box, sp_status))

#define REMOTEPROC_EN			0x1
#define REMOTEPROC_DIS			0x0

struct andes_rproc_pdata {
	struct device *dev;
	struct rproc *rproc;
	int irq;
	u32 sp_hartid;
	u64 rmem_base;
	u64 rmem_size;
	void __iomem *mbox_msg;
	void __iomem *sp_base;
	void __iomem *revdmem_base;
};

/* SW definition message box for sp and for opensbi */

struct swmsg_box {
	u32 sp_mbox;
	u32 opensbi_mbox;
	u32 mp_status;
	u32 sp_status;
};

enum {
	RPROC_MP_INIT = 0,
	RPROC_MP_SBI_ENABLE,
	RPROC_MP_REMOVE,
	RPROC_MP_PAESE_FW,
	RPROC_MP_LOAD_ELF,
	RPROC_MP_REVD_MEM = 5,
	RPROC_MP_START,
	RPROC_MP_STOP,
	RPROC_MP_SEND_IPI,
	RPROC_MP_HANDLE_EVENT,
	RPROC_MP_HANDLE_EVENT_DONE = 10,
};

struct remoteproc_work {
	struct work_struct work;
	struct rproc *rproc;
};

struct remoteproc_check {
	void (*check) (void *info);
	void *info;
};

extern struct remoteproc_check rproc_check;

#endif /* !_LINUX_SOC_ANDES_REMOTEPROC_H */
