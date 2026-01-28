/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Zhihe audio TDM audio support
 *
 * Copyright (C) 2024 Zhihe Computing Technology (Shenzhen) Co., Ltd..
 *
 * Author: Anonymous <anonymous@zhcomputing.com>
 */

#ifndef _ZHIHE_TDM_H
#define _ZHIHE_TDM_H

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/regmap.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <sound/dmaengine_pcm.h>

#define AUDIO_DIVCLK0		294912000
#define AUDIO_DIVCLK1		316108800

#define ZHIHE_TDM_DMABUF_SIZE	(64 * 1024)
#define ZHIHE_TDM_DMA_MAX_BURST	16
#define ZHIHE_TDM_DMA_FIX_BURST	1

#define ZHIHE_TDM_SLOT_MAX_CNT	8
#define DAI_TDM_SLOT		0

#define ZHIHE_TDM_BIT_WIDTH	32
#define ZHIHE_TDM_REG_STRIDE	4

#define ZHIHE_TDM_BITWTH_16	16
#define ZHIHE_TDM_BITWTH_24	24
#define ZHIHE_TDM_BITWTH_32	32

#define ZHIHE_TDM_RATES SNDRV_PCM_RATE_8000_384000
#define ZHIHE_TDM_FMTS (SNDRV_PCM_FMTBIT_FLOAT_LE | \
			SNDRV_PCM_FMTBIT_S32_LE | \
			SNDRV_PCM_FMTBIT_S24_LE | \
			SNDRV_PCM_FMTBIT_S16_LE | \
			SNDRV_PCM_FMTBIT_S8)

#define TDM_TDMEN		0x00
#define TDM_TDMCTL		0x04
#define TDM_CHOFFSET1		0x08
#define TDM_CHOFFSET2		0x0c
#define TDM_CHOFFSET3		0x10
#define TDM_CHOFFSET4		0x14
#define TDM_FIFOTL1		0x18
#define TDM_FIFOTL2		0x1C
#define TDM_FIFOTL3		0x20
#define TDM_FIFOTL4		0x24
#define TDM_SR			0x28
#define TDM_IMR			0x2C
#define TDM_ISR			0x30
#define TDM_RISR		0x34
#define TDM_ICR			0x38
#define TDM_DMACTL		0x3C
#define TDM_DMADL		0x40
#define TDM_LDR1		0x44
#define TDM_RDR1		0x48
#define TDM_LDR2		0x4C
#define TDM_RDR2		0x50
#define TDM_LDR3		0x54
#define TDM_RDR3		0x58
#define TDM_LDR4		0x5C
#define TDM_RDR4		0x60
#define TDM_DIV0_LEVEL		0x64

#define TDM_MODE_MASTER			0x1
#define TDM_MODE_SLAVE			0x0
#define TDMCTL_MODE_POS			(0U)
#define TDMCTL_MODE_MSK			(0x1U << TDMCTL_MODE_POS)
#define TDMCTL_MODE_SEL(X)		(X << TDMCTL_MODE_POS)
#define TDMCTL_DATAWTH_POS		(4U)
#define TDMCTL_DATAWTH_MSK		(0x3U << TDMCTL_DATAWTH_POS)
#define TDMCTL_DATAWTH_SEL(X)		(X << TDMCTL_DATAWTH_POS)
#define TDMCTL_CHNUM_POS		(8U)
#define TDMCTL_CHNUM_MSK		(0x3U << TDMCTL_CHNUM_POS)
#define TDMCTL_CHNUM_SEL(X)		(X << TDMCTL_CHNUM_POS)
#define TDMCTL_CHNUM_2			0x0
#define TDMCTL_CHNUM_4			0x1
#define TDMCTL_CHNUM_6			0x2
#define TDMCTL_CHNUM_8			0x3
#define TDMCTL_SPEDGE_POS		(13U)
#define TDMCTL_SPEDGE_MSK		(0x1U << TDMCTL_SPEDGE_POS)
#define TDMCTL_SPEDGE_SEL(X)		(X << TDMCTL_SPEDGE_POS)

#define TDMCTL_DATAWTH_16BIT_PACKED	0x0
#define TDMCTL_DATAWTH_16BIT		0x1
#define TDMCTL_DATAWTH_24BIT		0x2
#define TDMCTL_DATAWTH_32BIT		0x3

#define TDMCTL_DIV0_MASK		0xFFF

#define DMACTL_DMAEN_POS		(0U)
#define DMACTL_DMAEN_MSK		(0x1U << DMACTL_DMAEN_POS)
#define DMACTL_DMAEN_SEL(X)		(X << DMACTL_DMAEN_POS)

#define DMACTL_DMADL_POS		(0U)
#define DMACTL_DMADL_MSK		(0x7U << DMACTL_DMADL_POS)
#define DMACTL_DMADL_SEL(X)		(X << DMACTL_DMADL_POS)

#define TDMCTL_TDMEN_POS		(0U)
#define TDMCTL_TDMEN_MSK		(0x1U << TDMCTL_TDMEN_POS)
#define TDMCTL_TDMEN_SEL(X)		(X << TDMCTL_TDMEN_POS)

/* PERI1 SYSREG */
#define PERI1_DMA_REUSE_CSR		(0x10)

/* DMA REUSE CSR */
#define TDM_DMA_REUSE_MSK		GENMASK(5, 0)
#define TDM_DMA_REUSE_POS		(0U)
#define TDM_DMA_REUSE_SEL		(0x15U << TDM_DMA_REUSE_POS)

#define TDM_SPEDGE_UP			0
#define TDM_SPEDGE_DOWN			1
#define TDM_SPEDGE_DEFAULT		TDM_SPEDGE_DOWN

struct zhihe_tdm_soc_drvdata {
	int slot_num;
	char tdm_dai_name[16];
};

struct zhihe_tdm_priv {
	void __iomem *regs;

	struct clk *sclk;
	struct device *dev;
	struct regmap *regmap;
	struct regmap *sys_regmap;
	struct snd_soc_dai_driver *dev_dai;
	struct snd_dmaengine_dai_dma_data dma_params_rx;
	const struct zhihe_tdm_soc_drvdata *drvdata;
	
	unsigned int dai_fmt;
	unsigned int dma_maxburst;
	unsigned int cfg_off;
	unsigned int mode;
	unsigned int slots;
	unsigned int spedge;
	int irq;

	u32 suspend_tdmctl;
	u32 suspend_choffset1;
	u32 suspend_choffset2;
	u32 suspend_choffset3;
	u32 suspend_choffset4;
	u32 suspend_fifotl1;
	u32 suspend_fifotl2;
	u32 suspend_fifotl3;
	u32 suspend_fifotl4;
	u32 suspend_imr;
	u32 suspend_dmadl;
	u32 suspend_div0level;
	u32 cpr_peri_div_sel;
	u32 cpr_peri_clk_sel;

	spinlock_t zhihe_tdm_lock;
};

#endif /* _ZHIHE_TDM_H */
