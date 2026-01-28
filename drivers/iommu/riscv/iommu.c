// SPDX-License-Identifier: GPL-2.0-only
/*
 * IOMMU API for RISC-V architected Ziommu implementations.
 *
 * Copyright © 2022-2023 Rivos Inc.
 * Copyright © 2023 FORTH-ICS/CARV
 *
 * Authors
 *	Tomasz Jeznach <tjeznach@rivosinc.com>
 *	Nick Kossifidis <mick@ics.forth.gr>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bitfield.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/compiler.h>
#include <linux/init.h>
#include <linux/completion.h>
#include <linux/uaccess.h>
#include <linux/iommu.h>
#include <linux/irqdomain.h>
#include <linux/platform_device.h>
#include <linux/dma-map-ops.h>
#include <linux/delay.h>
#include <asm/page.h>

#include "../dma-iommu.h"
#include "iommu.h"

#include <asm/csr.h>
#include <asm/delay.h>

#include <dt-bindings/iommu/zh-iommu.h>

MODULE_DESCRIPTION("IOMMU driver for RISC-V architected Ziommu implementations");
MODULE_AUTHOR("Tomasz Jeznach <tjeznach@rivosinc.com>");
MODULE_AUTHOR("Nick Kossifidis <mick@ics.forth.gr>");
MODULE_ALIAS("riscv-iommu");
MODULE_LICENSE("GPL v2");

/* Global IOMMU params. */
static int ddt_mode = RISCV_IOMMU_DDTP_MODE_2LVL;
module_param(ddt_mode, int, 0644);
MODULE_PARM_DESC(ddt_mode, "Device Directory Table mode.");

static int cmdq_length = 256;
module_param(cmdq_length, int, 0644);
MODULE_PARM_DESC(cmdq_length, "Command queue length.");

static int fltq_length = 256;
module_param(fltq_length, int, 0644);
MODULE_PARM_DESC(fltq_length, "Fault queue length.");

/* IOMMU PSCID allocation namespace. */
#define RISCV_IOMMU_MAX_PSCID	(1U << 20)
static DEFINE_IDA(riscv_iommu_pscids);

/* 1 second */
#define RISCV_IOMMU_TIMEOUT	riscv_timebase

/* RISC-V IOMMU PPN <> PHYS address conversions, PHYS <=> PPN[53:10]
convert directly from Figure 61. Sv39 physical address to Figure 38. Command-queue base register fields
* for phys_to_ppn
* ((1ULL << 44) - 1) << 10) will clear lower 10bits anyway.so (((pa) >> 2) just shift 2 more bits. shift 12 bits in total.
* for ppn_to_phys
* input pn has already 10 lower zero bits. shift 2 bits so that it has 12 lower zero bits in total.
 */
#define phys_to_ppn(pa)	 (((pa) >> 2) & (((1ULL << 44) - 1) << 10))
#define ppn_to_phys(pn)	 (((pn) << 2) & (((1ULL << 44) - 1) << 12))

#define iommu_domain_to_riscv(iommu_domain) \
    container_of(iommu_domain, struct riscv_iommu_domain, domain)

#define iommu_device_to_riscv(iommu_device) \
    container_of(iommu_device, struct riscv_iommu, iommu)

static const struct iommu_domain_ops riscv_iommu_domain_ops;
static const struct iommu_ops riscv_iommu_ops;

/*
 * Common queue management routines
 */

/* Note: offsets are the same for all queues */
#define Q_HEAD(q) ((q)->qbr + (RISCV_IOMMU_REG_CQH - RISCV_IOMMU_REG_CQB))
#define Q_TAIL(q) ((q)->qbr + (RISCV_IOMMU_REG_CQT - RISCV_IOMMU_REG_CQB))

static int riscv_iommu_enable(struct riscv_iommu_device *iommu, unsigned requested_mode);

static unsigned riscv_iommu_queue_consume(struct riscv_iommu_device *iommu,
					  struct riscv_iommu_queue *q, unsigned *ready)
{
	u32 tail = riscv_iommu_readl(iommu, Q_TAIL(q));
	*ready = q->lui;

	BUG_ON(q->cnt <= tail);
	if (q->lui <= tail) // not wrapped
		return tail - q->lui;
	return q->cnt - q->lui; // wrapped.should be tail + q->cnt - q->lui
}

static void riscv_iommu_queue_release(struct riscv_iommu_device *iommu,
				      struct riscv_iommu_queue *q, unsigned count)
{
	q->lui = (q->lui + count) & (q->cnt - 1);
	riscv_iommu_writel(iommu, Q_HEAD(q), q->lui);
}

static u32 riscv_iommu_queue_ctrl(struct riscv_iommu_device *iommu,
				  struct riscv_iommu_queue *q, u32 val)
{
	cycles_t end_cycles = RISCV_IOMMU_TIMEOUT + get_cycles();

	riscv_iommu_writel(iommu, q->qcr, val);
	do {
		val = riscv_iommu_readl(iommu, q->qcr);
		if (!(val & RISCV_IOMMU_QUEUE_BUSY))
			break;
		cpu_relax();
	} while (get_cycles() < end_cycles);

	return val;
}

static void riscv_iommu_queue_free(struct riscv_iommu_device *iommu,
				   struct riscv_iommu_queue *q)
{
	size_t size = q->len * q->cnt;

	riscv_iommu_queue_ctrl(iommu, q, 0);

	if (q->base) {
		dmam_free_coherent(iommu->dev, size, q->base, q->base_dma);
	}
	if (q->irq)
		free_irq(q->irq, q);
}

static irqreturn_t riscv_iommu_cmdq_irq_check(int irq, void *data);
static irqreturn_t riscv_iommu_cmdq_process(int irq, void *data);
static irqreturn_t riscv_iommu_fltq_irq_check(int irq, void *data);
static irqreturn_t riscv_iommu_fltq_process(int irq, void *data);

static int riscv_iommu_queue_init(struct riscv_iommu_device *iommu, int queue_id)
{
	struct device *dev = iommu->dev;
	struct riscv_iommu_queue *q = NULL;
	size_t queue_size = 0;
	irq_handler_t irq_check;
	irq_handler_t irq_process;
	const char *name;
	int count = 0;
	int irq = 0;
	unsigned order = 0;
	u64 qbr_val = 0;
	u64 qbr_readback = 0;
	int ret = 0;

	switch (queue_id) {
	case RISCV_IOMMU_COMMAND_QUEUE:
		q = &iommu->cmdq;
		q->len = sizeof(struct riscv_iommu_command);
		count = iommu->cmdq_len;
		irq = iommu->irq;
		irq_check = riscv_iommu_cmdq_irq_check;
		irq_process = riscv_iommu_cmdq_process;
		q->qbr = RISCV_IOMMU_REG_CQB;
		q->qcr = RISCV_IOMMU_REG_CQCSR;
		name = "cmdq";
		break;
	case RISCV_IOMMU_FAULT_QUEUE:
		q = &iommu->fltq;
		q->len = sizeof(struct riscv_iommu_fq_record);
		count = iommu->fltq_len;
		irq = iommu->irq;
		irq_check = riscv_iommu_fltq_irq_check;
		irq_process = riscv_iommu_fltq_process;
		q->qbr = RISCV_IOMMU_REG_FQB;
		q->qcr = RISCV_IOMMU_REG_FQCSR;
		name = "fltq";
		break;
	default:
		dev_err(dev, "invalid queue interrupt index in queue_init!\n");
		return -EINVAL;
	}

	/* Polling not implemented */
	if (!irq)
		return -ENODEV;

	/* Allocate queue in memory and set the base register */
	order = ilog2(count);
	do {
		queue_size = q->len * (1ULL << order);
		q->base = dmam_alloc_coherent(dev, queue_size, &q->base_dma, GFP_KERNEL);
		if (q->base || queue_size < PAGE_SIZE)
			break;

		order--;
	} while (1);

	if (!q->base) {
		dev_err(dev, "failed to allocate %s queue (cnt: %u)\n", name, count);
		return -ENOMEM;
	}

	q->cnt = 1ULL << order; // q->cnt is the actual order allocated.

	qbr_val = phys_to_ppn(q->base_dma) |
	    FIELD_PREP(RISCV_IOMMU_QUEUE_LOGSZ_FIELD, order - 1);

	riscv_iommu_writeq(iommu, q->qbr, qbr_val);

	/*
	 * Queue base registers are WARL, so it's possible that whatever we wrote
	 * there was illegal/not supported by the hw in which case we need to make
	 * sure we set a supported PPN and/or queue size.
	 */
	qbr_readback = riscv_iommu_readq(iommu, q->qbr);
	if (qbr_readback == qbr_val)
		goto irq;

	dmam_free_coherent(dev, queue_size, q->base, q->base_dma);

	/* Get supported queue size */
	order = FIELD_GET(RISCV_IOMMU_QUEUE_LOGSZ_FIELD, qbr_readback) + 1;
	q->cnt = 1ULL << order;
	queue_size = q->len * q->cnt;

	/*
	* We only failed to set the queue size, re-try to allocate memory with
	* the queue size supported by the hw.
	*/
	dev_info(dev, "hardcoded queue size in %s base register\n", name);
	dev_info(dev, "retrying with queue length: %i\n", q->cnt);
	q->base = dmam_alloc_coherent(dev, queue_size, &q->base_dma, GFP_KERNEL);
	if (!q->base) {
		dev_err(dev, "failed to allocate %s queue (cnt: %u)\n",
			name, q->cnt);
		return -ENOMEM;
	}

	qbr_val = phys_to_ppn(q->base_dma) |
	    FIELD_PREP(RISCV_IOMMU_QUEUE_LOGSZ_FIELD, order - 1);
	riscv_iommu_writeq(iommu, q->qbr, qbr_val);

	/* Final check to make sure hw accepted our write */
	qbr_readback = riscv_iommu_readq(iommu, q->qbr);
	if (qbr_readback != qbr_val) {
		dev_err(dev, "failed to set base register for %s\n", name);
		goto fail;
	}

 irq:
	if (request_threaded_irq(irq, irq_check, irq_process, IRQF_ONESHOT | IRQF_SHARED,
				 dev_name(dev), q)) {
		dev_err(dev, "fail to request irq %d for %s\n", irq, name);
		goto fail;
	}

	q->irq = irq;

	/* Note: All RIO_xQ_EN/IE fields are in the same offsets */
	ret =
	    riscv_iommu_queue_ctrl(iommu, q,
				   RISCV_IOMMU_QUEUE_ENABLE |
				   RISCV_IOMMU_QUEUE_INTR_ENABLE);
	if (ret & RISCV_IOMMU_QUEUE_BUSY) {
		dev_err(dev, "%s init timeout\n", name);
		ret = -EBUSY;
		goto fail;
	}

	return 0;

 fail:
	riscv_iommu_queue_free(iommu, q);
	return ret;
}

/*
 * I/O MMU Command queue chapter 3.1
 */

static inline void riscv_iommu_cmd_inval_vma(struct riscv_iommu_command *cmd)
{
	cmd->dword0 =
	    FIELD_PREP(RISCV_IOMMU_CMD_OPCODE,
		       RISCV_IOMMU_CMD_IOTINVAL_OPCODE) | FIELD_PREP(RISCV_IOMMU_CMD_FUNC,
								     RISCV_IOMMU_CMD_IOTINVAL_FUNC_VMA);
	cmd->dword1 = 0;
}

static inline void riscv_iommu_cmd_inval_set_addr(struct riscv_iommu_command *cmd,
						  u64 addr)
{
	cmd->dword0 |= RISCV_IOMMU_CMD_IOTINVAL_AV;
	cmd->dword1 = (addr >> 12) << 10;
}

static inline void riscv_iommu_cmd_inval_set_pscid(struct riscv_iommu_command *cmd,
						   unsigned pscid)
{
	cmd->dword0 |= FIELD_PREP(RISCV_IOMMU_CMD_IOTINVAL_PSCID, pscid) |
	    RISCV_IOMMU_CMD_IOTINVAL_PSCV;
}

static inline void riscv_iommu_cmd_iofence(struct riscv_iommu_command *cmd)
{
	cmd->dword0 = FIELD_PREP(RISCV_IOMMU_CMD_OPCODE, RISCV_IOMMU_CMD_IOFENCE_OPCODE) |
	    FIELD_PREP(RISCV_IOMMU_CMD_FUNC, RISCV_IOMMU_CMD_IOFENCE_FUNC_C);
	cmd->dword1 = 0;
}

static inline void riscv_iommu_cmd_iodir_inval_ddt(struct riscv_iommu_command *cmd)
{
	cmd->dword0 = FIELD_PREP(RISCV_IOMMU_CMD_OPCODE, RISCV_IOMMU_CMD_IODIR_OPCODE) |
	    FIELD_PREP(RISCV_IOMMU_CMD_FUNC, RISCV_IOMMU_CMD_IODIR_FUNC_INVAL_DDT);
	cmd->dword1 = 0;
}

static inline void riscv_iommu_cmd_iodir_set_did(struct riscv_iommu_command *cmd,
						 unsigned devid)
{
	cmd->dword0 |=
	    FIELD_PREP(RISCV_IOMMU_CMD_IODIR_DID, devid) | RISCV_IOMMU_CMD_IODIR_DV;
}

/* TODO: Convert into lock-less MPSC implementation. */
static void riscv_iommu_post_sync(struct zh_iommu_device *iommus,
				  struct riscv_iommu_command *cmd, bool sync)
{
	u32 head, tail, next, last;
	unsigned long flags;
	struct riscv_iommu_device *iommu;

	list_for_each_entry(iommu, &iommus->iommus, list) {
		spin_lock_irqsave(&iommu->cq_lock, flags);
		head = riscv_iommu_readl(iommu, RISCV_IOMMU_REG_CQH) & (iommu->cmdq.cnt - 1);
		tail = riscv_iommu_readl(iommu, RISCV_IOMMU_REG_CQT) & (iommu->cmdq.cnt - 1);
		last = iommu->cmdq.lui;
		if (tail != last) {
			spin_unlock_irqrestore(&iommu->cq_lock, flags);
			/*
			* FIXME: This is a workaround for dropped MMIO writes/reads on QEMU platform.
			*        While debugging of the problem is still ongoing, this provides
			*        a simple impolementation of try-again policy.
			*        Will be changed to lock-less algorithm in the feature.
			*/
			dev_dbg(iommu->dev, "IOMMU CQT: %x != %x (1st)\n", last, tail);
			spin_lock_irqsave(&iommu->cq_lock, flags);
			tail =
			riscv_iommu_readl(iommu, RISCV_IOMMU_REG_CQT) & (iommu->cmdq.cnt - 1);
			last = iommu->cmdq.lui;
			if (tail != last) {
				spin_unlock_irqrestore(&iommu->cq_lock, flags);
				dev_dbg(iommu->dev, "IOMMU CQT: %x != %x (2nd)\n", last, tail);
				spin_lock_irqsave(&iommu->cq_lock, flags);
			}
		}

		next = (last + 1) & (iommu->cmdq.cnt - 1);
		if (next != head) {
			struct riscv_iommu_command *ptr = iommu->cmdq.base;
			ptr[last] = *cmd;
			dma_wmb();
			riscv_iommu_writel(iommu, RISCV_IOMMU_REG_CQT, next);
			iommu->cmdq.lui = next;
		}

		spin_unlock_irqrestore(&iommu->cq_lock, flags);

		if (sync && head != next) {
			cycles_t start_time = get_cycles();
			while (1) {
				last = riscv_iommu_readl(iommu, RISCV_IOMMU_REG_CQH) &
				(iommu->cmdq.cnt - 1);
				if (head < next && last >= next)
					break;
				if (head > next && last < head && last >= next)
					break;
				if (RISCV_IOMMU_TIMEOUT < (get_cycles() - start_time)) {
					dev_err(iommu->dev, "IOFENCE TIMEOUT\n");
					return;
				}
				cpu_relax();
			}
		}
	}

	return;
}

static void riscv_iommu_post(struct zh_iommu_device *iommus,
			     struct riscv_iommu_command *cmd)
{
	return riscv_iommu_post_sync(iommus, cmd, false);
}

static void riscv_iommu_iodir_inv_devid(struct zh_iommu_device *iommus, unsigned devid)
{
	struct riscv_iommu_command cmd;
	riscv_iommu_cmd_iodir_inval_ddt(&cmd);
	riscv_iommu_cmd_iodir_set_did(&cmd, devid);
	return riscv_iommu_post(iommus, &cmd);
}

static void riscv_iommu_iofence_sync(struct zh_iommu_device *iommus)
{
	struct riscv_iommu_command cmd;
	riscv_iommu_cmd_iofence(&cmd);
	return riscv_iommu_post_sync(iommus, &cmd, true);
}

/* Command queue primary interrupt handler */
static irqreturn_t riscv_iommu_cmdq_irq_check(int irq, void *data)
{
	struct riscv_iommu_queue *q = (struct riscv_iommu_queue *)data;
	struct riscv_iommu_device *iommu =
	    container_of(q, struct riscv_iommu_device, cmdq);
	u32 ipsr = riscv_iommu_readl(iommu, RISCV_IOMMU_REG_IPSR);
	if (ipsr & RISCV_IOMMU_IPSR_CIP)
		return IRQ_WAKE_THREAD;
	return IRQ_NONE;
}

static void riscv_iommu_dump_regs(struct riscv_iommu_device *iommu)
{
	dev_warn_ratelimited(iommu->dev,
		"%s iommu dump regs: CAP=0x%llx\n FCTL=0x%x\n DDTP=0x%llx\n CQB=0x%llx\n CQH=0x%x\n CQT=0x%x\n CQCSR=0x%x\n",
		iommu->name,
		riscv_iommu_readq(iommu, RISCV_IOMMU_REG_CAP),
		riscv_iommu_readl(iommu, RISCV_IOMMU_REG_FCTL),
		riscv_iommu_readq(iommu, RISCV_IOMMU_REG_DDTP),
		riscv_iommu_readq(iommu, RISCV_IOMMU_REG_CQB),
		riscv_iommu_readl(iommu, RISCV_IOMMU_REG_CQH),
		riscv_iommu_readl(iommu, RISCV_IOMMU_REG_CQT),
		riscv_iommu_readl(iommu, RISCV_IOMMU_REG_CQCSR));
}

/* Command queue interrupt hanlder thread function */
static irqreturn_t riscv_iommu_cmdq_process(int irq, void *data)
{
	struct riscv_iommu_queue *q = (struct riscv_iommu_queue *)data;
	struct riscv_iommu_device *iommu;
	unsigned ctrl;

	iommu = container_of(q, struct riscv_iommu_device, cmdq);

	/* Error reporting, clear error reports if any. */
	ctrl = riscv_iommu_readl(iommu, RISCV_IOMMU_REG_CQCSR);
	if (ctrl & (RISCV_IOMMU_CQCSR_CQMF |
		    RISCV_IOMMU_CQCSR_CMD_TO | RISCV_IOMMU_CQCSR_CMD_ILL)) {
		riscv_iommu_queue_ctrl(iommu, &iommu->cmdq, ctrl);
		dev_err(iommu->dev,
				     "Command queue error: fault: %d tout: %d err: %d\n",
				     !!(ctrl & RISCV_IOMMU_CQCSR_CQMF),
				     !!(ctrl & RISCV_IOMMU_CQCSR_CMD_TO),
				     !!(ctrl & RISCV_IOMMU_CQCSR_CMD_ILL));
		riscv_iommu_dump_regs(iommu);
	}

	/* Clear fault interrupt pending. */
	riscv_iommu_writel(iommu, RISCV_IOMMU_REG_IPSR, RISCV_IOMMU_IPSR_CIP);

	return IRQ_HANDLED;
}

/*
 * Fault/event queue, chapter 3.2
 */

static void riscv_iommu_fault_report(struct riscv_iommu_device *iommu,
				     struct riscv_iommu_fq_record *event)
{
	unsigned err, ttyp, devid;

	err = FIELD_GET(RISCV_IOMMU_FQ_HDR_CAUSE, event->hdr);
	ttyp = FIELD_GET(RISCV_IOMMU_FQ_HDR_TTYPE, event->hdr);
	devid = FIELD_GET(RISCV_IOMMU_FQ_HDR_DID, event->hdr);

	dev_warn_ratelimited(iommu->dev,
			     "Fault %d ttyp %d devid: %d" " iotval: %llx iotval2: %llx\n", err, ttyp,
			     devid, event->iotval, event->iotval2);
	riscv_iommu_dump_regs(iommu);
}

/* Fault/event queue primary interrupt handler */
static irqreturn_t riscv_iommu_fltq_irq_check(int irq, void *data)
{
	struct riscv_iommu_queue *q = (struct riscv_iommu_queue *)data;
	struct riscv_iommu_device *iommu =
	    container_of(q, struct riscv_iommu_device, fltq);
	u32 ipsr = riscv_iommu_readl(iommu, RISCV_IOMMU_REG_IPSR);
	if (ipsr & RISCV_IOMMU_IPSR_FIP)
		return IRQ_WAKE_THREAD;
	return IRQ_NONE;
}

/* Fault queue interrupt hanlder thread function */
static irqreturn_t riscv_iommu_fltq_process(int irq, void *data)
{
	struct riscv_iommu_queue *q = (struct riscv_iommu_queue *)data;
	struct riscv_iommu_device *iommu;
	struct riscv_iommu_fq_record *events;
	unsigned cnt, len, idx, ctrl;

	iommu = container_of(q, struct riscv_iommu_device, fltq);
	events = (struct riscv_iommu_fq_record *)q->base;

	/* Error reporting, clear error reports if any. */
	ctrl = riscv_iommu_readl(iommu, RISCV_IOMMU_REG_FQCSR);
	if (ctrl & (RISCV_IOMMU_FQCSR_FQMF | RISCV_IOMMU_FQCSR_FQOF)) {
		riscv_iommu_queue_ctrl(iommu, &iommu->fltq, ctrl);
		dev_warn_ratelimited(iommu->dev,
				     "Fault queue error: fault: %d full: %d\n",
				     !!(ctrl & RISCV_IOMMU_FQCSR_FQMF),
				     !!(ctrl & RISCV_IOMMU_FQCSR_FQOF));
	}

	/* Clear fault interrupt pending. */
	riscv_iommu_writel(iommu, RISCV_IOMMU_REG_IPSR, RISCV_IOMMU_IPSR_FIP);

	/* Report fault events. */
	do {
		cnt = riscv_iommu_queue_consume(iommu, q, &idx);
		if (!cnt)
			break;
		for (len = 0; len < cnt; idx++, len++)
			riscv_iommu_fault_report(iommu, &events[idx]);
		riscv_iommu_queue_release(iommu, q, cnt);
	} while (1);

	return IRQ_HANDLED;
}

/*
 * Page request queue, chapter 3.3
 */

/*
 * Register device for IOMMU tracking.
 */
static void riscv_iommu_add_device(struct riscv_iommu_device *iommu, struct device *dev)
{
	struct riscv_iommu_endpoint *ep, *rb_ep;
	struct rb_node **new_node, *parent_node = NULL;

	mutex_lock(&iommu->eps_mutex);

	ep = dev_iommu_priv_get(dev);

	new_node = &(iommu->eps.rb_node);
	while (*new_node) {
		rb_ep = rb_entry(*new_node, struct riscv_iommu_endpoint, node);
		parent_node = *new_node;
		if (rb_ep->devids[0] > ep->devids[0]) {
			new_node = &((*new_node)->rb_left);
		} else if (rb_ep->devids[0] < ep->devids[0]) {
			new_node = &((*new_node)->rb_right);
		} else {
			dev_warn(dev, "device %u already in the tree\n", ep->devids[0]);
			break;
		}
	}

	rb_link_node(&ep->node, parent_node, new_node);
	rb_insert_color(&ep->node, &iommu->eps);

	mutex_unlock(&iommu->eps_mutex);
}

static int riscv_iommu_of_xlate(struct device *dev, struct of_phandle_args *args)
{
	return iommu_fwspec_add_ids(dev, args->args, 1);
}

static bool riscv_iommu_capable(struct device *dev, enum iommu_cap cap)
{
	switch (cap) {
	case IOMMU_CAP_CACHE_COHERENCY:
		return true;

	default:
		break;
	}

	return false;
}

/* TODO: implement proper device context management, e.g. teardown flow */

/* Lookup or initialize device directory info structure. */
static void riscv_iommu_get_dc(struct riscv_iommu_device *iommu,
						 struct riscv_iommu_endpoint *ep)
{
	unsigned depth;
	u8 ddi_bits[3] = { 0 };
	u64 *ddtp = NULL, ddt;
	int i;

	if (iommu->ddt_mode == RISCV_IOMMU_DDTP_MODE_OFF ||
	    iommu->ddt_mode == RISCV_IOMMU_DDTP_MODE_BARE)
		return;

	/* Make sure the mode is valid */
	if (iommu->ddt_mode > RISCV_IOMMU_DDTP_MODE_MAX)
		return;

	/*
	 * Device id partitioning for base format:
	 * DDI[0]: bits 0 - 6   (1st level) (7 bits)
	 * DDI[1]: bits 7 - 15  (2nd level) (9 bits)
	 * DDI[2]: bits 16 - 23 (3rd level) (8 bits)
	 *
	 * For extended format:
	 * DDI[0]: bits 0 - 5   (1st level) (6 bits)
	 * DDI[1]: bits 6 - 14  (2nd level) (9 bits)
	 * DDI[2]: bits 15 - 23 (3rd level) (9 bits)
	 */
	ddi_bits[0] = 7;
	ddi_bits[1] = 7 + 9;
	ddi_bits[2] = 7 + 9 + 8;

	ep->dc = kzalloc(sizeof(struct riscv_iommu_dc *) * ep->num_ids, GFP_KERNEL);

	for (i = 0; i < ep->num_ids; i++) {
		depth = iommu->ddt_mode - RISCV_IOMMU_DDTP_MODE_1LVL;

		/* Make sure device id is within range */
		if (ep->devids[i] >= (1 << ddi_bits[depth]))
			return;

		/* Get to the level of the non-leaf node that holds the device context */
		for (ddtp = (u64 *) iommu->ddtp; depth-- > 0;) {
			const int split = ddi_bits[depth];
			/*
			* Each non-leaf node is 64bits wide and on each level
			* nodes are indexed by DDI[depth].
			*/
			ddtp += (ep->devids[i] >> split) & 0x1FF;

			printk("%s ddi_bits[%d]=%d, split=%d, (devid >> split) & 0x1FF=%d 1st ddtp=0x%px indexed ddtp=0x%px\n", __func__,
				depth, ddi_bits[depth], split, (ep->devids[i] >> split) & 0x1FF, (u64 *)iommu->ddtp, (u64 *)ddtp);
retry:
			/*
			* Check if this node has been populated and if not
			* allocate a new level and populate it.
			*/
			ddt = READ_ONCE(*ddtp);
			if (ddt & RISCV_IOMMU_DDTE_VALID) {
				ddtp = __va(ppn_to_phys(ddt));
			} else {
				u64 old, new = get_zeroed_page(GFP_KERNEL);
				if (!new)
					return;

				old = cmpxchg64_relaxed(ddtp, ddt,
							phys_to_ppn(__pa(new)) |
							RISCV_IOMMU_DDTE_VALID);

				if (old != ddt) {
					free_page(new);
					goto retry;
				}
				/* update to the next table */
				ddtp = (u64 *) new;
				printk("the next table alloced at va=0x%px pa=0x%px\n", ddtp, (u64 *)__pa(new));
			}
		}

		/*
		* Grab the node that matches DDI[depth], note that when using base
		* format the device context is 4 * 64bits, and the extended format
		* is 8 * 64bits, hence the (3 - base_format) below.
		* (devid & ((64 << base_format) - 1))==DDI[0], left shift 2 == multiply 4(4 * 64bits=256bits for DC)
		*/
		ddtp += (ep->devids[i] & ((64 << 1) - 1)) << 2;
		printk("DC located at va=0x%px pa=0x%px\n", ddtp, (u64 *)__pa(ddtp));
		ep->dc[i] = (struct riscv_iommu_dc *)ddtp;
	}

	arch_sync_dma_for_device(__pa(iommu->ddtp), SZ_4K, DMA_TO_DEVICE); // flush LV1 DDT
	dma_wmb();
	printk("%s flush cache for LV1 DDT pa=0x%lx\n", __func__, __pa(iommu->ddtp));

	return;
}

/* find out exactly which iommu hardware ip is used for specific devid */
static struct riscv_iommu_device *riscv_iommu_find_hardware(struct zh_iommu_device *iommus, u32 devid)
{
	char name[10];
	struct riscv_iommu_device *iommu;

	if (devid >= DEVID_DIE0_PERI1_DFMU && devid <= DEVID_DIE0_EMMC)
		strcpy(name, "peri1");
	else if (devid >= DEVID_DIE0_USB_DFMU && devid <= DEVID_DIE0_USB2_1)
		strcpy(name, "usb");
	else if (devid >= DEVID_DIE0_PCIE_DFMU && devid <= DEVID_DIE0_TEE_DMAC)
		strcpy(name, "pcie");
	else if (devid >= DEVID_DIE0_VI_DFMU && devid <= DEVID_DIE0_DW200)
		strcpy(name, "vi");
	else if (devid >= DEVID_DIE0_VP_DFMU && devid <= DEVID_DIE0_G2D)
		strcpy(name, "vp");
	else if (devid >= DEVID_DIE0_VO_DFMU && devid <= DEVID_DIE0_PIP_REC)
		strcpy(name, "vo");
	else if (devid >= DEVID_DIE0_NPU_DFMU && devid <= DEVID_DIE0_NPU)
		strcpy(name, "npu");
	else if (devid == DEVID_VIDEO_MEMORY_DUMMY)
		strcpy(name, "vp");
	else
		return ERR_PTR(-EINVAL);

	list_for_each_entry(iommu, &iommus->iommus, list) {
		if (strcmp(name, iommu->name) == 0)
			return iommu;
	}

	return ERR_PTR(-EINVAL);
}

static struct iommu_device *riscv_iommu_probe_device(struct device *dev)
{
	struct zh_iommu_device *iommus;
	struct riscv_iommu_endpoint *ep;
	struct iommu_fwspec *fwspec;
	struct device_node *np = dev->of_node;
	int i, ret;

	fwspec = dev_iommu_fwspec_get(dev);
	if (!fwspec || fwspec->ops != &riscv_iommu_ops ||
	    !fwspec->iommu_fwnode || !fwspec->iommu_fwnode->dev)
		return ERR_PTR(-ENODEV);

	iommus = dev_get_drvdata(fwspec->iommu_fwnode->dev);
	if (!iommus)
		return ERR_PTR(-ENODEV);

	if (dev_iommu_priv_get(dev)) // in case ep is ready
		return &iommus->iommu;

	ep = kzalloc(sizeof(*ep), GFP_KERNEL);
	if (!ep)
		return ERR_PTR(-ENOMEM);

	mutex_init(&ep->lock);
	INIT_LIST_HEAD(&ep->domain);

	/* TODO: Make this generic, for now hardcode domain id to 0 */
	ep->devids = fwspec->ids;
	ep->num_ids = fwspec->num_ids;
	ep->domid = 0;

	ep->iommu = riscv_iommu_find_hardware(iommus, ep->devids[0]);
	if (IS_ERR(ep->iommu)) {
		dev_err(dev, "riscv_iommu_find_hardware failed with devid:0x%x", ep->devids[0]);
		return ERR_PTR(-EINVAL);
	}
	ep->iommus = iommus;
	ep->dev = dev;

	if (of_property_read_u32(np, "iommu-group", &ep->group_id))
		ep->group_id = IOMMU_GROUP_PER_DEV;

	//enable iommu ddt to LV2 mode
	ret = riscv_iommu_enable(ep->iommu, ddt_mode);
	if (ret) {
		dev_err(dev, "failed to enable iommu LV2, error:%d\n", ret);
		return ERR_PTR(ret);
	}

	/* Initial DC pointer can be NULL if IOMMU is configured in OFF or BARE mode */
	riscv_iommu_get_dc(ep->iommu, ep);

	printk(KERN_CONT "adding device with devid ");
	for (i = 0; i < ep->num_ids; i++) {
		printk(KERN_CONT "%i ", ep->devids[i]);
	}
	printk(KERN_CONT "to %s iommu\n", ep->iommu->name);

	dev_iommu_priv_set(dev, ep);
	riscv_iommu_add_device(ep->iommu, dev);

	return &iommus->iommu;
}

static void riscv_iommu_probe_finalize(struct device *dev)
{
	set_dma_ops(dev, NULL);
	iommu_setup_dma_ops(dev, 0, U64_MAX);
}

static void riscv_iommu_release_device(struct device *dev)
{
	struct riscv_iommu_endpoint *ep = dev_iommu_priv_get(dev);
	struct riscv_iommu_device *iommu = ep->iommu;
	int i;

	dev_info(dev, "device with devid %i released\n", ep->devids[0]);

	mutex_lock(&ep->lock);
	list_del(&ep->domain);
	mutex_unlock(&ep->lock);

	for (i = 0; i < ep->num_ids; i++) {
		if (ep->dc[i]) {
			// this should be already done by domain detach.
			ep->dc[i]->tc = 0ULL;
			dma_wmb();
			ep->dc[i]->fsc = 0ULL;
			dma_wmb();
			riscv_iommu_iodir_inv_devid(ep->iommus, ep->devids[i]);
		}
	}

	/* Remove endpoint from IOMMU tracking structures */
	mutex_lock(&iommu->eps_mutex);
	rb_erase(&ep->node, &iommu->eps);
	mutex_unlock(&iommu->eps_mutex);

	set_dma_ops(dev, NULL);
	dev_iommu_priv_set(dev, NULL);

	kfree(ep);
}

static void riscv_iommu_group_release(void *iommu_data)
{
	struct riscv_iommu_group *group = iommu_data;
	struct zh_iommu_device *iommus = group->iommus;

	mutex_lock(&iommus->lock);
	list_del(&group->list);
	mutex_unlock(&iommus->lock);
}

static char * riscv_iommu_group_names[] = {"IOMMU_GROUP_PER_DEV", "IOMMU_GROUP_VIDEO", "IOMMU_GROUP_NPU"};

static struct iommu_group *riscv_iommu_device_group(struct device *dev)
{
	struct riscv_iommu_endpoint *ep = dev_iommu_priv_get(dev);
	struct zh_iommu_device *iommus = ep->iommus;
	struct riscv_iommu_group *group;
	struct iommu_group *grp;

	mutex_lock(&iommus->lock);

	/* Find if desired group existed in the list of riscv_iommu_device */
	list_for_each_entry(group, &iommus->groups, list) {
		if (group->group_id == ep->group_id && ep->group_id != IOMMU_GROUP_PER_DEV) {
			grp = iommu_group_ref_get(group->group);
			mutex_unlock(&iommus->lock);
			return grp;
		}
	}

	group = devm_kzalloc(iommus->dev, sizeof(*group), GFP_KERNEL);
	if (!group) {
		mutex_unlock(&iommus->lock);
		return ERR_PTR(-ENOMEM);
	}

	INIT_LIST_HEAD(&group->list);
	group->iommus = iommus;
	group->group_id = ep->group_id;
	group->group = generic_device_group(dev);

	if (IS_ERR(group->group)) {
		devm_kfree(iommus->dev, group);
		mutex_unlock(&iommus->lock);
		return group->group;
	}

	iommu_group_set_iommudata(group->group, group, riscv_iommu_group_release);
	iommu_group_set_name(group->group, riscv_iommu_group_names[group->group_id]);
	list_add_tail(&group->list, &iommus->groups);
	mutex_unlock(&iommus->lock);

	return group->group;
}

/*
 * Domain management
 */

static struct iommu_domain *riscv_iommu_domain_alloc(unsigned type)
{
	struct riscv_iommu_domain *domain;

	if (type != IOMMU_DOMAIN_DMA &&
	    type != IOMMU_DOMAIN_DMA_FQ &&
	    type != IOMMU_DOMAIN_UNMANAGED &&
	    type != IOMMU_DOMAIN_IDENTITY &&
	    type != IOMMU_DOMAIN_BLOCKED &&
	    type != IOMMU_DOMAIN_SVA)
		return NULL;

	domain = kzalloc(sizeof(*domain), GFP_KERNEL);
	if (!domain)
		return NULL;

	mutex_init(&domain->lock);
	INIT_LIST_HEAD(&domain->endpoints);

	domain->domain.ops = &riscv_iommu_domain_ops;
	domain->mode = RISCV_IOMMU_DC_FSC_MODE_BARE;
	domain->pscid = ida_alloc_range(&riscv_iommu_pscids, 1,
					RISCV_IOMMU_MAX_PSCID, GFP_KERNEL);

	printk("domain->pscid alloc %u\n", domain->pscid);

	return &domain->domain;
}

static void riscv_iommu_domain_free(struct iommu_domain *iommu_domain)
{
	struct riscv_iommu_domain *domain = iommu_domain_to_riscv(iommu_domain);

	if (!list_empty(&domain->endpoints)) {
		pr_warn("IOMMU domain is not empty!\n");
	}

	if (domain->pgtbl.cookie)
		free_io_pgtable_ops(&domain->pgtbl.ops);

	if (domain->pgd_root)
		free_pages((unsigned long)domain->pgd_root, 0);

	if ((int)domain->pscid > 0)
		ida_free(&riscv_iommu_pscids, domain->pscid);

	printk("domain free %u\n", domain->pscid);

	kfree(domain);
}

static int riscv_iommu_domain_finalize(struct riscv_iommu_domain *domain,
				       struct zh_iommu_device *iommus)
{
	struct iommu_domain_geometry *geometry;

	/* Domain assigned to another iommu */
	if (domain->iommus && domain->iommus != iommus)
		return -EINVAL;
	/* Domain already initialized */
	else if (domain->iommus)
		return 0;

	/*
	 * TODO: Before using VA_BITS and satp_mode here, verify they
	 * are supported by the iommu, through the capabilities register.
	 */

	geometry = &domain->domain.geometry;

	/*
	 * Note: RISC-V Privilege spec mandates that virtual addresses
	 * need to be sign-extended, so if (VA_BITS - 1) is set, all
	 * bits >= VA_BITS need to also be set or else we'll get a
	 * page fault. However the code that creates the mappings
	 * above us (e.g. iommu_dma_alloc_iova()) won't do that for us
	 * for now, so we'll end up with invalid virtual addresses
	 * to map. As a workaround until we get this sorted out
	 * limit the available virtual addresses to VA_BITS - 1.
	 */
	geometry->aperture_start = 0;
	geometry->aperture_end = DMA_BIT_MASK(VA_BITS - 1);
	geometry->force_aperture = true;

	domain->iommus = iommus;

	if (domain->domain.type == IOMMU_DOMAIN_IDENTITY)
		return 0;

	/* TODO: Fix this for RV32 */
	domain->mode = satp_mode >> 60;
	domain->pgd_root = (pgd_t *) __get_free_pages(GFP_KERNEL | __GFP_ZERO, 0);
	printk("%s alloc pgd_root=0x%px pa=0x%lx\n", __func__, domain->pgd_root, __pa(domain->pgd_root));

	if (!domain->pgd_root)
		return -ENOMEM;

	if (!alloc_io_pgtable_ops(RISCV_IOMMU, &domain->pgtbl.cfg, domain))
		return -ENOMEM;

	return 0;
}

static u64 riscv_iommu_domain_atp(struct riscv_iommu_domain *domain)
{
	u64 atp = FIELD_PREP(RISCV_IOMMU_DC_FSC_MODE, domain->mode);
	if (domain->mode != RISCV_IOMMU_DC_FSC_MODE_BARE)
		atp |= FIELD_PREP(RISCV_IOMMU_DC_FSC_PPN, virt_to_pfn(domain->pgd_root));
	return atp;
}

static int riscv_iommu_attach_dev(struct iommu_domain *iommu_domain, struct device *dev)
{
	struct riscv_iommu_domain *domain = iommu_domain_to_riscv(iommu_domain);
	struct riscv_iommu_endpoint *ep = dev_iommu_priv_get(dev);
	int ret, i;
	u64 val;

	/* PSCID not valid */
	if ((int)domain->pscid < 0)
		return -ENOMEM;

	mutex_lock(&domain->lock);
	mutex_lock(&ep->lock);

	if (!list_empty(&ep->domain)) {
		dev_warn(dev, "endpoint already attached to a domain. dropping\n");
		list_del_init(&ep->domain);
	}

	/* allocate root pages, initialize io-pgtable ops, etc. */
	ret = riscv_iommu_domain_finalize(domain, ep->iommus);
	if (ret < 0) {
		dev_err(dev, "can not finalize domain: %d\n", ret);
		mutex_unlock(&ep->lock);
		mutex_unlock(&domain->lock);
		return ret;
	}

	if (ep->iommu->ddt_mode == RISCV_IOMMU_DDTP_MODE_BARE &&
	    domain->domain.type == IOMMU_DOMAIN_IDENTITY) {
		dev_info(dev, "domain type %d attached w/ PSCID %u\n",
		    domain->domain.type, domain->pscid);
		return 0;
	}

	for (i = 0; i < ep->num_ids; i++) {
		if (!ep->dc[i])
			return -ENODEV;

		/* S-Stage translation table only. G-Stage not supported. */
		val = FIELD_PREP(RISCV_IOMMU_DC_TA_PSCID, domain->pscid);
		ep->dc[i]->ta = cpu_to_le64(val);
		ep->dc[i]->fsc = cpu_to_le64(riscv_iommu_domain_atp(domain));

		dma_wmb();

		/* Mark device context as valid, synchronise device context cache. */
		val = RISCV_IOMMU_DC_TC_V;
		ep->dc[i]->tc = cpu_to_le64(val);
		dma_wmb();
		arch_sync_dma_for_device(__pa(ep->dc[i]), SZ_4K, DMA_TO_DEVICE); // flush LV2 DDT
		printk("%s flush cache for LV2 DDT pa=0x%lx\n", __func__, __pa(ep->dc[i]));
		riscv_iommu_iodir_inv_devid(ep->iommus, ep->devids[i]);
	}

	list_add_tail(&ep->domain, &domain->endpoints);
	mutex_unlock(&ep->lock);
	mutex_unlock(&domain->lock);

	dev_info(dev, "domain type %d attached w/ PSCID %u for devid 0x%x\n",
	    domain->domain.type, domain->pscid, ep->devids[0]);

	riscv_iommu_dump_regs(ep->iommu);

	return 0;
}

void riscv_iommu_flush_iotlb_range(struct iommu_domain *iommu_domain,
					  unsigned long *start, unsigned long *end,
					  size_t *pgsize)
{
	struct riscv_iommu_domain *domain = iommu_domain_to_riscv(iommu_domain);
	struct riscv_iommu_command cmd;
	unsigned long iova;

	if (domain->mode == RISCV_IOMMU_DC_FSC_MODE_BARE)
		return;

	/* Domain not attached to an IOMMU! */
	BUG_ON(!domain->iommus);

	riscv_iommu_cmd_inval_vma(&cmd);
	riscv_iommu_cmd_inval_set_pscid(&cmd, domain->pscid);

	if (start && end && pgsize) {
		/* Cover only the range that is needed */
		for (iova = *start; iova <= *end; iova += *pgsize) {
			//printk("riscv_iommu_cmd_inval_set_addr iova = 0x%lx\n", iova);
			riscv_iommu_cmd_inval_set_addr(&cmd, iova);
			riscv_iommu_post(domain->iommus, &cmd);
		}
	} else {
		riscv_iommu_post(domain->iommus, &cmd);
	}
	riscv_iommu_iofence_sync(domain->iommus);
}

static void riscv_iommu_flush_iotlb_all(struct iommu_domain *iommu_domain)
{
	riscv_iommu_flush_iotlb_range(iommu_domain, NULL, NULL, NULL);
}

static void riscv_iommu_iotlb_sync(struct iommu_domain *iommu_domain,
				   struct iommu_iotlb_gather *gather)
{
	riscv_iommu_flush_iotlb_range(iommu_domain, &gather->start, &gather->end,
				      &gather->pgsize);
}

static void riscv_iommu_iotlb_sync_map(struct iommu_domain *iommu_domain,
				       unsigned long iova, size_t size)
{
	unsigned long end = iova + size - 1;
	/*
	 * Given we don't know the page size used by this range, we assume the
	 * smallest page size to ensure all possible entries are flushed from
	 * the IOATC.
	 */
	size_t pgsize = PAGE_SIZE;
	riscv_iommu_flush_iotlb_range(iommu_domain, &iova, &end, &pgsize);
}

static int riscv_iommu_map_pages(struct iommu_domain *iommu_domain,
				 unsigned long iova, phys_addr_t phys,
				 size_t pgsize, size_t pgcount, int prot,
				 gfp_t gfp, size_t *mapped)
{
	struct riscv_iommu_domain *domain = iommu_domain_to_riscv(iommu_domain);
	int ret;

	if (!domain->pgtbl.ops.map_pages)
		return -ENODEV;

	ret = domain->pgtbl.ops.map_pages(&domain->pgtbl.ops, iova, phys,
					   pgsize, pgcount, prot, gfp, mapped);

	dev_dbg(domain->iommus->dev, "LV3 target pa = 0x%llx\n", domain->pgtbl.ops.iova_to_phys(&domain->pgtbl.ops, iova));

	return ret;
}

static size_t riscv_iommu_unmap_pages(struct iommu_domain *iommu_domain,
				      unsigned long iova, size_t pgsize,
				      size_t pgcount, struct iommu_iotlb_gather *gather)
{
	struct riscv_iommu_domain *domain = iommu_domain_to_riscv(iommu_domain);

	if (!domain->pgtbl.ops.unmap_pages)
		return 0;

	return domain->pgtbl.ops.unmap_pages(&domain->pgtbl.ops, iova, pgsize,
					     pgcount, gather);
}

static phys_addr_t riscv_iommu_iova_to_phys(struct iommu_domain *iommu_domain,
					    dma_addr_t iova)
{
	struct riscv_iommu_domain *domain = iommu_domain_to_riscv(iommu_domain);

	if (!domain->pgtbl.ops.iova_to_phys)
		return 0;

	return domain->pgtbl.ops.iova_to_phys(&domain->pgtbl.ops, iova);
}

/*
 * Translation mode setup
 */

static u64 riscv_iommu_get_ddtp(struct riscv_iommu_device *iommu)
{
	u64 ddtp;
	cycles_t end_cycles = RISCV_IOMMU_TIMEOUT + get_cycles();

	/* Wait for DDTP.BUSY to be cleared and return latest value */
	do {
		ddtp = riscv_iommu_readq(iommu, RISCV_IOMMU_REG_DDTP);
		if (!(ddtp & RISCV_IOMMU_DDTP_BUSY))
			break;
		cpu_relax();
	} while (get_cycles() < end_cycles);

	return ddtp;
}

static void riscv_iommu_ddt_cleanup(struct riscv_iommu_device *iommu)
{
	/* TODO: teardown whole device directory tree. */
	if (iommu->ddtp) {
		free_page(iommu->ddtp);
		iommu->ddtp = 0;
	}
}

static int riscv_iommu_enable(struct riscv_iommu_device *iommu, unsigned requested_mode)
{
	struct device *dev = iommu->dev;
	u64 ddtp = 0;
	unsigned mode = requested_mode;
	unsigned mode_readback = 0;

	ddtp = riscv_iommu_get_ddtp(iommu);
	if (ddtp & RISCV_IOMMU_DDTP_BUSY)
		return -EBUSY;

	switch (mode) {
	case RISCV_IOMMU_DDTP_MODE_BARE:
	case RISCV_IOMMU_DDTP_MODE_OFF:
		riscv_iommu_ddt_cleanup(iommu);
		ddtp = FIELD_PREP(RISCV_IOMMU_DDTP_MODE, mode);
		break;
	case RISCV_IOMMU_DDTP_MODE_1LVL:
	case RISCV_IOMMU_DDTP_MODE_2LVL:
	case RISCV_IOMMU_DDTP_MODE_3LVL:
		if (iommu->ddtp)
			return 0;

		iommu->ddtp = get_zeroed_page(GFP_KERNEL);
		if (!iommu->ddtp)
			return -ENOMEM;

		ddtp = FIELD_PREP(RISCV_IOMMU_DDTP_MODE, mode) |
		    phys_to_ppn(__pa(iommu->ddtp));

		break;
	default:
		return -EINVAL;
	}

	riscv_iommu_writeq(iommu, RISCV_IOMMU_REG_DDTP, ddtp);
	ddtp = riscv_iommu_get_ddtp(iommu);
	if (ddtp & RISCV_IOMMU_DDTP_BUSY) {
		dev_warn(dev, "timeout when setting ddtp (ddt mode: %i)\n", mode);
		return -EBUSY;
	}

	mode_readback = FIELD_GET(RISCV_IOMMU_DDTP_MODE, ddtp);
	dev_info(dev, "%s iommu mode_readback: %i, mode: %i\n", iommu->name, mode_readback, mode);
	if (mode_readback != mode)
		goto fail;

	iommu->ddt_mode = mode;
	dev_info(dev, "%s iommu ddt_mode: %i\n", iommu->name, iommu->ddt_mode);
	return 0;

 fail:
	dev_err(dev, "failed to set DDT mode, tried: %i and got %i\n", mode,
		mode_readback);
	riscv_iommu_ddt_cleanup(iommu);
	return -EINVAL;
}

/*
 * Common I/O MMU driver probe/teardown
 */

static const struct iommu_domain_ops riscv_iommu_domain_ops = {
	.free = riscv_iommu_domain_free,
	.attach_dev = riscv_iommu_attach_dev,
	.map_pages = riscv_iommu_map_pages,
	.unmap_pages = riscv_iommu_unmap_pages,
	.iova_to_phys = riscv_iommu_iova_to_phys,
	.iotlb_sync = riscv_iommu_iotlb_sync,
	.iotlb_sync_map = riscv_iommu_iotlb_sync_map,
	.flush_iotlb_all = riscv_iommu_flush_iotlb_all,
};

static const struct iommu_ops riscv_iommu_ops = {
	.owner = THIS_MODULE,
	.pgsize_bitmap = SZ_4K | SZ_2M | SZ_1G,
	.capable = riscv_iommu_capable,
	.domain_alloc = riscv_iommu_domain_alloc,
	.probe_device = riscv_iommu_probe_device,
	.probe_finalize = riscv_iommu_probe_finalize,
	.release_device = riscv_iommu_release_device,
	.device_group = riscv_iommu_device_group,
	.of_xlate = riscv_iommu_of_xlate,
	.default_domain_ops = &riscv_iommu_domain_ops,
};

void riscv_iommu_remove(struct zh_iommu_device *iommus)
{
	struct riscv_iommu_device *iommu;

	iommu_device_unregister(&iommus->iommu);
	iommu_device_sysfs_remove(&iommus->iommu);
	list_for_each_entry(iommu, &iommus->iommus, list) {
		riscv_iommu_enable(iommu, RISCV_IOMMU_DDTP_MODE_OFF);
		riscv_iommu_queue_free(iommu, &iommu->fltq);
		riscv_iommu_queue_free(iommu, &iommu->cmdq);
	}
}

int riscv_iommu_init(struct riscv_iommu_device *iommu)
{
	int ret;

	iommu->eps = RB_ROOT;

	/*
	 * Assign queue lengths from module parameters if not already
	 * set on the device tree.
	 */
	if (!iommu->cmdq_len)
		iommu->cmdq_len = cmdq_length;
	if (!iommu->fltq_len)
		iommu->fltq_len = fltq_length;
	/* Clear any pending interrupt flag. */
	riscv_iommu_writel(iommu, RISCV_IOMMU_REG_IPSR,
			   RISCV_IOMMU_IPSR_CIP |
			   RISCV_IOMMU_IPSR_FIP |
			   RISCV_IOMMU_IPSR_PMIP | RISCV_IOMMU_IPSR_PIP);
	spin_lock_init(&iommu->cq_lock);
	mutex_init(&iommu->eps_mutex);
	ret = riscv_iommu_queue_init(iommu, RISCV_IOMMU_COMMAND_QUEUE);
	if (ret)
		goto fail;
	ret = riscv_iommu_queue_init(iommu, RISCV_IOMMU_FAULT_QUEUE);
	if (ret)
		goto fail;

	return 0;
 fail:
	riscv_iommu_enable(iommu, RISCV_IOMMU_DDTP_MODE_OFF);
	riscv_iommu_queue_free(iommu, &iommu->fltq);
	riscv_iommu_queue_free(iommu, &iommu->cmdq);
	return ret;
}

int riscv_iommu_register(struct zh_iommu_device *iommus)
{
	int ret;

	ret = iommu_device_sysfs_add(&iommus->iommu, NULL,
		NULL, "zhihe-iommu");
	if (ret) {
		dev_err(iommus->dev, "cannot register sysfs interface (%d)\n", ret);
		return ret;
	}

	ret = iommu_device_register(&iommus->iommu, &riscv_iommu_ops, iommus->dev);
	if (ret) {
		dev_err(iommus->dev, "cannot register iommu interface (%d)\n", ret);
		iommu_device_sysfs_remove(&iommus->iommu);
		return ret;
	}

	return ret;
}
