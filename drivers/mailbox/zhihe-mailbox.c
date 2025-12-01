// SPDX-License-Identifier: GPL-2.0
/*
 * a210 MailBox support
 *
 * Copyright (C) 2024 ZHIHE Group Holding Limited.
 *
 * Author: xionglue.huang <huangxionglue@zhcomputing.com>
 * Author: hongkun.xu <xuhongkun@zhcomputing.com>
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/slab.h>

#define ZHIHE_MBOX_V1			0x0
#define ZHIHE_MBOX_V2			0x1

/* Status Register */
#define ZHIHE_MBOX_STA			0x0
#define ZHIHE_MBOX_CLR			0x4
#define ZHIHE_MBOX_MASK		0xc

/* Transmit/receive data register:
 * INFO0 ~ INFO6
 */
#define ZHIHE_MBOX_INFO_NUM		8
#define ZHIHE_MBOX_DATA_INFO_NUM	7
#define ZHIHE_MBOX_INFO0		0x14
/* Transmit ack register: INFO7 */
#define ZHIHE_MBOX_INFO7		0x30

/* Generate remote icu IRQ Register */
#define ZHIHE_MBOX_GEN			0x10
#define ZHIHE_MBOX_GEN_RX_DATA		BIT(6)
#define ZHIHE_MBOX_GEN_TX_ACK		BIT(7)

#define ZHIHE_MBOX_CHAN_RES_SIZE	0x1000
#define ZHIHE_MBOX_CHANS		4
#define ZHIHE_MBOX_CHAN_NAME_SIZE	20

#define ZHIHE_MBOX_ACK_MAGIC		0xdeadbeaf

#ifdef CONFIG_PM_SLEEP
/* store MBOX context across system-wide suspend/resume transitions */
struct zhihe_mbox_context {
	u32 intr_mask[ZHIHE_MBOX_CHANS - 1];
};

#endif
enum zhihe_mbox_chan_type {
	ZHIHE_MBOX_TYPE_TXRX,		/* Tx & Rx chan */
	ZHIHE_MBOX_TYPE_DB,		/* Tx & Rx doorbell */
};

enum zhihe_mbox_icu_cpu_id {
	ZHIHE_MBOX_ICU_CPU0  = 0,		/* A200:910T,A210:die0-908 */
	ZHIHE_MBOX_ICU_CPU1  = 1,		/* A200:902                */
	ZHIHE_MBOX_ICU_CPU2  = 2,		/* A200:906                */
	ZHIHE_MBOX_ICU_CPU3  = 3,		/* A200:910R               */
};

enum zhihe_mbox_local_id {
	ZHIHE_MBOX_INTERRUPT = 0,
	ZHIHE_MBOX_DATA_CH0  = 1,		/* A210:die0-908--die0-902 */
	ZHIHE_MBOX_DATA_CH1  = 2,		
	ZHIHE_MBOX_DATA_CH2  = 3,		
};
enum zhihe_mbox_remote_id {
	ZHIHE_MBOX_REMOTE_CH0  = 0,		/* A210:die0-908--die0-902 */
	ZHIHE_MBOX_REMOTE_CH1  = 1,		
	ZHIHE_MBOX_REMOTE_CH2  = 2,			
};

struct zhihe_mbox_con_priv {
	enum zhihe_mbox_icu_cpu_id	icu_cpu_idx;
	enum zhihe_mbox_local_id	local_idx;
	enum zhihe_mbox_chan_type	type;
	void __iomem			*comm_local_base;
	void __iomem			*comm_remote_base;
	char				irq_desc[ZHIHE_MBOX_CHAN_NAME_SIZE];
	struct mbox_chan		*chan;
	struct tasklet_struct		txdb_tasklet;
};

struct zhihe_mbox_priv {
	struct device			*dev;
	void __iomem			*local_icu[ZHIHE_MBOX_CHANS];
	void __iomem			*remote_icu[ZHIHE_MBOX_CHANS - 1];
	void __iomem			*cur_cpu_ch_base;
	enum zhihe_mbox_icu_cpu_id	cur_icu_cpu_id;
	spinlock_t			mbox_lock; /* control register lock */

	struct mbox_controller		mbox;
	struct mbox_chan		mbox_chans[ZHIHE_MBOX_CHANS];

	struct zhihe_mbox_con_priv	con_priv[ZHIHE_MBOX_CHANS];
	struct clk			*clk;
	int				irq;
	int				version;
#ifdef CONFIG_PM_SLEEP
	struct zhihe_mbox_context	*ctx;
#endif
};

static struct zhihe_mbox_priv *to_zhihe_mbox_priv(struct mbox_controller *mbox)
{
	return container_of(mbox, struct zhihe_mbox_priv, mbox);
}

static void zhihe_mbox_write(struct zhihe_mbox_priv *priv, u32 val, u32 offs)
{
	iowrite32(val, priv->cur_cpu_ch_base + offs);
}

static u32 zhihe_mbox_read(struct zhihe_mbox_priv *priv, u32 offs)
{
	return ioread32(priv->cur_cpu_ch_base + offs);
}

static u32 zhihe_mbox_rmw(struct zhihe_mbox_priv *priv,
			   u32 off, u32 set, u32 clr)
{
	u32 val;
	unsigned long flags;

	spin_lock_irqsave(&priv->mbox_lock, flags);
	val = zhihe_mbox_read(priv, off);
	val &= ~clr;
	val |= set;
	zhihe_mbox_write(priv, val, off);
	spin_unlock_irqrestore(&priv->mbox_lock, flags);

	return val;
}

static void zhihe_mbox_chan_write(struct zhihe_mbox_con_priv *cp,
				   u32 val, u32 offs, bool is_remote)
{
	if (is_remote)
		iowrite32(val, cp->comm_remote_base + offs);
	else
		iowrite32(val, cp->comm_local_base + offs);
}

static u32 zhihe_mbox_chan_read(struct zhihe_mbox_con_priv *cp,
				 u32 offs, bool is_remote)
{
	if (is_remote)
		return ioread32(cp->comm_remote_base + offs);
	else
		return ioread32(cp->comm_local_base + offs);
}

static void zhihe_mbox_chan_rmw(struct zhihe_mbox_con_priv *cp,
				 u32 off, u32 set, u32 clr, bool is_remote)
{
	u32 val;
	unsigned long flags;
	struct zhihe_mbox_priv *priv = to_zhihe_mbox_priv(cp->chan->mbox);
	spin_lock_irqsave(&priv->mbox_lock, flags);
	val = zhihe_mbox_chan_read(cp, off, is_remote);
	val &= ~clr;
	val |= set;
	zhihe_mbox_chan_write(cp, val, off, is_remote);
	spin_unlock_irqrestore(&priv->mbox_lock, flags);
}

static void zhihe_mbox_chan_rd_data(struct zhihe_mbox_con_priv *cp,
				     void *data, bool is_remote)
{
	u32 i;
	u32 *arg = data;
	u32 off = ZHIHE_MBOX_INFO0;

	/* read info0 ~ info6, totally 28 bytes
	 * requires data memory size is 28 bytes
	 */
	for (i = 0; i < ZHIHE_MBOX_DATA_INFO_NUM; i++) {
		*arg = zhihe_mbox_chan_read(cp, off, is_remote);
		off += 4;
		arg++;
	}
}

static void zhihe_mbox_chan_wr_data(struct zhihe_mbox_con_priv *cp,
				     void *data, bool is_remote)
{
	u32 i;
	u32 *arg = data;
	u32 off = ZHIHE_MBOX_INFO0;

	/* write info0 ~ info6, totally 28 bytes
	 * requires data memory is 28 bytes valid data
	 */
	for (i = 0; i < ZHIHE_MBOX_DATA_INFO_NUM; i++) {
		zhihe_mbox_chan_write(cp, *arg, off, is_remote);
		off += 4;
		arg++;
	}
}

static void zhihe_mbox_chan_wr_ack(struct zhihe_mbox_con_priv *cp,
				    void *data, bool is_remote)
{
	u32 *arg = data;
	u32 off = ZHIHE_MBOX_INFO7;

	zhihe_mbox_chan_write(cp, *arg, off, is_remote);
}

static int zhihe_mbox_chan_id_to_mapbit(struct zhihe_mbox_con_priv *cp)
{
	int i;
	int mapbit = 0;
	struct zhihe_mbox_priv *priv = to_zhihe_mbox_priv(cp->chan->mbox);

	if (priv->version == ZHIHE_MBOX_V1) {
		for (i = 0; i < ZHIHE_MBOX_CHANS; i++) {
			if (i == cp->icu_cpu_idx)
				return mapbit;

			if (i != priv->cur_icu_cpu_id)
				mapbit++;
		}
	} else if (priv->version == ZHIHE_MBOX_V2) {
		for (i = 0; i < ZHIHE_MBOX_CHANS; i++) {
			if (i == cp->local_idx)
				return mapbit;

			if (i != ZHIHE_MBOX_INTERRUPT)
				mapbit++;
		}
	} else {
			dev_err(cp->chan->mbox->dev, "Unknown zhihe mailbox version\n");
	}

	if (i == ZHIHE_MBOX_CHANS)
		dev_err(cp->chan->mbox->dev, "convert to mapbit failed\n");

	return 0;
}

static void zhihe_mbox_txdb_tasklet(unsigned long data)
{
	struct zhihe_mbox_con_priv *cp = (struct zhihe_mbox_con_priv *)data;

	mbox_chan_txdone(cp->chan, 0);
}

static irqreturn_t zhihe_mbox_isr(int irq, void *p)
{
	u32 info0_data, info7_data;
	u32 sta, dat[ZHIHE_MBOX_DATA_INFO_NUM];
	u32 ack_magic = ZHIHE_MBOX_ACK_MAGIC;
	struct mbox_chan *chan = p;
	struct zhihe_mbox_con_priv *cp = chan->con_priv;
	int mapbit = zhihe_mbox_chan_id_to_mapbit(cp);
	struct zhihe_mbox_priv *priv = to_zhihe_mbox_priv(chan->mbox);

	sta = zhihe_mbox_read(priv, ZHIHE_MBOX_STA);
	if (!(sta & BIT(mapbit)))
		return IRQ_NONE;
	/* clear chan irq bit in STA register */
	zhihe_mbox_rmw(priv, ZHIHE_MBOX_CLR, BIT(mapbit), 0);
	/* rx doorbell */
	if (cp->type == ZHIHE_MBOX_TYPE_DB) {
		mbox_chan_received_data(cp->chan, NULL);
		return IRQ_HANDLED;
	}
	/* info0 is the protocol word, should not be zero! */
	info0_data = zhihe_mbox_chan_read(cp, ZHIHE_MBOX_INFO0, false);
	if (info0_data) {
		/* read info0~info6 data */
		zhihe_mbox_chan_rd_data(cp, dat, false);

		/* clear local info0 */
		zhihe_mbox_chan_write(cp, 0x0, ZHIHE_MBOX_INFO0, false);
		/* notify remote cpu */
		zhihe_mbox_chan_wr_ack(cp, &ack_magic, true);

		if (priv->version == ZHIHE_MBOX_V1) {
			/* CPU1 902/906 use polling mode to monitor info7 */
			if (cp->icu_cpu_idx != ZHIHE_MBOX_ICU_CPU1 &&
				cp->icu_cpu_idx != ZHIHE_MBOX_ICU_CPU2)
				zhihe_mbox_chan_rmw(cp, ZHIHE_MBOX_GEN,
							 ZHIHE_MBOX_GEN_TX_ACK, 0, true);
		}
		/* transfer the data to client */
		mbox_chan_received_data(chan, (void *)dat);
	}
	/* info7 magic value mean the real ack signal, not generate bit7 */
	info7_data = zhihe_mbox_chan_read(cp, ZHIHE_MBOX_INFO7, false);
	if (info7_data == ZHIHE_MBOX_ACK_MAGIC) {
		/* clear local info7 */
		zhihe_mbox_chan_write(cp, 0x0, ZHIHE_MBOX_INFO7, false);

		/* notify framework the last TX has completed */
		mbox_chan_txdone(chan, 0);
	}
	if (!info0_data && !info7_data)
		return IRQ_NONE;
	return IRQ_HANDLED;
}

static int zhihe_mbox_send_data(struct mbox_chan *chan, void *data)
{
	struct zhihe_mbox_con_priv *cp = chan->con_priv;
	if (cp->type == ZHIHE_MBOX_TYPE_DB)
		tasklet_schedule(&cp->txdb_tasklet);
	else
		zhihe_mbox_chan_wr_data(cp, data, true);
	zhihe_mbox_chan_rmw(cp, ZHIHE_MBOX_GEN, ZHIHE_MBOX_GEN_RX_DATA, 0,
			     true);
	return 0;
}

static int zhihe_mbox_startup(struct mbox_chan *chan)
{
	int ret;
	int mask_bit;
	u32 data[8] = {0};
	struct zhihe_mbox_con_priv *cp = chan->con_priv;
	struct zhihe_mbox_priv *priv = to_zhihe_mbox_priv(chan->mbox);
	/* clear local and remote generate and info0~info7 */
	zhihe_mbox_chan_rmw(cp, ZHIHE_MBOX_GEN, 0x0, 0xff, true);
	zhihe_mbox_chan_rmw(cp, ZHIHE_MBOX_GEN, 0x0, 0xff, false);
	zhihe_mbox_chan_wr_ack(cp, &data[7], true);
	zhihe_mbox_chan_wr_ack(cp, &data[7], false);
	zhihe_mbox_chan_wr_data(cp, &data[0], true);
	zhihe_mbox_chan_wr_data(cp, &data[0], false);
	/* enable the chan mask */
	mask_bit = zhihe_mbox_chan_id_to_mapbit(cp);
	zhihe_mbox_rmw(priv, ZHIHE_MBOX_MASK, BIT(mask_bit), 0);

	if (cp->type == ZHIHE_MBOX_TYPE_DB)
		/* tx doorbell doesn't have ACK, rx doorbell requires isr */
		tasklet_init(&cp->txdb_tasklet, zhihe_mbox_txdb_tasklet,
			     (unsigned long)cp);
	ret = request_irq(priv->irq, zhihe_mbox_isr, IRQF_SHARED |
			  IRQF_NO_SUSPEND, cp->irq_desc, chan);
	if (ret) {
		dev_err(priv->dev,
			"Unable to acquire IRQ %d\n", priv->irq);
		return ret;
	}
	return 0;
}

static void zhihe_mbox_shutdown(struct mbox_chan *chan)
{
	int mask_bit;
	struct zhihe_mbox_con_priv *cp = chan->con_priv;
	struct zhihe_mbox_priv *priv = to_zhihe_mbox_priv(chan->mbox);

	/* clear the chan mask */
	mask_bit = zhihe_mbox_chan_id_to_mapbit(cp);
	zhihe_mbox_rmw(priv, ZHIHE_MBOX_MASK, 0, BIT(mask_bit));

	free_irq(priv->irq, chan);
}

static const struct mbox_chan_ops zhihe_mbox_ops = {
	.send_data	= zhihe_mbox_send_data,
	.startup	= zhihe_mbox_startup,
	.shutdown	= zhihe_mbox_shutdown,
};

static int zhihe_mbox_init_generic(struct zhihe_mbox_priv *priv)
{
#ifdef CONFIG_PM_SLEEP
	priv->ctx = devm_kzalloc(priv->dev, sizeof(*priv->ctx), GFP_KERNEL);
	if (!priv->ctx)
		return -ENOMEM;
#endif
	/* Set default configuration */
	zhihe_mbox_write(priv, 0xff, ZHIHE_MBOX_CLR);
	zhihe_mbox_write(priv, 0x0, ZHIHE_MBOX_MASK);
	return 0;
}

static struct mbox_chan *zhihe_mbox_xlate(struct mbox_controller *mbox,
					   const struct of_phandle_args *sp)
{
	u32 chan, type;
	struct zhihe_mbox_con_priv *cp;
	struct zhihe_mbox_priv *priv = to_zhihe_mbox_priv(mbox);

	if (sp->args_count != 2) {
		dev_err(mbox->dev,
			"Invalid argument count %d\n", sp->args_count);
		return ERR_PTR(-EINVAL);
	}

	chan = sp->args[0]; /* comm remote channel */
	type = sp->args[1]; /* comm channel type */
	if (chan >= mbox->num_chans) {
		dev_err(mbox->dev, "Not supported channel number: %d\n", chan);
		return ERR_PTR(-EINVAL);
	}

	if (priv->version == ZHIHE_MBOX_V1) {
		if (chan == priv->cur_icu_cpu_id) {
			dev_err(mbox->dev, "Cannot communicate with yourself\n");
			return ERR_PTR(-EINVAL);
		}
	}

	if (type > ZHIHE_MBOX_TYPE_DB) {
		dev_err(mbox->dev,
			"Not supported the type for channel[%d]\n", chan);
		return ERR_PTR(-EINVAL);
	}

	cp = mbox->chans[chan].con_priv;
	cp->type = type;

	return &mbox->chans[chan];
}

static int zhihe_mbox_probe(struct platform_device *pdev)
{
	int ret;
	unsigned int i;
	unsigned int mbox_id;
	struct resource *res;
	struct zhihe_mbox_priv *priv;
	unsigned int remote_idx = 0;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ret = of_property_read_u32(np, "version", &priv->version);
	if (ret) {
		priv->version = ZHIHE_MBOX_V1;
	}

	if (of_property_read_u32(np, "icu_cpu_id", &priv->cur_icu_cpu_id)) {
		dev_err(dev, "icu_cpu_id is missing\n");
		return -EINVAL;
	}

	if (priv->version == ZHIHE_MBOX_V1) {
		if (priv->cur_icu_cpu_id != ZHIHE_MBOX_ICU_CPU0 &&
			priv->cur_icu_cpu_id != ZHIHE_MBOX_ICU_CPU3) {
			dev_err(dev, "icu_cpu_id is invalid\n");
			return -EINVAL;
		}
	}
	priv->dev = dev;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "local_addr0");
	if(res!=NULL) {
		if (priv->version == ZHIHE_MBOX_V1) {
			mbox_id = ZHIHE_MBOX_ICU_CPU0;
		} else if (priv->version == ZHIHE_MBOX_V2) {
			mbox_id = ZHIHE_MBOX_DATA_CH0;
		} else {
			dev_err(dev, "Unknown zhihe mailbox version\n");
		}
		priv->local_icu[mbox_id] = devm_ioremap_resource(dev, res);
		if (IS_ERR(priv->local_icu[mbox_id]))
			return PTR_ERR(priv->local_icu[mbox_id]);
	} else {
		dev_err(dev, "local_addr0 is NULL\n");
	}

	if (priv->version == ZHIHE_MBOX_V2) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "local_addr1");
		if(res!=NULL) {
			mbox_id = ZHIHE_MBOX_DATA_CH1;
			priv->local_icu[mbox_id] = devm_ioremap_resource(dev, res);
			if (IS_ERR(priv->local_icu[mbox_id]))
				return PTR_ERR(priv->local_icu[mbox_id]);
		} else {
			dev_err(dev, "local_addr1 is NULL\n");
		}

		res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "interrupt_addr");
		if(res!=NULL) {
			mbox_id = ZHIHE_MBOX_INTERRUPT;
			priv->local_icu[mbox_id] = devm_ioremap_resource(dev, res);
			if (IS_ERR(priv->local_icu[mbox_id]))
				return PTR_ERR(priv->local_icu[mbox_id]);
		} else {
			dev_err(dev, "interrupt_addr is NULL\n");
		}
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "remote_icu0");
	if(res!=NULL) {
		if (priv->version == ZHIHE_MBOX_V1) {
			mbox_id = 0;
		} else if (priv->version == ZHIHE_MBOX_V2) {
			mbox_id = ZHIHE_MBOX_REMOTE_CH0;
		} else {
			dev_err(dev, "Unknown zhihe mailbox version\n");
		}
		priv->remote_icu[mbox_id] = devm_ioremap_resource(dev, res);
		if (IS_ERR(priv->remote_icu[mbox_id]))
			return PTR_ERR(priv->remote_icu[mbox_id]);
	} else {
		dev_err(dev, "remote_icu0 is NULL\n");
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "remote_icu1");
	if(res!=NULL) {
		if (priv->version == ZHIHE_MBOX_V1) {
			mbox_id = 1;
		} else if (priv->version == ZHIHE_MBOX_V2) {
			mbox_id = ZHIHE_MBOX_REMOTE_CH1;
		} else {
			dev_err(dev, "Unknown zhihe mailbox version\n");
		}

		priv->remote_icu[mbox_id] = devm_ioremap_resource(dev, res);
		if (IS_ERR(priv->remote_icu[mbox_id]))
			return PTR_ERR(priv->remote_icu[mbox_id]);
	} else {
		dev_err(dev, "remote_icu1 is NULL\n");
	}

	if (priv->version == ZHIHE_MBOX_V1) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "remote_icu2");
		if(res!=NULL) {
			mbox_id = 2;
			priv->remote_icu[mbox_id] = devm_ioremap_resource(dev, res);
			if (IS_ERR(priv->remote_icu[mbox_id]))
				return PTR_ERR(priv->remote_icu[mbox_id]);
		} else {
			dev_err(dev, "remote_icu2 is NULL\n");
		}

		priv->local_icu[ZHIHE_MBOX_ICU_CPU1] =
			priv->local_icu[ZHIHE_MBOX_ICU_CPU0] +
			ZHIHE_MBOX_CHAN_RES_SIZE;

		priv->local_icu[ZHIHE_MBOX_ICU_CPU2] =
			priv->local_icu[ZHIHE_MBOX_ICU_CPU1] +
			ZHIHE_MBOX_CHAN_RES_SIZE;

		priv->local_icu[ZHIHE_MBOX_ICU_CPU3] =
			priv->local_icu[ZHIHE_MBOX_ICU_CPU2] +
			ZHIHE_MBOX_CHAN_RES_SIZE;
		mbox_id = priv->cur_icu_cpu_id;
	}else {
		mbox_id = ZHIHE_MBOX_INTERRUPT;
	}

	priv->cur_cpu_ch_base = priv->local_icu[mbox_id];
	priv->irq = platform_get_irq(pdev, 0);
	if (priv->irq < 0)
		return priv->irq;
	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk)) {
		if (PTR_ERR(priv->clk) != -ENOENT)
			return PTR_ERR(priv->clk);
		priv->clk = NULL;
	}
	ret = clk_prepare_enable(priv->clk);
	if (ret) {
		dev_err(dev, "Failed to enable clock\n");
		return ret;
	}
	/* init the chans */
	if (priv->version == ZHIHE_MBOX_V1) {
		for (i = 0; i < ZHIHE_MBOX_CHANS; i++) {
			struct zhihe_mbox_con_priv *cp = &priv->con_priv[i];
			cp->icu_cpu_idx = i;
			cp->chan = &priv->mbox_chans[i];
			priv->mbox_chans[i].con_priv = cp;
			snprintf(cp->irq_desc, sizeof(cp->irq_desc),
				 "zhihe_mbox_chan[%i]", cp->icu_cpu_idx);
			cp->comm_local_base = priv->local_icu[i];
			if (i != priv->cur_icu_cpu_id) {
				cp->comm_remote_base = priv->remote_icu[remote_idx];
				remote_idx++;
			}
		}
	} else if (priv->version == ZHIHE_MBOX_V2) {
		for (i = 0; i < ZHIHE_MBOX_CHANS; i++) {
			struct zhihe_mbox_con_priv *cp = &priv->con_priv[i];
			cp->local_idx = i;
			cp->chan = &priv->mbox_chans[i];
			priv->mbox_chans[i].con_priv = cp;
			snprintf(cp->irq_desc, sizeof(cp->irq_desc),
				 "zhihe_mbox_chan[%i]", cp->local_idx);
			cp->comm_local_base = priv->local_icu[i];
			if (i != ZHIHE_MBOX_INTERRUPT) {
				cp->comm_remote_base = priv->remote_icu[remote_idx];
				remote_idx++;
			}
		}
	} else {
		dev_err(dev, "Unknown zhihe mailbox version\n");
	}

	spin_lock_init(&priv->mbox_lock);

	priv->mbox.dev = dev;
	priv->mbox.ops = &zhihe_mbox_ops;
	priv->mbox.chans = priv->mbox_chans;
	priv->mbox.num_chans = ZHIHE_MBOX_CHANS;
	priv->mbox.of_xlate = zhihe_mbox_xlate;
	priv->mbox.txdone_irq = true;

	platform_set_drvdata(pdev, priv);

	ret = zhihe_mbox_init_generic(priv);
	if (ret) {
		dev_err(dev, "Failed to init mailbox context\n");
		return ret;
	}
	return devm_mbox_controller_register(dev, &priv->mbox);
}

static int zhihe_mbox_remove(struct platform_device *pdev)
{
	struct zhihe_mbox_priv *priv = platform_get_drvdata(pdev);

	clk_disable_unprepare(priv->clk);

	return 0;
}

static const struct of_device_id zhihe_mbox_dt_ids[] = {
	{ .compatible = "zhihe,mailbox" },
	{ },
};
MODULE_DEVICE_TABLE(of, zhihe_mbox_dt_ids);

#ifdef CONFIG_PM_SLEEP
static int __maybe_unused zhihe_mbox_suspend_noirq(struct device *dev)
{
	u32 i;
	struct zhihe_mbox_priv *priv = dev_get_drvdata(dev);
	struct zhihe_mbox_context *ctx = priv->ctx;

	/*
	 * ONLY interrupt mask bit should be stored and restores.
	 * INFO data all assumed to be lost.
	 */
	for (i = 0 ; i < ZHIHE_MBOX_CHANS; i++)
		ctx->intr_mask[i] = ioread32(priv->local_icu[i] +
					     ZHIHE_MBOX_MASK);

	return 0;
}

static int __maybe_unused zhihe_mbox_resume_noirq(struct device *dev)
{
	u32 i;
	struct zhihe_mbox_priv *priv = dev_get_drvdata(dev);
	struct zhihe_mbox_context *ctx = priv->ctx;

	for (i = 0 ; i < ZHIHE_MBOX_CHANS; i++)
		iowrite32(ctx->intr_mask[i],
			  priv->local_icu[i] + ZHIHE_MBOX_MASK);

	return 0;
}

#endif

static int __maybe_unused zhihe_mbox_runtime_suspend(struct device *dev)
{
	struct zhihe_mbox_priv *priv = dev_get_drvdata(dev);

	clk_disable_unprepare(priv->clk);

	return 0;
}

static int __maybe_unused zhihe_mbox_runtime_resume(struct device *dev)
{
	struct zhihe_mbox_priv *priv = dev_get_drvdata(dev);
	int ret = clk_prepare_enable(priv->clk);

	if (ret)
		dev_err(dev, "failed to enable clock\n");

	return ret;
}

static const struct dev_pm_ops zhihe_mbox_pm_ops = {
#ifdef CONFIG_PM_SLEEP
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(zhihe_mbox_suspend_noirq,
				      zhihe_mbox_resume_noirq)
#endif
	SET_RUNTIME_PM_OPS(zhihe_mbox_runtime_suspend,
			   zhihe_mbox_runtime_resume, NULL)
};

static struct platform_driver zhihe_mbox_driver = {
	.probe	= zhihe_mbox_probe,
	.remove	= zhihe_mbox_remove,
	.driver	= {
		.name		= "zhihe_mbox",
		.of_match_table	= zhihe_mbox_dt_ids,
		.pm		= &zhihe_mbox_pm_ops,
	},
};
module_platform_driver(zhihe_mbox_driver);

MODULE_AUTHOR("hongkun.xu <xuhongkun@zhcomputing.com>");
MODULE_AUTHOR("xionglue.huang <huangxionglue@zhcomputing.com>");
MODULE_DESCRIPTION("a210 Mailbox IPC driver");
MODULE_LICENSE("GPL v2");
