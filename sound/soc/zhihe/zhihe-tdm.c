/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Zhihe audio TDM audio support
 *
 * Copyright (C) 2024 Zhihe Computing Technology (Shenzhen) Co., Ltd..
 *
 * Author: Anonymous <anonymous@zhcomputing.com>
 */

#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <sound/initval.h>
#include <sound/pcm_params.h>

#include "zhihe-pcm.h"
#include "zhihe-tdm.h"

static int is_tdm_probe		= 0;
static int is_set_fmt_dai	= 0;
static int slot_use_cnt		= 0;

struct zhihe_tdm_priv *tdm_host_priv;

static u32 zhihe_special_sample_rates[] = { 11025, 22050, 44100, 88200, 176400 };

static int zhihe_tdm_dai_probe(struct snd_soc_dai *dai)
{
	struct zhihe_tdm_priv *tdm_priv = snd_soc_dai_get_drvdata(dai);

	if (!tdm_priv)
		return -EIO;

	snd_soc_dai_init_dma_data(dai, NULL, &tdm_priv->dma_params_rx);

	return 0;
}

static void zhihe_tdm_snd_rxctrl(struct zhihe_tdm_priv *chip, u32 on)
{
	regmap_update_bits(chip->regmap, TDM_DMACTL, DMACTL_DMAEN_MSK,
			   DMACTL_DMAEN_SEL(on));
	regmap_update_bits(chip->regmap, TDM_TDMEN, TDMCTL_TDMEN_MSK,
			   TDMCTL_TDMEN_SEL(on));
}

static int zhihe_tdm_dai_trigger(struct snd_pcm_substream *substream, int cmd,
				 struct snd_soc_dai *dai)
{
	int ret = 0;
	struct zhihe_tdm_priv *tdm_priv = snd_soc_dai_get_drvdata(dai);

	if (!tdm_priv || !tdm_priv->regmap)
		return -EINVAL;

	spin_lock(&tdm_priv->zhihe_tdm_lock);

	switch(cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (!slot_use_cnt) {
			zhihe_tdm_snd_rxctrl(tdm_priv, 1);
			tdm_priv->state |= TDM_STATE_RUNNING;
		}
		++slot_use_cnt;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		--slot_use_cnt;
		/* work around for DMAC stop issue. */
		dmaengine_terminate_async(snd_dmaengine_pcm_get_chan(substream));
		if (!slot_use_cnt) {
			zhihe_tdm_snd_rxctrl(tdm_priv, 0);
			tdm_priv->state &= ~TDM_STATE_RUNNING;
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}

	spin_unlock(&tdm_priv->zhihe_tdm_lock);

	return ret;
}

static int zhihe_tdm_set_fmt_dai(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct zhihe_tdm_priv *tdm_priv = snd_soc_dai_get_drvdata(dai);
	int ret = 0;

	if (!tdm_priv->regmap)
		return -EINVAL;

	if (is_set_fmt_dai) {
		pr_info("TDM fmt dai already setting\n");
		return 0;
	}

	pm_runtime_resume_and_get(tdm_priv->dev);

	spin_lock(&tdm_priv->zhihe_tdm_lock);

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
	case SND_SOC_DAIFMT_DSP_B:
		break;
	default:
		pr_err("Unknown fmt dai\n");
		ret = -EINVAL;
		goto finish;
	}

	/* CPU CLK in FUNC "snd_soc_daifmt_clock_provider_flipped" is flipped */
	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_BP_FP:
		/* CPU MASTER */
		tdm_priv->mode = TDM_MODE_MASTER;
		break;
	case SND_SOC_DAIFMT_BC_FC:
		/* CPU SLAVE */
		tdm_priv->mode = TDM_MODE_SLAVE;
		break;
	default:
		pr_err("Unknown clock provider dai\n");
		ret = -EINVAL;
		goto finish;
	}

	regmap_update_bits(tdm_priv->regmap, TDM_TDMCTL, TDMCTL_MODE_MSK,
			   TDMCTL_MODE_SEL(tdm_priv->mode));

	// bclk发送数据 0：上升沿 1：下降沿
	regmap_update_bits(tdm_priv->regmap, TDM_TDMCTL, TDMCTL_SPEDGE_MSK,
			   TDMCTL_SPEDGE_SEL(tdm_priv->spedge));

	// tdm fifo dma 阈值
	regmap_update_bits(tdm_priv->regmap, TDM_DMADL, DMACTL_DMADL_MSK,
			   DMACTL_DMADL_SEL(0));

	is_set_fmt_dai++;

finish:
	spin_unlock(&tdm_priv->zhihe_tdm_lock);

	pm_runtime_put_sync(tdm_priv->dev);

	return ret;
}

static void zhihe_tdm_set_div(struct zhihe_tdm_priv *chip, u32 sample_rate,
			      u32 width)
{
	bool is_divclk1 = false;
	u32 src_clk;
	u32 div0;
	int i;

	clk_set_rate(chip->sclk, AUDIO_DIVCLK0);

	for (i = 0; i < ARRAY_SIZE(zhihe_special_sample_rates); ++i) {
		if (zhihe_special_sample_rates[i] == sample_rate) {
			clk_set_rate(chip->sclk, AUDIO_DIVCLK1);
			is_divclk1 = true;
			break;
		}
	}

	/**
	 * audio_divclk1 for 44.1k fsb.
	 * audio_divclk0 for 48k fsb.
	 */
	src_clk = is_divclk1 ? AUDIO_DIVCLK1 : AUDIO_DIVCLK0;

	div0 = src_clk / (sample_rate * (width * chip->slots));
	regmap_update_bits(chip->regmap, TDM_DIV0_LEVEL, TDMCTL_DIV0_MASK,
			   div0);
}

static int zhihe_tdm_dai_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params,
				   struct snd_soc_dai *dai)
{
	struct zhihe_tdm_priv *tdm_priv =  snd_soc_dai_get_drvdata(dai);
	u32 datawth;
	u32 chn_num;
	u32 sample_rate;
	u32 width;

	if (!tdm_priv || !tdm_priv->regmap)
		return -EINVAL;

	/* mask all tdm-interrupt to avoid too much irq info. */
	writel(0, tdm_priv->regs + TDM_IMR);

	sample_rate = params_rate(params);

	switch(params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		/* TDMCTL_DATAWTH_16BIT. */
		datawth = TDMCTL_DATAWTH_16BIT_PACKED;
		width = ZHIHE_TDM_BITWTH_16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		datawth = TDMCTL_DATAWTH_24BIT;
		width = ZHIHE_TDM_BITWTH_24;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
	case SNDRV_PCM_FMTBIT_FLOAT_LE:
		datawth = TDMCTL_DATAWTH_32BIT;
		width = ZHIHE_TDM_BITWTH_32;
		break;
	default:
		pr_err("Unknown data format\n");
		return -EINVAL;
	}

	switch(tdm_priv->slots) {
	case 2:
		chn_num = TDMCTL_CHNUM_2;
		break;
	case 4:
		chn_num = TDMCTL_CHNUM_4;
		break;
	case 6:
		chn_num = TDMCTL_CHNUM_6;
		break;
	case 8:
		chn_num = TDMCTL_CHNUM_8;
		break;
	default:
		pr_err("Not support slot num\n");
		return -EINVAL;
	}

	regmap_update_bits(tdm_priv->regmap, TDM_TDMCTL, TDMCTL_DATAWTH_MSK,
			   TDMCTL_DATAWTH_SEL(datawth));

	regmap_update_bits(tdm_priv->regmap, TDM_TDMCTL, TDMCTL_CHNUM_MSK,
			   TDMCTL_CHNUM_SEL(chn_num));

	zhihe_tdm_set_div(tdm_priv, sample_rate, width);

	return 0;
}


static const struct snd_soc_dai_ops zhihe_tdm_dai_ops = {
	.probe		= zhihe_tdm_dai_probe,
	.trigger	= zhihe_tdm_dai_trigger,
	.set_fmt	= zhihe_tdm_set_fmt_dai,
	.hw_params	= zhihe_tdm_dai_hw_params,
	.set_tdm_slot	= NULL,
};

static struct snd_soc_dai_driver zhihe_tdm_soc_dai[] = {
	/* tdm dai. */
	{
		.capture = {
			.rates		= ZHIHE_TDM_RATES,
			.formats	= ZHIHE_TDM_FMTS,
			.channels_min	= 1,
			.channels_max	= 1,
		},
		.ops = &zhihe_tdm_dai_ops,
		.symmetric_rate = 1,
	},
};

static const struct snd_soc_component_driver zhihe_tdm_soc_component = {
	.name = "zhihe_tdm",
};

static bool zhihe_tdm_wr_reg(struct device *dev, unsigned int reg)
{
	return true;
}

static bool zhihe_tdm_rd_reg(struct device *dev, unsigned int reg)
{
	return true;
}

static const struct regmap_config zhihe_tdm_regmap_config = {
	.reg_bits	= ZHIHE_TDM_BIT_WIDTH,
	.val_bits	= ZHIHE_TDM_BIT_WIDTH,
	.reg_stride	= ZHIHE_TDM_REG_STRIDE,
	.max_register	= TDM_DIV0_LEVEL,
	.writeable_reg	= zhihe_tdm_wr_reg,
	.readable_reg	= zhihe_tdm_rd_reg,
	.cache_type	= REGCACHE_NONE,
};

static int __maybe_unused zhihe_tdm_runtime_suspend(struct device *dev)
{
	struct zhihe_tdm_priv *tdm_priv = dev_get_drvdata(dev);

	if (!tdm_priv->regmap)
		return -EINVAL;

	regcache_cache_only(tdm_priv->regmap, true);

	return 0;
}

static int __maybe_unused zhihe_tdm_runtime_resume(struct device *dev)
{
	struct zhihe_tdm_priv *tdm_priv = dev_get_drvdata(dev);

	if (!tdm_priv->regmap)
		return -EINVAL;

	regcache_cache_only(tdm_priv->regmap, false);

	return 0;
}

static int __maybe_unused zhihe_tdm_suspend(struct device *dev)
{
	struct zhihe_tdm_priv *tdm_priv = dev_get_drvdata(dev);

	if (!tdm_priv->regmap)
		return -EINVAL;

	pm_runtime_get_sync(dev);

	regmap_read(tdm_priv->regmap, TDM_TDMCTL,
		    &tdm_priv->suspend_tdmctl);
	regmap_read(tdm_priv->regmap, TDM_CHOFFSET1,
		    &tdm_priv->suspend_choffset1);
	regmap_read(tdm_priv->regmap, TDM_CHOFFSET2,
		    &tdm_priv->suspend_choffset2);
	regmap_read(tdm_priv->regmap, TDM_CHOFFSET3,
		    &tdm_priv->suspend_choffset3);
	regmap_read(tdm_priv->regmap, TDM_CHOFFSET4,
		    &tdm_priv->suspend_choffset4);
	regmap_read(tdm_priv->regmap, TDM_FIFOTL1,
		    &tdm_priv->suspend_fifotl1);
	regmap_read(tdm_priv->regmap, TDM_FIFOTL2,
		    &tdm_priv->suspend_fifotl2);
	regmap_read(tdm_priv->regmap, TDM_FIFOTL3,
		    &tdm_priv->suspend_fifotl3);
	regmap_read(tdm_priv->regmap, TDM_FIFOTL4,
		    &tdm_priv->suspend_fifotl4);
	regmap_read(tdm_priv->regmap, TDM_IMR,
		    &tdm_priv->suspend_imr);
	regmap_read(tdm_priv->regmap, TDM_DMADL,
		    &tdm_priv->suspend_dmadl);
	regmap_read(tdm_priv->regmap, TDM_DIV0_LEVEL,
		    &tdm_priv->suspend_div0level);

	pm_runtime_put_sync(dev);

	return 0;
}

static int __maybe_unused zhihe_tdm_resume(struct device *dev)
{
	struct zhihe_tdm_priv *tdm_priv = dev_get_drvdata(dev);

	if (!tdm_priv->regmap)
		return -EINVAL;

	pm_runtime_get_sync(dev);

	regmap_write(tdm_priv->regmap, TDM_TDMCTL,
		     tdm_priv->suspend_tdmctl);
	regmap_write(tdm_priv->regmap, TDM_CHOFFSET1,
		     tdm_priv->suspend_choffset1);
	regmap_write(tdm_priv->regmap, TDM_CHOFFSET2,
		     tdm_priv->suspend_choffset2);
	regmap_write(tdm_priv->regmap, TDM_CHOFFSET3,
		     tdm_priv->suspend_choffset3);
	regmap_write(tdm_priv->regmap, TDM_CHOFFSET4,
		     tdm_priv->suspend_choffset4);
	regmap_write(tdm_priv->regmap, TDM_FIFOTL1,
		     tdm_priv->suspend_fifotl1);
	regmap_write(tdm_priv->regmap, TDM_FIFOTL2,
		     tdm_priv->suspend_fifotl2);
	regmap_write(tdm_priv->regmap, TDM_FIFOTL3,
		     tdm_priv->suspend_fifotl3);
	regmap_write(tdm_priv->regmap, TDM_FIFOTL4,
		     tdm_priv->suspend_fifotl4);
	regmap_write(tdm_priv->regmap, TDM_IMR,
		     tdm_priv->suspend_imr);
	regmap_write(tdm_priv->regmap, TDM_DMADL,
		     tdm_priv->suspend_dmadl);
	regmap_write(tdm_priv->regmap, TDM_DIV0_LEVEL,
		     tdm_priv->suspend_div0level);

	pm_runtime_put_sync(dev);

	return 0;
}

static const struct zhihe_tdm_soc_drvdata zhihe_tdm_slot0_drvdata = {
	.slot_num = 0,
	.tdm_dai_name = "tdm-slot0-dai",
};

static const struct zhihe_tdm_soc_drvdata zhihe_tdm_slot1_drvdata = {
	.slot_num = 1,
	.tdm_dai_name = "tdm-slot1-dai",
};

static const struct zhihe_tdm_soc_drvdata zhihe_tdm_slot2_drvdata = {
	.slot_num = 2,
	.tdm_dai_name = "tdm-slot2-dai",
};

static const struct zhihe_tdm_soc_drvdata zhihe_tdm_slot3_drvdata = {
	.slot_num = 3,
	.tdm_dai_name = "tdm-slot3-dai",
};

static const struct zhihe_tdm_soc_drvdata zhihe_tdm_slot4_drvdata = {
	.slot_num = 4,
	.tdm_dai_name = "tdm-slot4-dai",
};

static const struct zhihe_tdm_soc_drvdata zhihe_tdm_slot5_drvdata = {
	.slot_num = 5,
	.tdm_dai_name = "tdm-slot5-dai",
};

static const struct zhihe_tdm_soc_drvdata zhihe_tdm_slot6_drvdata = {
	.slot_num = 6,
	.tdm_dai_name = "tdm-slot6-dai",
};

static const struct zhihe_tdm_soc_drvdata zhihe_tdm_slot7_drvdata = {
	.slot_num = 7,
	.tdm_dai_name = "tdm-slot7-dai",
};

static const struct of_device_id zhihe_tdm_of_match[] = {
	{ .compatible = "zhihe,tdm-0", .data = &zhihe_tdm_slot0_drvdata },
	{ .compatible = "zhihe,tdm-1", .data = &zhihe_tdm_slot1_drvdata },
	{ .compatible = "zhihe,tdm-2", .data = &zhihe_tdm_slot2_drvdata },
	{ .compatible = "zhihe,tdm-3", .data = &zhihe_tdm_slot3_drvdata },
	{ .compatible = "zhihe,tdm-4", .data = &zhihe_tdm_slot4_drvdata },
	{ .compatible = "zhihe,tdm-5", .data = &zhihe_tdm_slot5_drvdata },
	{ .compatible = "zhihe,tdm-6", .data = &zhihe_tdm_slot6_drvdata },
	{ .compatible = "zhihe,tdm-7", .data = &zhihe_tdm_slot7_drvdata },
	{},
};
MODULE_DEVICE_TABLE(of, zhihe_tdm_of_match);

static int zhihe_tdm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	struct zhihe_tdm_priv *tdm_priv;
	struct resource *res;
	const uint32_t *iprop;
	int ret;

	if (!dev || !np)
		return -ENODEV;

	tdm_priv = devm_kzalloc(&pdev->dev, sizeof(*tdm_priv), GFP_KERNEL);
	if (!tdm_priv)
		return -ENOMEM;

	tdm_priv->dev = dev;
	tdm_priv->drvdata = of_device_get_match_data(&pdev->dev);

	/* Choose bclk spedge */
	if (of_property_read_bool(np, "spedge-down"))
		tdm_priv->spedge = TDM_SPEDGE_DOWN;
	else if (of_property_read_bool(np, "spedge-up"))
		tdm_priv->spedge = TDM_SPEDGE_UP;
	else
		tdm_priv->spedge = TDM_SPEDGE_DEFAULT;

	iprop = of_get_property(np, "tdm-slots", NULL);
	if (iprop) {
		switch (be32_to_cpup(iprop)) {
		case 2:
		case 4:
		case 6:
		case 8:
			tdm_priv->slots = be32_to_cpup(iprop);
			break;
		default:
			dev_err(dev, "invalid tdm-slots\n");
			return -EINVAL;
		}
	} else
		tdm_priv->slots = ZHIHE_TDM_SLOT_MAX_CNT;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	platform_set_drvdata(pdev, tdm_priv);

	if (is_tdm_probe) {
		pr_info("%s: tdm is already probed\n", __func__);
		tdm_priv->regs = tdm_host_priv->regs;
		tdm_priv->regmap = tdm_host_priv->regmap;
		tdm_priv->irq = tdm_host_priv->irq;
	} else {
		pr_info("%s: tdm probing\n", __func__);
		tdm_host_priv = tdm_priv;

		tdm_priv->regs = devm_ioremap_resource(dev, res);
		if (IS_ERR(tdm_priv->regs))
			return PTR_ERR(tdm_priv->regs);

		tdm_priv->regmap =
			devm_regmap_init_mmio(&pdev->dev, tdm_priv->regs,
					      &zhihe_tdm_regmap_config);
		if (IS_ERR(tdm_priv->regmap)) {
			dev_err(dev,
				"Failed to initialise managed register map\n");
			return PTR_ERR(tdm_priv->regmap);
		}

		/* prepare source clocks */
		tdm_priv->sclk = devm_clk_get(&pdev->dev, "sclk");
		if (IS_ERR(tdm_priv->sclk)) {
			dev_err(&pdev->dev, "Can't retrieve tdm sclk clock\n");
			return PTR_ERR(tdm_priv->sclk);
		}

		/* Request IRQ. */
		tdm_priv->irq = platform_get_irq(pdev, 0);
		if (tdm_priv->irq <= 0)
			return tdm_priv->irq < 0 ? tdm_priv->irq : -ENODEV;

		pm_runtime_enable(&pdev->dev);
	}

	tdm_priv->dma_params_rx.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	tdm_priv->dma_params_rx.maxburst = ZHIHE_TDM_DMA_MAX_BURST;
	tdm_priv->dma_params_rx.addr = res->start + TDM_LDR1 + \
				       tdm_priv->drvdata->slot_num * \
				       ZHIHE_TDM_REG_STRIDE;

	ret = devm_snd_dmaengine_pcm_register(&pdev->dev, NULL, 0);
	if (ret) {
		dev_err(dev, "Could not register PCM\n");
		goto err_pm_disable;
	}

	tdm_priv->dev_dai = devm_kmemdup(&pdev->dev, zhihe_tdm_soc_dai,
					 sizeof(zhihe_tdm_soc_dai),
					 GFP_KERNEL);
	if (!tdm_priv->dev_dai) {
		ret = -ENOMEM;
		goto err_pm_disable;
	}

	tdm_priv->dev_dai[DAI_TDM_SLOT].name =
		tdm_priv->drvdata->tdm_dai_name;

	ret = devm_snd_soc_register_component(&pdev->dev,
					      &zhihe_tdm_soc_component,
					      tdm_priv->dev_dai,
					      ARRAY_SIZE(zhihe_tdm_soc_dai));
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot register snd component\n");
		goto err_pm_disable;
	}

	spin_lock_init(&tdm_priv->zhihe_tdm_lock);

	is_tdm_probe++;

	return ret;

err_pm_disable:
	pm_runtime_disable(&pdev->dev);

	return ret;
}

static int zhihe_tdm_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		zhihe_tdm_runtime_suspend(&pdev->dev);

	is_tdm_probe--;
	is_set_fmt_dai--;

	return 0;
}

static const struct dev_pm_ops zhihe_tdm_pm_ops = {
	SET_RUNTIME_PM_OPS(zhihe_tdm_runtime_suspend,
			   zhihe_tdm_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(zhihe_tdm_suspend, zhihe_tdm_resume)
};

static struct platform_driver zhihe_tdm_driver = {
	.driver = {
		.name		= "zhihe-tdm-audio",
		.pm		= &zhihe_tdm_pm_ops,
		.of_match_table	= zhihe_tdm_of_match,
	},
	.probe	= zhihe_tdm_probe,
	.remove	= zhihe_tdm_remove,
};
module_platform_driver(zhihe_tdm_driver);

MODULE_AUTHOR("Anonymous <anonymous@zhcomputing.com>");
MODULE_DESCRIPTION("Zhihe TDM audio driver");
MODULE_LICENSE("GPL v2");
