#ifndef _ZHIHE_PDM_H
#define _ZHIHE_PDM_H

#include <linux/io.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/scatterlist.h>
#include <linux/sh_dma.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <sound/pcm.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/dmaengine_pcm.h>

#include "zhihe-pcm.h"

/* PDM REGISTER */
#define CP_VAD_CTRL			(0x00)
#define CP_ZCR_TH_CTRL			(0x04)
#define CP_STE_HTH_CTRL			(0x08)
#define CP_STE_LTH_CTRL			(0x0C)
#define	CP_VAD_INTR_MFLAG		(0x10)
#define	CP_VAD_INTR_CLR			(0x14)
#define	CP_VAD_INTR_FLAG		(0x18)
#define	CP_VAD_INTR_MASK		(0x1C)
#define CP_DAI_EN			(0x20)
#define CP_DAI_CTRL			(0x24)
#define CP_FIFO_CNT			(0x28)
#define CP_FIFO_TH_CTRL			(0x2C)
#define CP_FIFO_INTR_MFLAG		(0x30)
#define CP_FIFO_INTR_CLR		(0x34)
#define CP_FIFO_INTR_FLAG		(0x38)
#define CP_FIFO_INTR_MASK		(0x3C)
#define CP_CIC_L_ST_OFFSET		(0x40)
#define CP_CIC_L_LP_OFFSET		(0x44)
#define CP_CIC_R_ST_OFFSET		(0x48)
#define CP_CIC_R_LP_OFFSET		(0x4C)
#define CP_FIFO0_L 			(0x50)
#define CP_FIFO0_R 			(0x54)
#define CP_FIFO1_L			(0x58)
#define CP_FIFO1_R			(0x5C)
#define CP_FIFO2_L			(0x60)
#define CP_FIFO2_R			(0x64)
#define CP_FIFO3_L			(0x68)
#define CP_FIFO3_R			(0x6C)
#define CP_VAD_IP_ID			(0x74)

/* VADCTRL */
#define VADCTRL_PDM_EN_MSK		GENMASK(1, 0)
#define VADCTRL_PDM_EN_POS		(0U)
#define VADCTRL_VAD_EN_SEL		(0x1U << VADCTRL_PDM_EN_POS)
#define VADCTRL_DATA_TRANS_EN_SEL	(0x2U << VADCTRL_PDM_EN_POS)
#define VADCTRL_CH_EN_MSK		GENMASK(7, 4)
#define VADCTRL_CH_EN_POS		(4U)
#define VADCTRL_CH0_EN			(0x1U << VADCTRL_CH_EN_POS)
#define VADCTRL_CH1_EN			(0x2U << VADCTRL_CH_EN_POS)
#define VADCTRL_CH2_EN			(0x4U << VADCTRL_CH_EN_POS)
#define VADCTRL_CH3_EN			(0x8U << VADCTRL_CH_EN_POS)
#define VADCTRL_ALL_CH_EN		(0xFU << VADCTRL_CH_EN_POS)

/* DAI_EN */
#define DAI_EN_MSK 			BIT(0)
#define DAI_EN_POS			(0U)
#define DAI_EN_SEL 			(0x1U << DAI_EN_POS)

/* DAI_CTRL */
#define DAI_LP_GAIN_MSK			GENMASK(22, 20)
#define DAI_LP_GAIN_POS			(20U)
#define DAI_ST_GAIN_MSK			GENMASK(26, 24)
#define DAI_ST_GAIN_POS			(24U)

/* PERI1 SYSREG */
#define PERI1_PDM_TDM_CSR		(0x0C)
#define PERI1_DMA_REUSE_CSR		(0x10)

/* PDM CSR */
#define PDM_VAD_EN_MSK			BIT(0)
#define PDM_VAD_EN_POS			(0U)
#define PDM_VAD_EN_SEL			(0x1U << PDM_VAD_EN_POS)

/* DMA REUSE CSR */
#define PDM_DMA_REUSE_MSK		GENMASK(5, 0)
#define PDM_DMA_REUSE_POS		(0U)
#define PDM_DMA_REUSE_SEL		(0x2AU << PDM_DMA_REUSE_POS)

#define CIC_OFFSET_CNT 4

struct zhihe_pdm_priv {
	void __iomem *regs;
	struct device *dev;
	struct regmap *regmap;
	struct regmap *sys_regmap;
	struct clk *mclk;
	struct clk *pclk;
	struct snd_dmaengine_dai_dma_data dma_params_rx;

	char name[16];
	u32 dma_maxburst;
	u32 mclk_keepon;
	u32 gain_dB;
	u32 cic_offset[CIC_OFFSET_CNT];
	bool cic_offset_en;
};

#endif /* _ZHIHE_PDM_H */
