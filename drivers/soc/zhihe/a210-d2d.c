// SPDX-License-Identifier: GPL-2.0
/*
 * A210 D2D Driver
 *
 * Copyright © 2025 Zhihe Computing Inc.
 */

/* Uncomment to enable debug messages (dev_dbg will output) */
/* #define DEBUG */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_device.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/bitfield.h>
#include <linux/clk.h>

#define DRIVER_NAME "a210-d2d"

/* Ctrl Register offsets */
#define D2D_INTR_STATUS_REG	0x100
#define D2D_INTR_CLR_REG	0x104
/* ECC, no individual bit mask */
#define D2D_INTR_ECC_STATUS_REG 0x108
#define D2D_INTR_MASK_REG	0x10c

/* Err info */
#define D2D_ECC_INFO0				0x110
#define D2D_ECC_INFO1				0x114
#define D2D_ECC_INFO2				0x118
#define D2D_ECC_INFO3				0x11c
#define D2D_AXI_RCH_ERR_INFO			0x120
#define D2D_ILLEGAL_MST_AWADDR_ACCESS_LSB_INFO	0x124
#define D2D_ILLEGAL_MST_ARADDR_ACCESS_LSB_INFO	0x128
#define D2D_ILLEGAL_SLV_AWADDR_ACCESS_LSB_INFO	0x12c
#define D2D_ILLEGAL_SLV_ARADDR_ACCESS_LSB_INFO	0x130
/* [7:0] MST_AW [15:8] MST_AR [23:16] SLV_AW [31:24] SLV_AR */
#define D2D_ILLEGAL_ADDR_ACCESS_MSB_INFO 	0x134
#define D2D_RX_ARID_OVERFLOW			0x200

#define CRC_ERR_SHIFT			(0)
#define ECC_SGL_BIT_ERR_SHIFT		(1)
#define ECC_MULT_BIT_ERR_SHIFT		(2)
#define ECC_UNKNOW_BIT_ERR_SHIFT	(3)
#define ILLEGAL_MST_AWADDR_ACCESS_SHIFT	(4)
#define ILLEGAL_MST_ARADDR_ACCESS_SHIFT	(5)
#define ILLEGAL_SLV_AWADDR_ACCESS_SHIFT	(6)
#define ILLEGAL_SLV_ARADDR_ACCESS_SHIFT	(7)
#define MST_RRESP_ERR_SHIFT		(8)
#define SLV_RRESP_ERR_SHIFT		(9)
#define ACE_WACK_FULL_SHIFT		(10)
#define ACE_RACK_FULL_SHIT		(11)

/* Default interrupt mask (bit=1 means enable, bit=0 means mask/disable) */
#define D2D_INTR_MASK_DEFAULT	0xfff

/* PHY Register offsets */
#define PLLRI_FDDIV_REG				0x1310

#define FBDIV_MASK  GENMASK(21, 10)

struct a210_d2d_intr_stats {
	u64 crc_err_count;				/* CRC error count */
	u64 ecc_sgl_bit_err_count;			/* ECC single bit error count */
	u32 ecc_sgl_bit_err_last_info[4];		/* Last ECC single bit error info */
	u64 ecc_mult_bit_err_count;			/* ECC multiple bit error count */
	u32 ecc_mult_bit_err_last_info[4];		/* Last ECC multiple bit error info */
	u64 ecc_unknow_bit_err_count;			/* ECC unknown bit error count */
	u32 ecc_unknow_bit_err_last_info[4];		/* Last ECC unknown bit error info */
	u64 illegal_mst_awaddr_access_count;		/* Illegal master AW address access count */
	u32 illegal_mst_awaddr_access_last_info[2];	/* Last illegal master AW address access info */
	u64 illegal_mst_araddr_access_count;		/* Illegal master AR address access count */
	u32 illegal_mst_araddr_access_last_info[2];	/* Last illegal master AR address access info */
	u64 illegal_slv_awaddr_access_count;		/* Illegal slave AW address access count */
	u32 illegal_slv_awaddr_access_last_info[2];	/* Last illegal slave AW address access info */
	u64 illegal_slv_araddr_access_count;		/* Illegal slave AR address access count */
	u32 illegal_slv_araddr_access_last_info[2];	/* Last illegal slave AR address access info */
	u64 mst_rresp_err_count;			/* Master read response error count */
	u32 mst_rresp_err_last_info;			/* Last master read response error info */
	u64 slv_rresp_err_count;			/* Slave read response error count */
	u32 slv_rresp_err_last_info;			/* Last slave read response error info */
	u64 ace_wack_full_count;			/* ACE write acknowledge full count */
	u64 ace_rack_full_count;			/* ACE read acknowledge full count */
};

struct a210_d2d_ctrl {
	void __iomem *base;				/* virtual base address */
	int irq;					/* interrupt number */
	struct resource *mem_res;			/* memory resource */
	u64 irq_count;					/* interrupt count for statistics */
	u32 last_intr_status;				/* last interrupt status */
	u32 last_intr_ecc_status;			/* last ECC interrupt status */
	u32 ctrl_id;					/* controller ID (0 or 1) */
	u32 saved_intr_mask;				/* saved interrupt mask for PM suspend/resume */
	struct a210_d2d_intr_stats intr_stats;		/* interrupt error statistics */
	struct kobject kobj;				/* kobject for sysfs */
};

struct a210_d2d_phy {
	void __iomem *base;				/* virtual base address */
	struct resource *mem_res;			/* memory resource */
	u32 phy_id;					/* PHY ID (0 or 1) */
	u32 speed;					/* PHY speed */
	u32 refclk;					/* reference clock rate in Hz */
	struct clk *clk;				/* clock pointer */
	struct kobject kobj;				/* kobject for sysfs */
};

struct a210_d2d_dev {
	struct device *dev;				/* base dev */
	struct a210_d2d_ctrl ctrl[2];			/* array of D2D controllers */
	struct a210_d2d_phy phy[2];			/* array of D2D PHYs */
	u32 num_ctrls;					/* number of controllers found */
	u32 num_phys;					/* number of PHYs found */
	u32 nr_dies;					/* number of dies in the system */
};

struct a210_d2d_irq_handler {
	unsigned int bit_shift;
	void (*handler)(struct a210_d2d_ctrl *ctrl);
};

/**
 * a210_d2d_handle_crc_err - Handle CRC error interrupt
 * @ctrl: pointer to D2D controller structure
 */
static void a210_d2d_handle_crc_err(struct a210_d2d_ctrl *ctrl)
{
	ctrl->intr_stats.crc_err_count++;
}

/**
 * a210_d2d_handle_ecc_sgl_bit_err - Handle ECC single bit error interrupt
 * @ctrl: pointer to D2D controller structure
 */
static void a210_d2d_handle_ecc_sgl_bit_err(struct a210_d2d_ctrl *ctrl)
{
	u32 *err_info = &ctrl->intr_stats.ecc_sgl_bit_err_last_info[0];

	err_info[0] = readl(ctrl->base + D2D_ECC_INFO0);
	err_info[1] = readl(ctrl->base + D2D_ECC_INFO1);
	err_info[2] = readl(ctrl->base + D2D_ECC_INFO2);
	err_info[3] = readl(ctrl->base + D2D_ECC_INFO3);

	ctrl->intr_stats.ecc_sgl_bit_err_count++;
}

/**
 * a210_d2d_handle_ecc_mult_bit_err - Handle ECC multiple bit error interrupt
 * @ctrl: pointer to D2D controller structure
 */
static void a210_d2d_handle_ecc_mult_bit_err(struct a210_d2d_ctrl *ctrl)
{
	u32 *err_info = &ctrl->intr_stats.ecc_mult_bit_err_last_info[0];

	err_info[0] = readl(ctrl->base + D2D_ECC_INFO0);
	err_info[1] = readl(ctrl->base + D2D_ECC_INFO1);
	err_info[2] = readl(ctrl->base + D2D_ECC_INFO2);
	err_info[3] = readl(ctrl->base + D2D_ECC_INFO3);

	ctrl->intr_stats.ecc_mult_bit_err_count++;

	pr_warn_ratelimited("D2D ctrl%d: ECC multiple bit error (count=%llu, info=0x%08x 0x%08x 0x%08x 0x%08x)\n",
			    ctrl->ctrl_id, ctrl->intr_stats.ecc_mult_bit_err_count,
			    err_info[0], err_info[1], err_info[2], err_info[3]);
}

/**
 * a210_d2d_handle_ecc_unknow_bit_err - Handle ECC unknown bit error interrupt
 * @ctrl: pointer to D2D controller structure
 */
static void a210_d2d_handle_ecc_unknow_bit_err(struct a210_d2d_ctrl *ctrl)
{
	u32 *err_info = &ctrl->intr_stats.ecc_unknow_bit_err_last_info[0];

	err_info[0] = readl(ctrl->base + D2D_ECC_INFO0);
	err_info[1] = readl(ctrl->base + D2D_ECC_INFO1);
	err_info[2] = readl(ctrl->base + D2D_ECC_INFO2);
	err_info[3] = readl(ctrl->base + D2D_ECC_INFO3);

	ctrl->intr_stats.ecc_unknow_bit_err_count++;

	pr_err_ratelimited("D2D ctrl%d: ECC unknown bit error (count=%llu, info=0x%08x 0x%08x 0x%08x 0x%08x)\n",
			   ctrl->ctrl_id, ctrl->intr_stats.ecc_unknow_bit_err_count,
			   err_info[0], err_info[1], err_info[2], err_info[3]);
}

/**
 * a210_d2d_handle_illegal_mst_awaddr - Handle illegal master AW address access interrupt
 * @ctrl: pointer to D2D controller structure
 */
static void a210_d2d_handle_illegal_mst_awaddr(struct a210_d2d_ctrl *ctrl)
{
	u32 err_info[2];

	err_info[0] = readl(ctrl->base + D2D_ILLEGAL_ADDR_ACCESS_MSB_INFO) & 0xff;
	err_info[1] = readl(ctrl->base + D2D_ILLEGAL_MST_AWADDR_ACCESS_LSB_INFO);

	ctrl->intr_stats.illegal_mst_awaddr_access_count++;
	ctrl->intr_stats.illegal_mst_awaddr_access_last_info[0] = err_info[0];
	ctrl->intr_stats.illegal_mst_awaddr_access_last_info[1] = err_info[1];

	pr_err_ratelimited("D2D ctrl%d: Illegal master AW address access (count=%llu, info=0x%02x 0x%08x)\n",
			   ctrl->ctrl_id, ctrl->intr_stats.illegal_mst_awaddr_access_count, err_info[0], err_info[1]);
}

/**
 * a210_d2d_handle_illegal_mst_araddr - Handle illegal master AR address access interrupt
 * @ctrl: pointer to D2D controller structure
 */
static void a210_d2d_handle_illegal_mst_araddr(struct a210_d2d_ctrl *ctrl)
{
	u32 err_info[2];

	err_info[0] = (readl(ctrl->base + D2D_ILLEGAL_ADDR_ACCESS_MSB_INFO) >> 8) & 0xff;
	err_info[1] = readl(ctrl->base + D2D_ILLEGAL_MST_ARADDR_ACCESS_LSB_INFO);

	ctrl->intr_stats.illegal_mst_araddr_access_count++;
	ctrl->intr_stats.illegal_mst_araddr_access_last_info[0] = err_info[0];
	ctrl->intr_stats.illegal_mst_araddr_access_last_info[1] = err_info[1];

	pr_err_ratelimited("D2D ctrl%d: Illegal master AR address access (count=%llu, info=0x%02x 0x%08x)\n",
			   ctrl->ctrl_id, ctrl->intr_stats.illegal_mst_araddr_access_count, err_info[0], err_info[1]);
}

/**
 * a210_d2d_handle_illegal_slv_awaddr - Handle illegal slave AW address access interrupt
 * @ctrl: pointer to D2D controller structure
 */
static void a210_d2d_handle_illegal_slv_awaddr(struct a210_d2d_ctrl *ctrl)
{
	u32 err_info[2];

	err_info[0] = (readl(ctrl->base + D2D_ILLEGAL_ADDR_ACCESS_MSB_INFO) >> 16) & 0xff;
	err_info[1] = readl(ctrl->base + D2D_ILLEGAL_SLV_AWADDR_ACCESS_LSB_INFO);

	ctrl->intr_stats.illegal_slv_awaddr_access_count++;
	ctrl->intr_stats.illegal_slv_awaddr_access_last_info[0] = err_info[0];
	ctrl->intr_stats.illegal_slv_awaddr_access_last_info[1] = err_info[1];

	pr_err_ratelimited("D2D ctrl%d: Illegal slave AW address access (count=%llu, info=0x%02x 0x%08x)\n",
			   ctrl->ctrl_id, ctrl->intr_stats.illegal_slv_awaddr_access_count, err_info[0], err_info[1]);
}

/**
 * a210_d2d_handle_illegal_slv_araddr - Handle illegal slave AR address access interrupt
 * @ctrl: pointer to D2D controller structure
 */
static void a210_d2d_handle_illegal_slv_araddr(struct a210_d2d_ctrl *ctrl)
{
	u32 err_info[2];

	err_info[0] = (readl(ctrl->base + D2D_ILLEGAL_ADDR_ACCESS_MSB_INFO) >> 24) & 0xff;
	err_info[1] = readl(ctrl->base + D2D_ILLEGAL_SLV_ARADDR_ACCESS_LSB_INFO);

	ctrl->intr_stats.illegal_slv_araddr_access_count++;
	ctrl->intr_stats.illegal_slv_araddr_access_last_info[0] = err_info[0];
	ctrl->intr_stats.illegal_slv_araddr_access_last_info[1] = err_info[1];

	pr_err_ratelimited("D2D ctrl%d: Illegal slave AR address access (count=%llu, info=0x%02x 0x%08x)\n",
			   ctrl->ctrl_id, ctrl->intr_stats.illegal_slv_araddr_access_count, err_info[0], err_info[1]);
}

/**
 * a210_d2d_handle_mst_rresp_err - Handle master read response error interrupt
 * @ctrl: pointer to D2D controller structure
 */
static void a210_d2d_handle_mst_rresp_err(struct a210_d2d_ctrl *ctrl)
{
	u32 err_info;

	err_info = readl(ctrl->base + D2D_AXI_RCH_ERR_INFO) & 0xffff;

	ctrl->intr_stats.mst_rresp_err_count++;
	ctrl->intr_stats.mst_rresp_err_last_info = err_info;

	pr_err_ratelimited("D2D ctrl%d: Master read response error (count=%llu, info=0x%08x)\n",
			   ctrl->ctrl_id, ctrl->intr_stats.mst_rresp_err_count, err_info);
}

/**
 * a210_d2d_handle_slv_rresp_err - Handle slave read response error interrupt
 * @ctrl: pointer to D2D controller structure
 */
static void a210_d2d_handle_slv_rresp_err(struct a210_d2d_ctrl *ctrl)
{
	u32 err_info;

	err_info = (readl(ctrl->base + D2D_AXI_RCH_ERR_INFO) >> 16) & 0xffff;

	ctrl->intr_stats.slv_rresp_err_count++;
	ctrl->intr_stats.slv_rresp_err_last_info = err_info;

	pr_err_ratelimited("D2D ctrl%d: Slave read response error (count=%llu, info=0x%08x)\n",
			   ctrl->ctrl_id, ctrl->intr_stats.slv_rresp_err_count, err_info);
}

/**
 * a210_d2d_handle_ace_wack_full - Handle ACE write acknowledge full interrupt
 * @ctrl: pointer to D2D controller structure
 */
static void a210_d2d_handle_ace_wack_full(struct a210_d2d_ctrl *ctrl)
{
	ctrl->intr_stats.ace_wack_full_count++;

	pr_warn_ratelimited("D2D ctrl%d: ACE write acknowledge full (count=%llu)\n",
			    ctrl->ctrl_id, ctrl->intr_stats.ace_wack_full_count);
}

/**
 * a210_d2d_handle_ace_rack_full - Handle ACE read acknowledge full interrupt
 * @ctrl: pointer to D2D controller structure
 */
static void a210_d2d_handle_ace_rack_full(struct a210_d2d_ctrl *ctrl)
{
	ctrl->intr_stats.ace_rack_full_count++;

	pr_warn_ratelimited("D2D ctrl%d: ACE read acknowledge full (count=%llu)\n",
			    ctrl->ctrl_id, ctrl->intr_stats.ace_rack_full_count);
}

static const struct a210_d2d_irq_handler irq_handlers[] = {
	{ CRC_ERR_SHIFT,			a210_d2d_handle_crc_err },
	{ ECC_SGL_BIT_ERR_SHIFT,		a210_d2d_handle_ecc_sgl_bit_err },
	{ ECC_MULT_BIT_ERR_SHIFT,		a210_d2d_handle_ecc_mult_bit_err },
	{ ECC_UNKNOW_BIT_ERR_SHIFT,		a210_d2d_handle_ecc_unknow_bit_err },
	{ ILLEGAL_MST_AWADDR_ACCESS_SHIFT,	a210_d2d_handle_illegal_mst_awaddr },
	{ ILLEGAL_MST_ARADDR_ACCESS_SHIFT,	a210_d2d_handle_illegal_mst_araddr },
	{ ILLEGAL_SLV_AWADDR_ACCESS_SHIFT,	a210_d2d_handle_illegal_slv_awaddr },
	{ ILLEGAL_SLV_ARADDR_ACCESS_SHIFT,	a210_d2d_handle_illegal_slv_araddr },
	{ MST_RRESP_ERR_SHIFT,			a210_d2d_handle_mst_rresp_err },
	{ SLV_RRESP_ERR_SHIFT,			a210_d2d_handle_slv_rresp_err },
	{ ACE_WACK_FULL_SHIFT,			a210_d2d_handle_ace_wack_full },
	{ ACE_RACK_FULL_SHIT,			a210_d2d_handle_ace_rack_full },
};

static void a210_d2d_handle_interrupt(struct a210_d2d_ctrl *ctrl, u32 intr_status)
{
	for (int i = 0; i < ARRAY_SIZE(irq_handlers); i++) {
		if (intr_status & BIT(irq_handlers[i].bit_shift)) {
			irq_handlers[i].handler(ctrl);
		}
	}
}

/**
 * a210_d2d_irq_handler - D2D interrupt handler
 * @irq: interrupt number
 * @data: pointer to private data (controller structure)
 *
 * Return: IRQ_HANDLED if interrupt was handled, IRQ_NONE otherwise
 */
static irqreturn_t a210_d2d_irq_handler(int irq, void *data)
{
	struct a210_d2d_ctrl *ctrl = data;
	u32 intr_status, intr_ecc_status;

	intr_status = readl(ctrl->base + D2D_INTR_STATUS_REG);
	if (!intr_status)
		return IRQ_NONE;

	intr_ecc_status = readl(ctrl->base + D2D_INTR_ECC_STATUS_REG);

	ctrl->irq_count++;
	ctrl->last_intr_status = intr_status;
	ctrl->last_intr_ecc_status = intr_ecc_status;

	a210_d2d_handle_interrupt(ctrl, intr_status);

	/* Clear the interrupt by writing back the status bits */
	writel(intr_status, ctrl->base + D2D_INTR_CLR_REG);

	/* Read back to ensure write is complete */
	readl(ctrl->base + D2D_INTR_CLR_REG);

	return IRQ_HANDLED;
}

/**
 * Sysfs attribute functions for D2D controller
 */

/* Helper macro to get ctrl from kobject */
#define to_ctrl(obj) container_of(obj, struct a210_d2d_ctrl, kobj)

static ssize_t intr_status_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	struct a210_d2d_ctrl *ctrl = to_ctrl(kobj);
	u32 status;

	status = readl(ctrl->base + D2D_INTR_STATUS_REG);
	return sprintf(buf, "0x%08x\n", status);
}
static struct kobj_attribute intr_status_attr = __ATTR_RO(intr_status);

static ssize_t irq_count_show(struct kobject *kobj,
			      struct kobj_attribute *attr, char *buf)
{
	struct a210_d2d_ctrl *ctrl = to_ctrl(kobj);

	return sprintf(buf, "%llu\n", ctrl->irq_count);
}
static struct kobj_attribute irq_count_attr = __ATTR_RO(irq_count);

static ssize_t last_intr_status_show(struct kobject *kobj,
				     struct kobj_attribute *attr, char *buf)
{
	struct a210_d2d_ctrl *ctrl = to_ctrl(kobj);

	return sprintf(buf, "0x%08x\n", ctrl->last_intr_status);
}
static struct kobj_attribute last_intr_status_attr = __ATTR_RO(last_intr_status);

static ssize_t last_intr_ecc_status_show(struct kobject *kobj,
					 struct kobj_attribute *attr, char *buf)
{
	struct a210_d2d_ctrl *ctrl = to_ctrl(kobj);

	return sprintf(buf, "0x%08x\n", ctrl->last_intr_ecc_status);
}
static struct kobj_attribute last_intr_ecc_status_attr = __ATTR_RO(last_intr_ecc_status);

static ssize_t base_addr_show(struct kobject *kobj,
			      struct kobj_attribute *attr, char *buf)
{
	struct a210_d2d_ctrl *ctrl = to_ctrl(kobj);

	return sprintf(buf, "0x%llx\n", (u64)ctrl->mem_res->start);
}
static struct kobj_attribute base_addr_attr = __ATTR_RO(base_addr);

static ssize_t irq_num_show(struct kobject *kobj,
			    struct kobj_attribute *attr, char *buf)
{
	struct a210_d2d_ctrl *ctrl = to_ctrl(kobj);

	return sprintf(buf, "%d\n", ctrl->irq);
}
static struct kobj_attribute irq_num_attr = __ATTR_RO(irq_num);

static ssize_t intr_err_stats_show(struct kobject *kobj,
				   struct kobj_attribute *attr, char *buf)
{
	struct a210_d2d_ctrl *ctrl = to_ctrl(kobj);
	struct a210_d2d_intr_stats *stats = &ctrl->intr_stats;
	ssize_t len = 0;

	len += sprintf(buf + len, "CRC Error: %llu\n",
		       stats->crc_err_count);
	len += sprintf(buf + len, "ECC Single Bit Error: %llu (last_info=0x%08x 0x%08x 0x%08x 0x%08x)\n",
		       stats->ecc_sgl_bit_err_count, stats->ecc_sgl_bit_err_last_info[0], stats->ecc_sgl_bit_err_last_info[1],
		       stats->ecc_sgl_bit_err_last_info[2], stats->ecc_sgl_bit_err_last_info[3]);
	len += sprintf(buf + len, "ECC Multiple Bit Error: %llu (last_info=0x%08x 0x%08x 0x%08x 0x%08x)\n",
		       stats->ecc_mult_bit_err_count, stats->ecc_mult_bit_err_last_info[0], stats->ecc_mult_bit_err_last_info[1],
		       stats->ecc_mult_bit_err_last_info[2], stats->ecc_mult_bit_err_last_info[3]);
	len += sprintf(buf + len, "ECC Unknown Bit Error: %llu (last_info=0x%08x 0x%08x 0x%08x 0x%08x)\n",
		       stats->ecc_unknow_bit_err_count, stats->ecc_unknow_bit_err_last_info[0], stats->ecc_unknow_bit_err_last_info[1],
		       stats->ecc_unknow_bit_err_last_info[2], stats->ecc_unknow_bit_err_last_info[3]);
	len += sprintf(buf + len, "Illegal Master AW Addr Access: %llu (last_info=0x%02x 0x%08x)\n",
		       stats->illegal_mst_awaddr_access_count,
		       stats->illegal_mst_awaddr_access_last_info[0], stats->illegal_mst_awaddr_access_last_info[1]);
	len += sprintf(buf + len, "Illegal Master AR Addr Access: %llu (last_info=0x%02x 0x%08x)\n",
		       stats->illegal_mst_araddr_access_count,
		       stats->illegal_mst_araddr_access_last_info[0], stats->illegal_mst_araddr_access_last_info[1]);
	len += sprintf(buf + len, "Illegal Slave AW Addr Access: %llu (last_info=0x%02x 0x%08x)\n",
		       stats->illegal_slv_awaddr_access_count,
		       stats->illegal_slv_awaddr_access_last_info[0], stats->illegal_slv_awaddr_access_last_info[1]);
	len += sprintf(buf + len, "Illegal Slave AR Addr Access: %llu (last_info=0x%02x 0x%08x)\n",
		       stats->illegal_slv_araddr_access_count,
		       stats->illegal_slv_araddr_access_last_info[0], stats->illegal_slv_araddr_access_last_info[1]);
	len += sprintf(buf + len, "Master Read Response Error: %llu (last_info=0x%08x)\n",
		       stats->mst_rresp_err_count, stats->mst_rresp_err_last_info);
	len += sprintf(buf + len, "Slave Read Response Error: %llu (last_info=0x%08x)\n",
		       stats->slv_rresp_err_count, stats->slv_rresp_err_last_info);
	len += sprintf(buf + len, "ACE Write ACK Full: %llu\n",
		       stats->ace_wack_full_count);
	len += sprintf(buf + len, "ACE Read ACK Full: %llu\n",
		       stats->ace_rack_full_count);

	return len;
}
static struct kobj_attribute intr_err_stats_attr = __ATTR_RO(intr_err_stats);

/* Attribute array for ctrl kobject */
static struct attribute *ctrl_attrs[] = {
	&intr_status_attr.attr,
	&irq_count_attr.attr,
	&last_intr_status_attr.attr,
	&last_intr_ecc_status_attr.attr,
	&base_addr_attr.attr,
	&irq_num_attr.attr,
	&intr_err_stats_attr.attr,
	NULL,
};

static const struct attribute_group ctrl_attr_group = {
	.attrs = ctrl_attrs,
};

/* Ktype for ctrl kobject */
static void ctrl_kobj_release(struct kobject *kobj)
{
	/* Nothing to free, ctrl is part of d2d_dev */
}

static struct kobj_type ctrl_ktype = {
	.release = ctrl_kobj_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = (const struct attribute_group *[]){
		&ctrl_attr_group,
		NULL,
	},
};

/**
 * Sysfs attribute functions for D2D PHY
 */

/* Helper macro to get phy from kobject */
#define to_phy(obj) container_of(obj, struct a210_d2d_phy, kobj)

static ssize_t speed_show(struct kobject *kobj,
			  struct kobj_attribute *attr, char *buf)
{
	struct a210_d2d_phy *phy = to_phy(kobj);

	/* valid if only phy_id == 0 */
	if (phy->phy_id)
		return -ENODEV;

	if (!phy->speed) {
		u32 fbdiv = FIELD_GET(FBDIV_MASK, readl(phy->base + PLLRI_FDDIV_REG));
		phy->speed = (fbdiv << 1) * 100;
	}

	return sprintf(buf, "%u Mbps\n", phy->speed);
}
static struct kobj_attribute speed_attr = __ATTR_RO(speed);

static ssize_t phy_base_addr_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	struct a210_d2d_phy *phy = to_phy(kobj);

	return sprintf(buf, "0x%llx\n", (u64)phy->mem_res->start);
}
static struct kobj_attribute phy_base_addr_attr = __ATTR_RO(phy_base_addr);

static ssize_t refclk_show(struct kobject *kobj,
			   struct kobj_attribute *attr, char *buf)
{
	struct a210_d2d_phy *phy = to_phy(kobj);

	if (!phy->refclk)
		return sprintf(buf, "N/A\n");

	return sprintf(buf, "%u Hz\n", phy->refclk);
}
static struct kobj_attribute refclk_attr = __ATTR_RO(refclk);

/* Attribute array for phy kobject */
static struct attribute *phy_attrs[] = {
	&speed_attr.attr,
	&phy_base_addr_attr.attr,
	&refclk_attr.attr,
	NULL,
};

static const struct attribute_group phy_attr_group = {
	.attrs = phy_attrs,
};

/* Ktype for phy kobject */
static void phy_kobj_release(struct kobject *kobj)
{
	/* Nothing to free, phy is part of d2d_dev */
}

static struct kobj_type phy_ktype = {
	.release = phy_kobj_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = (const struct attribute_group *[]){
		&phy_attr_group,
		NULL,
	},
};

/* Sysfs attribute for nr_dies */
static ssize_t nr_dies_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct a210_d2d_dev *d2d = dev_get_drvdata(dev);

	if (d2d->nr_dies == 0)
		return -ENODATA;

	return sprintf(buf, "%u\n", d2d->nr_dies);
}
static DEVICE_ATTR_RO(nr_dies);

/* Attribute group for main device */
static struct attribute *a210_d2d_attrs[] = {
	&dev_attr_nr_dies.attr,
	NULL,
};

static const struct attribute_group a210_d2d_attr_group = {
	.attrs = a210_d2d_attrs,
};

static const struct attribute_group *a210_d2d_attr_groups[] = {
	&a210_d2d_attr_group,
	NULL,
};

/**
 * a210_d2d_parse_child_nodes - Parse child nodes from device tree
 * @pdev: platform device
 * @d2d: pointer to D2D device structure
 *
 * Return: 0 on success, negative error code on failure
 */
static int a210_d2d_parse_child_nodes(struct platform_device *pdev,
				      struct a210_d2d_dev *d2d)
{
	struct device *dev = &pdev->dev;
	struct device_node *child;
	int ret;

	/* Iterate through all child nodes */
	for_each_child_of_node(dev->of_node, child) {
		struct resource res;

		/* Check if this is a d2d_ctrl node */
		if (of_device_is_compatible(child, "zhihe,d2d-ctrl")) {
			struct a210_d2d_ctrl *ctrl;
			int irq;

			if (d2d->num_ctrls >= 2) {
				dev_warn(dev, "Too many d2d_ctrl nodes, skipping %pOF\n",
					 child);
				continue;
			}

			ret = of_address_to_resource(child, 0, &res);
			if (ret) {
				dev_err(dev, "Failed to get resource from ctrl node %pOF: %d\n",
					child, ret);
				of_node_put(child);
				return ret;
			}

			ctrl = &d2d->ctrl[d2d->num_ctrls];
			ctrl->ctrl_id = d2d->num_ctrls;

			ctrl->base = devm_ioremap_resource(dev, &res);
			if (IS_ERR(ctrl->base)) {
				dev_err(dev, "Failed to map ctrl%d registers\n",
					ctrl->ctrl_id);
				of_node_put(child);
				return PTR_ERR(ctrl->base);
			}

			ctrl->mem_res = devm_kzalloc(dev, sizeof(*ctrl->mem_res), GFP_KERNEL);
			if (!ctrl->mem_res) {
				of_node_put(child);
				return -ENOMEM;
			}
			memcpy(ctrl->mem_res, &res, sizeof(res));

			irq = of_irq_get(child, 0);
			if (irq < 0) {
				dev_err(dev, "Failed to get IRQ for ctrl%d: %d\n",
					ctrl->ctrl_id, irq);
				of_node_put(child);
				return irq;
			}
			ctrl->irq = irq;

			ret = devm_request_irq(dev, ctrl->irq, a210_d2d_irq_handler,
					       IRQF_SHARED, dev_name(dev), ctrl);
			if (ret) {
				dev_err(dev, "Failed to request IRQ %d for ctrl%d: %d\n",
					ctrl->irq, ctrl->ctrl_id, ret);
				of_node_put(child);
				return ret;
			}

			dev_info(dev, "D2D ctrl%d: base=0x%llx, irq=%d\n",
				 ctrl->ctrl_id, (u64)res.start, ctrl->irq);

			/* Clear any pending interrupts */
			writel(0xFFFFFFFF, ctrl->base + D2D_INTR_CLR_REG);

			/* Initialize kobject for this controller */
			memset(&ctrl->kobj, 0, sizeof(ctrl->kobj));
			ret = kobject_init_and_add(&ctrl->kobj, &ctrl_ktype,
						    &dev->kobj, "ctrl%d", ctrl->ctrl_id);
			if (ret) {
				dev_err(dev, "Failed to create kobject for ctrl%d: %d\n",
					ctrl->ctrl_id, ret);
				of_node_put(child);
				return ret;
			}

			/* Notify userspace about the new kobject */
			kobject_uevent(&ctrl->kobj, KOBJ_ADD);

			d2d->num_ctrls++;
		}
		/* Check if this is a d2d_phy node */
		else if (of_device_is_compatible(child, "zhihe,d2d-phy")) {
			struct a210_d2d_phy *phy;
			struct clk *clk;
			unsigned long clk_rate;

			if (d2d->num_phys >= 2) {
				dev_warn(dev, "Too many d2d_phy nodes, skipping %pOF\n",
					 child);
				continue;
			}

			ret = of_address_to_resource(child, 0, &res);
			if (ret) {
				dev_err(dev, "Failed to get resource from phy node %pOF: %d\n",
					child, ret);
				of_node_put(child);
				return ret;
			}

			phy = &d2d->phy[d2d->num_phys];
			phy->phy_id = d2d->num_phys;

			phy->base = devm_ioremap_resource(dev, &res);
			if (IS_ERR(phy->base)) {
				dev_err(dev, "Failed to map phy%d registers\n",
					phy->phy_id);
				of_node_put(child);
				return PTR_ERR(phy->base);
			}

			phy->mem_res = devm_kzalloc(dev, sizeof(*phy->mem_res), GFP_KERNEL);
			if (!phy->mem_res) {
				of_node_put(child);
				return -ENOMEM;
			}
			memcpy(phy->mem_res, &res, sizeof(res));

			/* Initialize speed to 0 as default */
			phy->speed = 0;

			/* Get clock from device tree if available */
			clk = of_clk_get(child, 0);
			if (IS_ERR(clk)) {
				ret = PTR_ERR(clk);
				/* If clock provider is not ready, defer probe */
				if (ret == -EPROBE_DEFER) {
					dev_info(dev, "Clock not ready for phy%d, deferring probe\n",
						 phy->phy_id);
					of_node_put(child);
					return ret;
				}
				/* Clock is optional for other errors, just log a message */
				dev_dbg(dev, "No clock specified for phy%d (err=%d)\n",
					 phy->phy_id, ret);
				phy->clk = NULL;
				phy->refclk = 0;
			} else {
				phy->clk = clk;
				ret = clk_prepare_enable(clk);
				if (ret) {
					dev_err(dev, "Failed to enable clock for phy%d: %d\n",
						phy->phy_id, ret);
					clk_put(clk);
					of_node_put(child);
					return ret;
				}

				clk_rate = clk_get_rate(clk);
				phy->refclk = (u32)clk_rate;
				dev_info(dev, "D2D phy%d: refclk=%u Hz\n",
					 phy->phy_id, phy->refclk);
			}

			dev_info(dev, "D2D phy%d: base=0x%llx\n",
				 phy->phy_id, (u64)res.start);

			/* Initialize kobject for this PHY */
			memset(&phy->kobj, 0, sizeof(phy->kobj));
			ret = kobject_init_and_add(&phy->kobj, &phy_ktype,
						    &dev->kobj, "phy%d", phy->phy_id);
			if (ret) {
				dev_err(dev, "Failed to create kobject for phy%d: %d\n",
					phy->phy_id, ret);
				of_node_put(child);
				return ret;
			}

			/* Notify userspace about the new kobject */
			kobject_uevent(&phy->kobj, KOBJ_ADD);

			d2d->num_phys++;
		}
	}

	dev_info(dev, "Parsed %d controllers and %d PHYs\n",
		 d2d->num_ctrls, d2d->num_phys);

	return 0;
}

/**
 * a210_d2d_probe - Probe function for D2D driver
 * @pdev: platform device
 *
 * Return: 0 on success, negative error code on failure
 */
static int a210_d2d_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct a210_d2d_dev *d2d;
	int ret;

	dev_info(dev, "Probing A210 D2D device\n");

	/* Allocate driver data structure */
	d2d = devm_kzalloc(dev, sizeof(*d2d), GFP_KERNEL);
	if (!d2d)
		return -ENOMEM;

	d2d->dev = dev;
	platform_set_drvdata(pdev, d2d);

	/* Read nr_dies property from device tree if present */
	if (of_property_read_u32(dev->of_node, "nr_dies", &d2d->nr_dies) == 0) {
		dev_info(dev, "Number of dies in system: %d\n", d2d->nr_dies);
	}

	/* Parse child nodes (controllers and PHYs) */
	ret = a210_d2d_parse_child_nodes(pdev, d2d);
	if (ret) {
		dev_err(dev, "Failed to parse child nodes: %d\n", ret);
		return ret;
	}

	/* Verify we have at least some controllers */
	if (d2d->num_ctrls == 0) {
		dev_err(dev, "No D2D controllers found\n");
		return -ENODEV;
	}

	dev_info(dev, "A210 D2D device probe completed successfully\n");
	dev_info(dev, "Found %d controller(s) and %d PHY(s)\n",
		 d2d->num_ctrls, d2d->num_phys);

	return 0;
}

/**
 * a210_d2d_remove - Remove function for D2D driver
 * @pdev: platform device
 *
 * Return: 0 on success
 */
static int a210_d2d_remove(struct platform_device *pdev)
{
	struct a210_d2d_dev *d2d = platform_get_drvdata(pdev);
	int i;

	dev_info(d2d->dev, "Removing A210 D2D device\n");

	/* Clear all interrupts and cleanup controller kobjects */
	for (i = 0; i < d2d->num_ctrls; i++) {
		writel(0xFFFFFFFF, d2d->ctrl[i].base + D2D_INTR_CLR_REG);
		kobject_put(&d2d->ctrl[i].kobj);
	}

	/* Cleanup PHY kobjects and clocks */
	for (i = 0; i < d2d->num_phys; i++) {
		if (d2d->phy[i].clk) {
			clk_disable_unprepare(d2d->phy[i].clk);
			clk_put(d2d->phy[i].clk);
		}
		kobject_put(&d2d->phy[i].kobj);
	}

	dev_info(d2d->dev, "A210 D2D device removed\n");

	return 0;
}

/* Device tree match table */
static const struct of_device_id a210_d2d_of_match[] = {
	{ .compatible = "zhihe,d2d" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, a210_d2d_of_match);

/**
 * a210_d2d_suspend - Suspend function for PM
 * @dev: device structure
 *
 * Return: 0 on success, negative error code on failure
 */
static int a210_d2d_suspend(struct device *dev)
{
	struct a210_d2d_dev *d2d = dev_get_drvdata(dev);
	int i;

	dev_info(dev, "Suspending A210 D2D device\n");

	/* Save controller states and disable interrupts */
	for (i = 0; i < d2d->num_ctrls; i++) {
		struct a210_d2d_ctrl *ctrl = &d2d->ctrl[i];

		/* Save interrupt mask register */
		ctrl->saved_intr_mask = readl(ctrl->base + D2D_INTR_MASK_REG);

		/* Disable all interrupts before suspend (bit=0 means disabled) */
		writel(0x0, ctrl->base + D2D_INTR_MASK_REG);

		/* Clear any pending interrupts */
		writel(0xFFFFFFFF, ctrl->base + D2D_INTR_CLR_REG);

		dev_dbg(dev, "D2D ctrl%d suspended, saved mask=0x%08x\n",
			ctrl->ctrl_id, ctrl->saved_intr_mask);
	}

	dev_info(dev, "A210 D2D device suspended successfully\n");

	return 0;
}

/**
 * a210_d2d_resume - Resume function for PM
 * @dev: device structure
 *
 * Return: 0 on success, negative error code on failure
 */
static int a210_d2d_resume(struct device *dev)
{
	struct a210_d2d_dev *d2d = dev_get_drvdata(dev);
	int i;

	dev_info(dev, "Resuming A210 D2D device\n");

	/* Restore controller states */
	for (i = 0; i < d2d->num_ctrls; i++) {
		struct a210_d2d_ctrl *ctrl = &d2d->ctrl[i];

		/* Clear any pending interrupts that occurred during suspend */
		writel(0xFFFFFFFF, ctrl->base + D2D_INTR_CLR_REG);

		/* Restore interrupt mask register */
		writel(ctrl->saved_intr_mask, ctrl->base + D2D_INTR_MASK_REG);

		dev_dbg(dev, "D2D ctrl%d resumed, restored mask=0x%08x\n",
			ctrl->ctrl_id, ctrl->saved_intr_mask);
	}

	dev_info(dev, "A210 D2D device resumed successfully\n");

	return 0;
}

/* PM operations structure */
static const struct dev_pm_ops a210_d2d_pm_ops = {
	.suspend = a210_d2d_suspend,
	.resume = a210_d2d_resume,
};

/* Platform driver structure */
static struct platform_driver a210_d2d_driver = {
	.probe = a210_d2d_probe,
	.remove = a210_d2d_remove,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = a210_d2d_of_match,
		.dev_groups = a210_d2d_attr_groups,  /* Auto-managed sysfs attributes */
		.pm = &a210_d2d_pm_ops,
	},
};

module_platform_driver(a210_d2d_driver);

MODULE_AUTHOR("chengc");
MODULE_DESCRIPTION("A210 D2D Device Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRIVER_NAME);
