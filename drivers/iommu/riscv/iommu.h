// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright © 2022-2023 Rivos Inc.
 * Copyright © 2023 FORTH-ICS/CARV
 *
 * RISC-V Ziommu - IOMMU Interface Specification.
 *
 * Authors
 *	Tomasz Jeznach <tjeznach@rivosinc.com>
 *	Nick Kossifidis <mick@ics.forth.gr>
 */

#ifndef _RISCV_IOMMU_H_
#define _RISCV_IOMMU_H_

#include <linux/types.h>
#include <linux/iova.h>
#include <linux/io.h>
#include <linux/idr.h>
#include <linux/mmu_notifier.h>
#include <linux/list.h>
#include <linux/iommu.h>
#include <linux/io-pgtable.h>

#include "iommu-bits.h"

#define IOMMU_PAGE_SIZE_4K	BIT_ULL(12)
#define IOMMU_PAGE_SIZE_2M	BIT_ULL(21)
#define IOMMU_PAGE_SIZE_1G	BIT_ULL(30)
#define IOMMU_PAGE_SIZE_512G	BIT_ULL(39)

struct riscv_iommu_queue {
	dma_addr_t base_dma;	/* ring buffer bus address */
	void *base;		/* ring buffer pointer */
	size_t len;		/* single item length */
	u32 cnt;		/* items count */
	u32 lui;		/* last used index, consumer/producer share */
	unsigned qbr;		/* queue base register offset */
	unsigned qcr;		/* queue control and status register offset */
	int irq;		/* registered interrupt number */
};

enum riscv_queue_ids {
	RISCV_IOMMU_COMMAND_QUEUE	= 0,
	RISCV_IOMMU_FAULT_QUEUE		= 1,
	RISCV_IOMMU_PAGE_REQUEST_QUEUE	= 2
};

/* virtual iommu device representing all the real iommu hardware IPs and register to kernel iommu framework */
struct zh_iommu_device {
	struct iommu_device iommu;	/* iommu core interface */
	struct device *dev;		/* iommu hardware */
	struct list_head groups;	/* struct riscv_iommu_group{} existed */
	struct list_head iommus;	/* struct riscv_iommu_device{} for individual iommu IPs existed*/
	struct mutex lock;		/* protect modification of member: groups,iommus */
	struct device **pd_devs;
	int num_pds;
	struct clk_bulk_data *clks;
	u32 num_clks;
};

/* real iommu hardware IP */
struct riscv_iommu_device {
	struct device *dev;		/* iommu hardware */
	char *name;

	/* hardware control register space */
	void __iomem *reg;
	resource_size_t reg_phys;

	/* IRQs for the various queues */
	int irq;

	/* Queue lengths */
	int cmdq_len;
	int fltq_len;

	/* supported and enabled hardware capabilities */
	u64 cap;

	/* global lock, to be removed */
	spinlock_t cq_lock;

	/* device directory table root pointer and mode */
	unsigned long ddtp; // va
	unsigned ddt_mode;

	/* hardware queues */
	struct riscv_iommu_queue cmdq;
	struct riscv_iommu_queue fltq;

	/* Connected end-points */
	struct rb_root eps;
	struct mutex eps_mutex;

	struct list_head list;
};

struct riscv_iommu_domain {
	struct iommu_domain domain;
	struct io_pgtable pgtbl;

	struct list_head endpoints;
	struct list_head notifiers;
	struct mutex lock;
	struct mmu_notifier mn;
	struct zh_iommu_device *iommus;

	unsigned mode;		/* RIO_ATP_MODE_* enum */
	unsigned pscid;		/* RISC-V IOMMU PSCID / GSCID */

	pgd_t *pgd_root;	/* page table root pointer */
};

/* Private dev_iommu_priv object, device-domain relationship. */
struct riscv_iommu_endpoint {
	struct device *dev;			/* platform or PCI endpoint device */
	unsigned num_ids;
	unsigned domid;    			/* PCI domain number, segment */
	struct rb_node node;    		/* device tracking node (lookup by devid) */
	struct zh_iommu_device *iommus;		/* virtual iommu device */
	struct riscv_iommu_device *iommu;	/* real parent iommu device */
	struct mutex lock;
	struct list_head domain;		/* endpoint attached managed domain */
	struct riscv_iommu_dc **dc;		/* device context pointer entries */
	u32 *devids;      	/* PCI bus:device:function number */
	unsigned int group_id;	/* desire to join which iommu_group */
};

/* include one instance of struct iommu_group and it's managing tools */
struct riscv_iommu_group {
	struct list_head list;
	struct zh_iommu_device *iommus;
	struct iommu_group *group;
	unsigned int group_id;
};

/* Helper functions and macros */

static inline u32 riscv_iommu_readl(struct riscv_iommu_device *iommu,
				    unsigned offset)
{
	return readl_relaxed(iommu->reg + offset);
}

static inline void riscv_iommu_writel(struct riscv_iommu_device *iommu,
				      unsigned offset, u32 val)
{
	writel_relaxed(val, iommu->reg + offset);
}

static inline u64 riscv_iommu_readq(struct riscv_iommu_device *iommu,
				    unsigned offset)
{
	return readq_relaxed(iommu->reg + offset);
}

static inline void riscv_iommu_writeq(struct riscv_iommu_device *iommu,
				      unsigned offset, u64 val)
{
	writeq_relaxed(val, iommu->reg + offset);
}

int riscv_iommu_init(struct riscv_iommu_device *iommu);
void riscv_iommu_remove(struct zh_iommu_device *iommus);

int riscv_iommu_sysfs_add(struct zh_iommu_device *iommus);
int riscv_iommu_register(struct zh_iommu_device *iommus);

#endif
