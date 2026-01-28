/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Zhihe audio I2S audio support
 *
 * Copyright (C) 2024 Zhihe Computing Technology (Shenzhen) Co., Ltd..
 *
 * Author: Anonymous <anonymous@zhcomputing.com>
 *
 */

#include <linux/dma/dw-axi-dma.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <sound/initval.h>
#include <sound/pcm_params.h>
#include <sound/dmaengine_pcm.h>

#include "zhihe-i2s.h"

#define DRV_NAME "zhihe-i2s"

/**
 * buffer_size default: 23 * 1024 (bit)
 * width min: 8 (bit)
 * sample_rate min: 8000 (hz)
 * time: 10 (us)
 * max poll count = 23Kbit / (8bit * 8000hz) / 10us = 36800
 */
#define MAX_POLL_CNT 40000

static int i2s3_probe_flag = 0;
static void __iomem *i2s3_host_regs = NULL;

static atomic_t rx_device_usage = ATOMIC_INIT(0);

/* 0: master-sd0 && 1: slave-sd1 && 2: slave-sd2 && 3: slave-sd3 */
static struct dma_chan *i2s3_tx_ch[4] = { NULL, NULL, NULL, NULL };

static unsigned int a200_special_sample_rates[] = { 11025, 22050, 44100, 88200 };

static bool is_csr_available(struct device_node *i2s_node, int port_reg, int reg)
{
	struct device_node *remote_node;

	if (!i2s_node)
		return false;

	remote_node = of_graph_get_remote_node(i2s_node, port_reg, reg);
	if (remote_node) {
		of_node_put(remote_node);
		return true;
	}

	return false;
}

static int a200_i2s_set_div(struct zhihe_i2s_priv *i2s_priv, unsigned int rate,
			    unsigned int ratio)
{
	unsigned int div, div0;
	unsigned int i2s_src_clk = 0;
	unsigned int cpr_div = (IIS_SRC_CLK / AUDIO_IIS_SRC0_CLK) - 1;
	int i;

	if (strcmp(i2s_priv->drvdata->name, AP_I2S)) {
		for (i = 0; i < ARRAY_SIZE(a200_special_sample_rates); i++) {
			if (a200_special_sample_rates[i] == rate) {
				i2s_src_clk = 1;
				break;
			}
		}
		/* choose src clk between 48000 and 44100 fsb for div */
		if (i2s_src_clk)
			div = AUDIO_IIS_SRC1_CLK / ratio;	// 44100 fsb
		else
			div = AUDIO_IIS_SRC0_CLK / ratio;	// 48000 fsb
		/* set audio cpr reg for i2s0/1/2 */
		if (strstr(i2s_priv->drvdata->name, I2S0)) {
			if (i2s_src_clk)
				regmap_update_bits(i2s_priv->audio_cpr_regmap,
						   CPR_PERI_CLK_SEL_REG,
						   CPR_I2S0_SRC_SEL_MSK,
						   CPR_I2S0_SRC_SEL(2));
			else
				regmap_update_bits(i2s_priv->audio_cpr_regmap,
						   CPR_PERI_CLK_SEL_REG,
						   CPR_I2S0_SRC_SEL_MSK,
						   CPR_I2S0_SRC_SEL(0));
		} else if (strstr(i2s_priv->drvdata->name, I2S1)) {
			if (i2s_src_clk)
				regmap_update_bits(i2s_priv->audio_cpr_regmap,
						   CPR_PERI_CLK_SEL_REG,
						   CPR_I2S1_SRC_SEL_MSK,
						   CPR_I2S1_SRC_SEL(2));
			else
				regmap_update_bits(i2s_priv->audio_cpr_regmap,
						   CPR_PERI_CLK_SEL_REG,
						   CPR_I2S1_SRC_SEL_MSK,
						   CPR_I2S1_SRC_SEL(0));
		} else if (strstr(i2s_priv->drvdata->name, I2S2)) {
			if (i2s_src_clk)
				regmap_update_bits(i2s_priv->audio_cpr_regmap,
						   CPR_PERI_CLK_SEL_REG,
						   CPR_I2S2_SRC_SEL_MSK,
						   CPR_I2S2_SRC_SEL(2));
			else
				regmap_update_bits(i2s_priv->audio_cpr_regmap,
						   CPR_PERI_CLK_SEL_REG,
						   CPR_I2S2_SRC_SEL_MSK,
						   CPR_I2S2_SRC_SEL(0));
		}
	} else
		div = IIS_SRC_CLK / ratio;

	div0 = (div + div % rate) / rate;
	regmap_write(i2s_priv->regmap, I2S_DIV0_LEVEL, div0);
	if (strcmp(i2s_priv->drvdata->name, AP_I2S))
		regmap_update_bits(i2s_priv->audio_cpr_regmap,
				   CPR_PERI_DIV_SEL_REG, CPR_AUDIO_DIV0_SEL_MSK,
				   CPR_AUDIO_DIV0_SEL(cpr_div));

	return 0;
}

static int a210_i2s_set_div(struct zhihe_i2s_priv *i2s_priv, unsigned int rate,
			    unsigned int ratio)
{
	unsigned int div0, div3;
	unsigned int __maybe_unused sclk;
	unsigned int __maybe_unused mclk = rate * ratio;
	int ret = 0;

	if (ZHIHE_IIS_44100_SRC_CLK % mclk)
		sclk = ZHIHE_IIS_48000_SRC_CLK;
	else
		sclk = ZHIHE_IIS_44100_SRC_CLK;

	ret = clk_set_rate(i2s_priv->sclk, sclk);
	if (ret) {
		dev_err(i2s_priv->dev, "Fail to set sclk %d\n", ret);
		return ret;
	}
	div0 = DIV_ROUND_CLOSEST(sclk, mclk);
	div3 = DIV3_DEFAULT;
	regmap_write(i2s_priv->regmap, I2S_DIV0_LEVEL, div0);
	regmap_write(i2s_priv->regmap, I2S_DIV3_LEVEL, div3);

	return ret;
}

static bool tx_fifo_empty(void __iomem *addr, unsigned int channels)
{
	unsigned int tx_fifo_mty_msk = 0, tx_fifo_mty = 0;
	unsigned int status = readl(addr + I2S_SR);

	switch (channels) {
	case 8:
		tx_fifo_mty_msk |= SR_TFE3_Msk;
		tx_fifo_mty |= SR_TFE3_TX_FIFO_EMPTY;
		fallthrough;
	case 6:
		tx_fifo_mty_msk |= SR_TFE2_Msk;
		tx_fifo_mty |= SR_TFE2_TX_FIFO_EMPTY;
		fallthrough;
	case 4:
		tx_fifo_mty_msk |= SR_TFE1_Msk;
		tx_fifo_mty |= SR_TFE1_TX_FIFO_EMPTY;
		fallthrough;
	case 2:
		tx_fifo_mty_msk |= SR_TFE0_Msk;
		tx_fifo_mty |= SR_TFE0_TX_FIFO_EMPTY;
		break;
	default:
		tx_fifo_mty_msk |= SR_TFE0_Msk;
		tx_fifo_mty |= SR_TFE0_TX_FIFO_EMPTY;
		break;
	}

	return ((status & tx_fifo_mty_msk) == tx_fifo_mty) &&
	       !(status & SR_TXBUSY_STATUS);
}

static int i2s_multi_channel_sync(void __iomem *addr, unsigned int channels,
				  bool master, bool tx, bool on)
{
	if (on)
		return 0;

	if (!addr)
		return -EINVAL;
	
	int i = MAX_POLL_CNT;
	
	do {
		if (tx && tx_fifo_empty(addr, channels))
			break;
		else if (!tx) {
			if (!master)
				break;
			else if (atomic_read(&rx_device_usage) == 0) {
				udelay(100);
				break;
			}
		}
		udelay(10);
	} while (i--);

	return 0;
}

static int zhihe_snd_ctrl(struct zhihe_i2s_priv *i2s_priv, bool tx, bool on,
			  int (*cb)(struct dma_chan *))
{
	unsigned int dma_en;
	unsigned long flags;
	int i;

	if (strstr(i2s_priv->drvdata->name, I2S3)) {
		if (!tx && on)
			atomic_inc(&rx_device_usage);
		else if (!tx && !on)
			atomic_dec(&rx_device_usage);
		if (!i2s_priv->regmap)
			return i2s_multi_channel_sync(i2s3_host_regs, i2s_priv->ch_cnt, false, tx, on);
	}

	spin_lock_irqsave(&i2s_priv->zhihe_i2s_lock, flags);

	/* get current dma status, save tx/rx configuration. */
	regmap_read(i2s_priv->regmap, I2S_DMACR, &dma_en);
	if (on) {
		dma_en |= tx ? DMACR_TDMAE_EN : DMACR_RDMAE_EN;
		regmap_update_bits(i2s_priv->regmap, I2S_FUNCMODE,
				   FUNCMODE_TMODE_WEN_Msk | FUNCMODE_TMODE_Msk |
				   FUNCMODE_RMODE_WEN_Msk | FUNCMODE_RMODE_Msk,
				   FUNCMODE_TMODE_WEN | FUNCMODE_TMODE |
				   FUNCMODE_RMODE_WEN | FUNCMODE_RMODE);
		regmap_write(i2s_priv->regmap, I2S_DMACR, dma_en);
		regmap_write(i2s_priv->regmap, I2S_IISEN, IISEN_I2SEN);
	} else {
		dma_en &= tx ? ~DMACR_TDMAE_EN : ~DMACR_RDMAE_EN;
		i2s_multi_channel_sync(i2s_priv->regs, i2s_priv->ch_cnt, true, tx, on);
		regmap_write(i2s_priv->regmap, I2S_DMACR, dma_en);


		/* work around to deal with tx channel sync */
		if (tx) {
			for (i = 1; i < i2s_priv->ch_cnt / 2; i++) {
				if (i2s3_tx_ch[i])
					cb(i2s3_tx_ch[i]);
			}
		}

		/**
		 * The enablement of I2S can onlybe truned off when
		 * the DMA configuration for RX and TX is completely disabled.
		 */
		if (!((DMACR_TDMAE_MSK | DMACR_RDMAE_MSK) & dma_en)) {
			regmap_write(i2s_priv->regmap, I2S_IISEN, 0);
			regmap_update_bits(i2s_priv->regmap, I2S_FUNCMODE,
					   FUNCMODE_TMODE_WEN_Msk |
					   FUNCMODE_TMODE_Msk |
					   FUNCMODE_RMODE_WEN_Msk |
					   FUNCMODE_RMODE_Msk,
					   FUNCMODE_TMODE_WEN |
					   FUNCMODE_RMODE_WEN);
		}
	}

	spin_unlock_irqrestore(&i2s_priv->zhihe_i2s_lock, flags);

	return 0;
}

/**
 * zhihe_i2s_dai_trigger: start and stop the DMA transfer.
 *
 * This function is called by ALSA to start, stop, pause, and resume the DMA
 * transfer of data.
 */
static int zhihe_i2s_dai_trigger(struct snd_pcm_substream *substream, int cmd,
				 struct snd_soc_dai *dai)
{
	struct zhihe_i2s_priv *i2s_priv = snd_soc_dai_get_drvdata(dai);
	bool tx = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	int (*cb)(struct dma_chan *chan);
	int ret;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		ret = zhihe_snd_ctrl(i2s_priv, tx, true, NULL);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		cb = dmaengine_terminate_async;
		ret = zhihe_snd_ctrl(i2s_priv, tx, false, cb);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		/* work around for DMAC stop issue. */
		dmaengine_pause(snd_dmaengine_pcm_get_chan(substream));
		ret = zhihe_snd_ctrl(i2s_priv, tx, false, NULL);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static int zhihe_i2s_set_fmt_dai(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct zhihe_i2s_priv *i2s_priv = snd_soc_dai_get_drvdata(dai);
	unsigned int cnfout = 0, cnfin = 0;
	int ret;

	if (strstr(i2s_priv->drvdata->name, I2S3) && !i2s_priv->regmap)
		return 0;

	ret = pm_runtime_resume_and_get(i2s_priv->dev);
	if (ret < 0) {
		dev_err(i2s_priv->dev, "Fail to resume device %d\n", ret);
		return ret;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		cnfout |= CNFOUT_TSAFS_I2S;
		cnfin |= CNFIN_RSAFS_I2S;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		cnfout |= CNFOUT_TSAFS_RJ;
		cnfin |= CNFIN_RSAFS_RJ;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		cnfout |= CNFOUT_TSAFS_LJ;
		cnfin |= CNFIN_RSAFS_LJ;
		break;
	/* dsp-a need delay 1 bclk, register-map no delay setting */
	case SND_SOC_DAIFMT_DSP_B:
		cnfout |= CNFOUT_TSAFS_PCM;
		cnfin |= CNFIN_RSAFS_PCM;
		break;
	default:
		ret = -EINVAL;
		goto err_pm_put;
	}

	/* CPU CLK in FUNC "snd_soc_daifmt_clock_provider_flipped" is flipped */
	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_BP_FP:
		/* CPU MASTER */
		cnfout &= ~CNFOUT_I2S_TXMODE_SLAVE;
		cnfin |= CNFIN_I2S_RXMODE_MASTER_MODE;
		break;
	case SND_SOC_DAIFMT_BC_FC:
		/* CPU SLAVE */
		cnfout |= CNFOUT_I2S_TXMODE_SLAVE;
		cnfin &= ~CNFIN_I2S_RXMODE_MASTER_MODE;
		break;
	default:
		ret = -EINVAL;
		goto err_pm_put;
	}

	/* Active level of left/right channel */
	if (i2s_priv->alolrc_high) {
		cnfout |= CNFOUT_TALOLRC_HIGHFORLEFT;
		cnfin |= CNFIN_RALOLRC_HIGHFORLEFT;
	}

	regmap_update_bits(i2s_priv->regmap, I2S_IISCNF_OUT, CNFOUT_TSAFS_MSK |
			   CNFOUT_TALOLRC_Msk | CNFOUT_I2S_TXMODE_Msk, cnfout);
	regmap_update_bits(i2s_priv->regmap, I2S_IISCNF_IN, CNFIN_RSAFS_Msk |
			   CNFIN_RALOLRC_Msk | CNFIN_I2S_RXMODE_Msk, cnfin);

err_pm_put:
	pm_runtime_put_sync(i2s_priv->dev);

	return ret;
}

static int zhihe_i2s_dai_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params,
				   struct snd_soc_dai *dai)
{
	struct zhihe_i2s_priv *i2s_priv = snd_soc_dai_get_drvdata(dai);
	unsigned int channels = params_channels(params);
	unsigned int format = params_format(params);
	unsigned int rate = params_rate(params);
	unsigned int fssta = 0, iiscnf_out = 0, iiscnf_in = 0;
	unsigned int ratio = IIS_MCLK_SEL_256;
	unsigned int funcmode;
	int ret = 0;

	if (strstr(i2s_priv->drvdata->name, I2S3) && !i2s_priv->regmap)
		return 0;

	switch (format) {
	case SNDRV_PCM_FORMAT_S8:
		fssta |= I2S_DATA_8BIT_WIDTH_32BIT;
		fssta |= FSSTA_SCLK_SEL_64;
		fssta |= FSSTA_MCLK_SEL_256;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		fssta |= I2S_DATA_WIDTH_16BIT;
		fssta |= FSSTA_SCLK_SEL_64;	// in multi-channel mode, HW-codec require sclk at 64fs
		fssta |= FSSTA_MCLK_SEL_256;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		fssta |= I2S_DATA_24BIT_WIDTH_32BIT;
		fssta |= FSSTA_SCLK_SEL_64;
		fssta |= FSSTA_MCLK_SEL_256;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
	case SNDRV_PCM_FORMAT_FLOAT_LE:
		fssta |= I2S_DATA_WIDTH_32BIT;
		fssta |= FSSTA_SCLK_SEL_64;
		fssta |= FSSTA_MCLK_SEL_256;
		break;
	default:
		dev_err(i2s_priv->dev, "Unknown data format: %u\n", format);
		return -EINVAL;
	}

	regmap_update_bits(i2s_priv->regmap, I2S_FSSTA, FSSTA_DATAWTH_Msk |
			   FSSTA_SCLK_SEL_Msk | FSSTA_MCLK_SEL_Msk, fssta);

	regmap_read(i2s_priv->regmap, I2S_FUNCMODE, &funcmode);

	funcmode &= ~FUNCMODE_CH0_ENABLE;
	funcmode &= ~FUNCMODE_CH1_ENABLE;
	funcmode &= ~FUNCMODE_CH2_ENABLE;
	funcmode &= ~FUNCMODE_CH3_ENABLE;
	switch (i2s_priv->ch_cnt) {
	case 8:
		funcmode |= FUNCMODE_CH3_ENABLE;
		fallthrough;
	case 6:
		funcmode |= FUNCMODE_CH2_ENABLE;
		fallthrough;
	case 4:
		funcmode |= FUNCMODE_CH1_ENABLE;
		fallthrough;
	case 2:
		funcmode |= FUNCMODE_CH0_ENABLE;
		break;
	default:
		dev_err(i2s_priv->dev, "Invalid channel count: %u\n", i2s_priv->ch_cnt);
		return -EINVAL;
	}
	regmap_write(i2s_priv->regmap, I2S_FUNCMODE, funcmode);

	if (channels == MONO_SOURCE) {
		iiscnf_out |= CNFOUT_TX_VOICE_EN_MONO;
		/* CNFIN_RX_CH_SEL_LEFT only use in mono mode. */
		if (i2s_priv->rx_ch_left)
			iiscnf_in |= CNFIN_RX_CH_SEL_LEFT;
		iiscnf_in |= CNFIN_RVOICEEN_MONO;
	} else {
		iiscnf_out &= ~CNFOUT_TX_VOICE_EN_MONO;
		iiscnf_in &= ~CNFIN_RVOICEEN_MONO;
	}
	regmap_update_bits(i2s_priv->regmap, I2S_IISCNF_OUT,
			   CNFOUT_TX_VOICE_EN_Msk, iiscnf_out);
	regmap_update_bits(i2s_priv->regmap, I2S_IISCNF_IN,
			   CNFIN_RX_CH_SEL_Msk | CNFIN_RVOICEEN_Msk, iiscnf_in);

	if (i2s_priv->board == ZHIHE_A210)
		ret = a210_i2s_set_div(i2s_priv, rate, ratio);
	else if (i2s_priv->board == ZHIHE_A200)
		ret = a200_i2s_set_div(i2s_priv, rate, ratio);

	return ret;
}

static int zhihe_hdmi_dai_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params,
				    struct snd_soc_dai *dai)
{
	struct zhihe_i2s_priv *i2s_priv = snd_soc_dai_get_drvdata(dai);
	unsigned int channels = params_channels(params);
	unsigned int format = params_format(params);
	unsigned int rate = params_rate(params);
	unsigned int fssta = 0, iiscnf_out = 0;
	unsigned int ratio = IIS_MCLK_SEL_256;
	unsigned int funcmode;
	int ret = 0;

	if (strstr(i2s_priv->drvdata->name, I2S3) && !i2s_priv->regmap)
		return 0;

	switch (format) {
	case SNDRV_PCM_FORMAT_S16_LE:
		fssta |= I2S_DATA_WIDTH_16BIT;
		fssta |= FSSTA_MCLK_SEL_256;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		fssta |= I2S_DATA_WIDTH_24BIT;
		fssta |= FSSTA_MCLK_SEL_256;
		break;
	default:
		dev_err(i2s_priv->dev, "Unknown data format: %d\n", format);
		return -EINVAL;
	}

	/* HDMI audio interface operates with a SCLK at 64fs. */
	fssta |= FSSTA_SCLK_SEL_64;

	regmap_update_bits(i2s_priv->regmap, I2S_FSSTA, FSSTA_DATAWTH_Msk |
			   FSSTA_SCLK_SEL_Msk | FSSTA_MCLK_SEL_Msk, fssta);

	/* HDMI audio only use channel 0. */
	regmap_read(i2s_priv->regmap, I2S_FUNCMODE, &funcmode);
	funcmode &= ~FUNCMODE_CH0_ENABLE;
	funcmode &= ~FUNCMODE_CH1_ENABLE;
	funcmode &= ~FUNCMODE_CH2_ENABLE;
	funcmode &= ~FUNCMODE_CH3_ENABLE;
	funcmode |= FUNCMODE_CH0_ENABLE;
	regmap_write(i2s_priv->regmap, I2S_FUNCMODE, funcmode);

	if (channels == MONO_SOURCE)
		iiscnf_out |= CNFOUT_TX_VOICE_EN_MONO;
	else
		iiscnf_out &= ~CNFOUT_TX_VOICE_EN_MONO;
	regmap_update_bits(i2s_priv->regmap, I2S_IISCNF_OUT,
			   CNFOUT_TX_VOICE_EN_Msk, iiscnf_out);

	if (i2s_priv->board == ZHIHE_A210)
		ret = a210_i2s_set_div(i2s_priv, rate, ratio);
	else if (i2s_priv->board == ZHIHE_A200)
		ret = a200_i2s_set_div(i2s_priv, rate, ratio);

	return ret;
}

static int zhihe_i2s_dai_startup(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	struct zhihe_i2s_priv *i2s_priv = snd_soc_dai_get_drvdata(dai);
	struct dma_chan *chan = snd_dmaengine_pcm_get_chan(substream);
	bool tx = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;

	if (!strcmp(i2s_priv->drvdata->name, "i2s3-8ch-sd0")) {
		if (tx)
			i2s3_tx_ch[0] = chan;
	} else if (!strcmp(i2s_priv->drvdata->name, "i2s3-8ch-sd1")) {
		if (tx)
			i2s3_tx_ch[1] = chan;
	} else if (!strcmp(i2s_priv->drvdata->name, "i2s3-8ch-sd2")) {
		if (tx)
			i2s3_tx_ch[2] = chan;
	} else if (!strcmp(i2s_priv->drvdata->name, "i2s3-8ch-sd3")) {
		if (tx)
			i2s3_tx_ch[3] = chan;
	}

	return 0;
}

static void zhihe_i2s_dai_shutdown(struct snd_pcm_substream *substream,
				   struct snd_soc_dai *dai)
{
	struct zhihe_i2s_priv *i2s_priv = snd_soc_dai_get_drvdata(dai);
	bool tx = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;

	if (!strcmp(i2s_priv->drvdata->name, "i2s3-8ch-sd0")) {
		if (tx)
			i2s3_tx_ch[0] = NULL;
	} else if (!strcmp(i2s_priv->drvdata->name, "i2s3-8ch-sd1")) {
		if (tx)
			i2s3_tx_ch[1] = NULL;
	} else if (!strcmp(i2s_priv->drvdata->name, "i2s3-8ch-sd2")) {
		if (tx)
			i2s3_tx_ch[2] = NULL;
	} else if (!strcmp(i2s_priv->drvdata->name, "i2s3-8ch-sd3")) {
		if (tx)
			i2s3_tx_ch[3] = NULL;
	}

	if (i2s_priv->regmap)
		regmap_write(i2s_priv->regmap, I2S_ICR, 0xffffffff);
}

static int zhihe_i2s_dai_probe(struct snd_soc_dai *dai)
{
	struct zhihe_i2s_priv *i2s_priv = snd_soc_dai_get_drvdata(dai);

	snd_soc_dai_init_dma_data(dai, &i2s_priv->dma_params_tx,
				  &i2s_priv->dma_params_rx);

	return 0;
}

static int zhihe_hdmi_dai_probe(struct snd_soc_dai *dai)
{
	struct zhihe_i2s_priv *i2s_priv = snd_soc_dai_get_drvdata(dai);

	if (i2s_priv->board == ZHIHE_A210) {
		u32 val;
		if (!i2s_priv->sys_csr) {
			dev_err(i2s_priv->dev,
				"sys_csr is null, can't init hdmi dai\n");
			return -EINVAL;
		}

		val = readl(i2s_priv->sys_csr + SYS_CSR_OFFSET);
		if (i2s_priv->hdmi_connected)
			val |= HDMI_AUDIO_EN;
		writel(val, i2s_priv->sys_csr + SYS_CSR_OFFSET);
	}

	snd_soc_dai_init_dma_data(dai, &i2s_priv->dma_params_tx,
				  &i2s_priv->dma_params_rx);

	return 0;
}

static const struct snd_soc_dai_ops zhihe_i2s_dai_ops = {
	.probe		= zhihe_i2s_dai_probe,
	.startup	= zhihe_i2s_dai_startup,
	.shutdown	= zhihe_i2s_dai_shutdown,
	.trigger	= zhihe_i2s_dai_trigger,
	.set_fmt	= zhihe_i2s_set_fmt_dai,
	.hw_params	= zhihe_i2s_dai_hw_params,
};

static const struct snd_soc_dai_ops zhihe_hdmi_dai_ops = {
	.probe		= zhihe_hdmi_dai_probe,
	.trigger	= zhihe_i2s_dai_trigger,
	.set_fmt	= zhihe_i2s_set_fmt_dai,
	.hw_params	= zhihe_hdmi_dai_hw_params,
};

static struct snd_soc_dai_driver zhihe_i2s_soc_dai[] = {
	/* i2s dai. */
	{
		.playback = {
			.rates		= ZHIHE_I2S_RATES,
			.formats	= ZHIHE_I2S_FMTS,
			.channels_min	= 1,
			.channels_max	= 2,
		},
		.capture = {
			.rates		= ZHIHE_I2S_RATES,
			.formats	= ZHIHE_I2S_FMTS,
			.channels_min	= 1,
			.channels_max	= 2,
		},
		.ops = &zhihe_i2s_dai_ops,
		.symmetric_rate = 1,
		.symmetric_sample_bits = 1,
	},
	/* i2s hdmi dai. */
	{
		.playback = {
			.rates		= ZHIHE_I2S_RATES,
			.formats	= ZHIHE_I2S_HDMI_FMTS,
			.channels_min	= 1,
			.channels_max	= 2,
		},
		.ops = &zhihe_hdmi_dai_ops,
	},
};

static const struct snd_soc_component_driver zhihe_i2s_soc_component = {
	.name = DRV_NAME,
};

static bool zhihe_i2s_read_and_write_regs(unsigned int reg)
{
	switch (reg) {
	case I2S_IISEN:
	case I2S_FUNCMODE:
	case I2S_IISCNF_IN:
	case I2S_FSSTA:
	case I2S_IISCNF_OUT:
	case I2S_FADTLR:
	case I2S_SCCR:
	case I2S_TXFTLR:
	case I2S_RXFTLR:
	case I2S_IMR:
	case I2S_DMACR:
	case I2S_DMATDLR:
	case I2S_DMARDLR:
	case I2S_DR0:
	case I2S_DIV0_LEVEL:
	case I2S_DIV3_LEVEL:
	case I2S_DR1:
	case I2S_DR2:
	case I2S_DR3:
		return true;
	default:
		return false;
	}
}

static bool zhihe_i2s_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case I2S_ICR:
		return true;
	default:
		return zhihe_i2s_read_and_write_regs(reg);
	}
}

static bool zhihe_i2s_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case I2S_TXFLR:
	case I2S_RXFLR:
	case I2S_SR:
	case I2S_ISR:
	case I2S_RISR:
		return true;
	default:
		return zhihe_i2s_read_and_write_regs(reg);
	}
}

static bool zhihe_i2s_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case I2S_FUNCMODE:
	case I2S_FSSTA:
	case I2S_TXFLR:
	case I2S_RXFLR:
	case I2S_SR:
	case I2S_ISR:
	case I2S_RISR:
	case I2S_ICR:
	case I2S_DR0:
	case I2S_DR1:
	case I2S_DR2:
	case I2S_DR3:
		return true;
	default:
		return false;
	}
}

static const struct reg_default zhihe_i2s_reg_defaults[] = {
	{ I2S_FUNCMODE,	0x00000100 },
	{ I2S_TXFTLR,	0x00000010 },
	{ I2S_RXFTLR,	0x00000010 },
	{ I2S_IMR,	0x001fffff },
	{ I2S_DMATDLR,	0x00000010 },
};

static const struct regmap_config zhihe_i2s_regmap_config = {
	.reg_bits		= ZHIHE_I2S_BIT_WIDTH,
	.val_bits		= ZHIHE_I2S_BIT_WIDTH,
	.reg_stride		= ZHIHE_I2S_REG_STRIDE,
	.max_register		= I2S_DR3,
	.reg_defaults		= zhihe_i2s_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(zhihe_i2s_reg_defaults),
	.writeable_reg		= zhihe_i2s_writeable_reg,
	.readable_reg		= zhihe_i2s_readable_reg,
	.volatile_reg		= zhihe_i2s_volatile_reg,
	.cache_type		= REGCACHE_FLAT,
};

static int __maybe_unused zhihe_i2s_rt_suspend(struct device *dev)
{
	struct zhihe_i2s_priv *i2s_priv = dev_get_drvdata(dev);

	if (strstr(i2s_priv->drvdata->name, I2S3) && !i2s_priv->regmap)
		return 0;

	regcache_cache_only(i2s_priv->regmap, true);
	if (i2s_priv->board == ZHIHE_A210)
		clk_disable_unprepare(i2s_priv->sclk);

	return 0;
}

static int __maybe_unused zhihe_i2s_rt_resume(struct device *dev)
{
	struct zhihe_i2s_priv *i2s_priv = dev_get_drvdata(dev);
	int ret;

	if (strstr(i2s_priv->drvdata->name, I2S3) && !i2s_priv->regmap)
		return 0;

	if (i2s_priv->board == ZHIHE_A210) {
		ret = clk_prepare_enable(i2s_priv->sclk);
		if (ret) {
			dev_err(i2s_priv->dev, "clock enable failed %d\n", ret);
			return ret;
		}
	}

	regcache_cache_only(i2s_priv->regmap, false);
	regcache_mark_dirty(i2s_priv->regmap);

	ret = regcache_sync(i2s_priv->regmap);
	if (i2s_priv->board == ZHIHE_A210 && ret)
		clk_disable_unprepare(i2s_priv->sclk);

	return ret;
}

static int __maybe_unused zhihe_i2s_suspend(struct device *dev)
{
	struct zhihe_i2s_priv *i2s_priv = dev_get_drvdata(dev);

	if (strstr(i2s_priv->drvdata->name, I2S3) && !i2s_priv->regmap)
		return 0;

	regcache_mark_dirty(i2s_priv->regmap);
	if (i2s_priv->board == ZHIHE_A200) {
		if (strcmp(i2s_priv->drvdata->name, AP_I2S))
			regcache_mark_dirty(i2s_priv->audio_cpr_regmap);
	}

	return 0;
}

static int __maybe_unused zhihe_i2s_resume(struct device *dev)
{
	struct zhihe_i2s_priv *i2s_priv = dev_get_drvdata(dev);
	int ret;

	if (strstr(i2s_priv->drvdata->name, I2S3) && !i2s_priv->regmap)
		return 0;

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return ret;

	ret = regcache_sync(i2s_priv->regmap);
	if (i2s_priv->board == ZHIHE_A200) {
		if (strcmp(i2s_priv->drvdata->name, AP_I2S))
			ret |= regcache_sync(i2s_priv->audio_cpr_regmap);
	}
	pm_runtime_put_sync(dev);

	return ret;
}

static const struct zhihe_i2s_soc_drvdata zhihe_i2s0_data = {
	.name		= "i2s0",
	.i2s_dai_name	= "pcm-i2s0-dai",
	.hdmi_dai_name	= "pcm-i2s0-hdmi-dai",
	.tx_dr		= I2S_DR0,
	.rx_dr		= I2S_DR0,
};

static const struct zhihe_i2s_soc_drvdata zhihe_i2s1_data = {
	.name		= "i2s1",
	.i2s_dai_name	= "pcm-i2s1-dai",
	.hdmi_dai_name	= "pcm-i2s1-hdmi-dai",
	.tx_dr		= I2S_DR0,
	.rx_dr		= I2S_DR0,
};

static const struct zhihe_i2s_soc_drvdata zhihe_i2s2_data = {
	.name		= "i2s2",
	.i2s_dai_name	= "pcm-i2s2-dai",
	.hdmi_dai_name	= "pcm-i2s2-hdmi-dai",
	.tx_dr		= I2S_DR0,
	.rx_dr		= I2S_DR0,
};

static const struct zhihe_i2s_soc_drvdata zhihe_i2s3_8ch_sd0_data = {
	.name		= "i2s3-8ch-sd0",
	.i2s_dai_name	= "pcm-i2s3-8ch-sd0-dai",
	.hdmi_dai_name	= "pcm-i2s3-8ch-sd0-hdmi-dai",
	.tx_dr		= I2S_DR0,
	.rx_dr		= I2S_DR0,
};

static const struct zhihe_i2s_soc_drvdata zhihe_i2s3_8ch_sd1_data = {
	.name		= "i2s3-8ch-sd1",
	.i2s_dai_name	= "pcm-i2s3-8ch-sd1-dai",
	.hdmi_dai_name	= "pcm-i2s3-8ch-sd1-hdmi-dai",
	.tx_dr		= I2S_DR1,
	.rx_dr		= I2S_DR1,
};

static const struct zhihe_i2s_soc_drvdata zhihe_i2s3_8ch_sd2_data = {
	.name		= "i2s3-8ch-sd2",
	.i2s_dai_name	= "pcm-i2s3-8ch-sd2-dai",
	.hdmi_dai_name	= "pcm-i2s3-8ch-sd2-hdmi-dai",
	.tx_dr		= I2S_DR2,
	.rx_dr		= I2S_DR2,
};

static const struct zhihe_i2s_soc_drvdata zhihe_i2s3_8ch_sd3_data = {
	.name		= "i2s3-8ch-sd3",
	.i2s_dai_name	= "pcm-i2s3-8ch-sd3-dai",
	.hdmi_dai_name	= "pcm-i2s3-8ch-sd3-hdmi-dai",
	.tx_dr		= I2S_DR3,
	.rx_dr		= I2S_DR3,
};

static const struct zhihe_i2s_soc_drvdata zhihe_ap_i2s_data = {
	.name		= AP_I2S,
	.i2s_dai_name	= "pcm-ap-i2s-dai",
	.hdmi_dai_name	= "pcm-ap-hdmi-dai",
	.tx_dr		= I2S_DR0,
	.rx_dr		= I2S_DR0,
};

static const struct of_device_id zhihe_i2s_of_match[] = {
	/* a210: i2s-2ch */
	{ .compatible = "zhihe,i2s0", .data = &zhihe_i2s0_data },
	{ .compatible = "zhihe,i2s1", .data = &zhihe_i2s1_data },
	{ .compatible = "zhihe,i2s2", .data = &zhihe_i2s2_data },
	/* a210: i2s3-8ch-sdX */
	{ .compatible = "zhihe,i2s3-8ch-sd0", .data = &zhihe_i2s3_8ch_sd0_data },
	{ .compatible = "zhihe,i2s3-8ch-sd1", .data = &zhihe_i2s3_8ch_sd1_data },
	{ .compatible = "zhihe,i2s3-8ch-sd2", .data = &zhihe_i2s3_8ch_sd2_data },
	{ .compatible = "zhihe,i2s3-8ch-sd3", .data = &zhihe_i2s3_8ch_sd3_data },
	/* a200: i2s-2ch */
	{ .compatible = "xuantie,th1520-i2s", .data = NULL},
	{},
};
MODULE_DEVICE_TABLE(of, zhihe_i2s_of_match);

static ssize_t zhihe_i2s_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct zhihe_i2s_priv *i2s_priv = dev_get_drvdata(dev);
	unsigned int value, reg;

	for (reg = I2S_IISEN; reg <= I2S_DR3; reg += ZHIHE_I2S_REG_STRIDE) {
		regmap_read(i2s_priv->regmap, reg, &value);
		printk("i2s reg[0x%x]=0x%x\n", reg, value);
	}

	if (strcmp(i2s_priv->drvdata->name, AP_I2S)) {
		for (reg = 0; reg <= 0xfc; reg += 0x4) {
			regmap_read(i2s_priv->audio_cpr_regmap, reg, &value);
			printk("cpr reg[0x%x]=0x%x\n", reg, value);
		}
	}

	return 0;
}

static DEVICE_ATTR(registers, 0644, zhihe_i2s_show, NULL);

static struct attribute *zhihe_i2s_debug_attrs[] = {
	&dev_attr_registers.attr,
	NULL,
};

static struct attribute_group zhihe_i2s_debug_attr_group = {
	.name	= "zhihe_i2s_debug",
	.attrs	= zhihe_i2s_debug_attrs,
};

static int zhihe_i2s_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct zhihe_i2s_priv *i2s_priv;
	struct snd_soc_dai_driver *dai;
	struct resource *res;
	struct axi_dma_peripheral_config *dma_pcfg;
	unsigned int val;
	int ret;

	i2s_priv = devm_kzalloc(&pdev->dev, sizeof(*i2s_priv), GFP_KERNEL);
	if (!i2s_priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, i2s_priv);

	spin_lock_init(&i2s_priv->zhihe_i2s_lock);
	i2s_priv->dev = &pdev->dev;
	i2s_priv->drvdata = of_device_get_match_data(&pdev->dev);

	/* Choose board by dts. */
	if (of_property_read_bool(np, "snd-soc-zhihe-a210"))
		i2s_priv->board = ZHIHE_A210;
	else if (of_property_read_bool(np, "snd-soc-zhihe-a200"))
		i2s_priv->board = ZHIHE_A200;
	else
		i2s_priv->board = ZHIHE_BOARD_DEFAULT;

	if (i2s_priv->board == ZHIHE_A200) {
		if (strstr(pdev->name, AP_I2S))
			i2s_priv->drvdata = &zhihe_ap_i2s_data;
		else if (strstr(pdev->name, AUDIO_I2S0))
			i2s_priv->drvdata = &zhihe_i2s0_data;	// use zhihe i2s0 data
		else if (strstr(pdev->name, AUDIO_I2S1))
			i2s_priv->drvdata = &zhihe_i2s1_data;	// use zhihe i2s1 data
		else if (strstr(pdev->name, AUDIO_I2S2))
			i2s_priv->drvdata = &zhihe_i2s2_data;	// use zhihe i2s2 data
		else {
			dev_err(&pdev->dev,
				"unsupport audio dev name: %s\n", pdev->name);
			return -EINVAL;
		}
	}

	/* Choose active level of left/right channel. */
	if (of_property_read_bool(np, "alolrc-high"))
		i2s_priv->alolrc_high = true;

	/* RX channel choose left channel in mono mode. */
	if (of_property_read_bool(np, "rx-ch-left"))
		i2s_priv->rx_ch_left = true;

	/* multi-channels support */
	if (!of_property_read_u32(np, "multi-channels", &val))
		if (val == 2 || val == 4 || val == 6 || val == 8)
			i2s_priv->ch_cnt = val;
		else {
			dev_err(i2s_priv->dev, "Invalid channel count: %u\n", val);
			return -EINVAL;
		}
	else
		i2s_priv->ch_cnt = 2;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (strstr(i2s_priv->drvdata->name, I2S3) && strcmp(i2s_priv->drvdata->name, "i2s3-8ch-sd0")) {
		i2s_priv->regs = NULL;
		i2s_priv->regmap = NULL;
	} else {
		i2s_priv->regs = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(i2s_priv->regs))
			return PTR_ERR(i2s_priv->regs);

		i2s_priv->regmap = devm_regmap_init_mmio(&pdev->dev, i2s_priv->regs,
							 &zhihe_i2s_regmap_config);
		if (IS_ERR(i2s_priv->regmap)) {
			dev_err(&pdev->dev,
				"Failed to initialise managed register map\n");
			return PTR_ERR(i2s_priv->regmap);
		}

		if (i2s_priv->board == ZHIHE_A200) {
			/* set audio-cpr regmap without ap_i2s */
			if (strcmp(i2s_priv->drvdata->name, AP_I2S)) {
				i2s_priv->audio_cpr_regmap =
					syscon_regmap_lookup_by_phandle(np, "audio-cpr-regmap");
				if (IS_ERR(i2s_priv->audio_cpr_regmap)) {
					dev_err(&pdev->dev,
						"cannot find regmap for audio cpr register\n");
					return PTR_ERR(i2s_priv->audio_cpr_regmap);
				}
				regmap_update_bits(i2s_priv->audio_cpr_regmap,
						   CPR_PERI_DIV_SEL_REG,
						   CPR_AUDIO_DIV1_SEL_MSK,
						   CPR_AUDIO_DIV1_SEL(5));
				/* enable i2s sync */
				regmap_update_bits(i2s_priv->audio_cpr_regmap,
						   CPR_PERI_CTRL_REG,
						   CPR_I2S_SYNC_MSK,
						   CPR_I2S_SYNC_EN);
			} else
				i2s_priv->audio_cpr_regmap = NULL;

			/* set reset */
			i2s_priv->rst = devm_reset_control_get_optional_shared(&pdev->dev, NULL);
			if (IS_ERR(i2s_priv->rst))
				return PTR_ERR(i2s_priv->rst);
			reset_control_deassert(i2s_priv->rst);
		}

		/* prepare APB clocks */
		i2s_priv->pclk = devm_clk_get(&pdev->dev, "pclk");
		if (IS_ERR(i2s_priv->pclk)) {
			dev_err(&pdev->dev, "Can't retrieve i2s bus clock\n");
			return PTR_ERR(i2s_priv->pclk);
		}
		ret = clk_prepare_enable(i2s_priv->pclk);
		if (ret) {
			dev_err(&pdev->dev, "pclk enable failed %d\n", ret);
			return ret;
		}

		if (i2s_priv->board == ZHIHE_A210) {
			/* prepare source clocks */
			i2s_priv->sclk = devm_clk_get(&pdev->dev, "sclk");
			if (IS_ERR(i2s_priv->sclk)) {
				dev_err(&pdev->dev, "Can't retrieve i2s sclk clock\n");
				ret = PTR_ERR(i2s_priv->sclk);
				goto err_clk;
			}
		}

		/* Request IRQ. */
		i2s_priv->irq = platform_get_irq(pdev, 0);
		if (i2s_priv->irq <= 0) {
			ret = i2s_priv->irq < 0 ? i2s_priv->irq : -ENODEV;
			goto err_clk;
		}

		pm_runtime_enable(&pdev->dev);
		if (i2s_priv->board == ZHIHE_A210) {
			if (!pm_runtime_enabled(&pdev->dev)) {
				ret = zhihe_i2s_rt_resume(&pdev->dev);
				if (ret)
					goto err_pm_disable;
			}
		} else if (i2s_priv->board == ZHIHE_A200) {
			/* clk gate is enabled by hardware as default register value */
			pm_runtime_resume_and_get(&pdev->dev);
			pm_runtime_put_sync(&pdev->dev);

			/* create sysfs interface */
			ret = sysfs_create_group(&pdev->dev.kobj,
						&zhihe_i2s_debug_attr_group);
			if (ret) {
				dev_err(&pdev->dev, "failed to create attr group\n");
				goto err_suspend;
			}
		}

		/* CSR transmit channel config, only i2s3 on A210 is supported */
		if (!strcmp(i2s_priv->drvdata->name, "i2s3-8ch-sd0")) {
			i2s_priv->sys_csr = devm_platform_ioremap_resource(pdev, 1);
			if (!i2s_priv->sys_csr)
				dev_warn(&pdev->dev, "failed to map sys_csr\n");
			else
				i2s_priv->hdmi_connected =
					is_csr_available(np, TRANSFER_PORT_HDMI, -1);
		}
	}

	i2s_priv->dma_params_tx.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	i2s_priv->dma_params_rx.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	i2s_priv->dma_params_tx.maxburst = ZHIHE_I2S_DMA_MAX_BURST;
	i2s_priv->dma_params_rx.maxburst = ZHIHE_I2S_DMA_MAX_BURST;
	i2s_priv->dma_params_tx.addr = res->start + i2s_priv->drvdata->tx_dr;
	i2s_priv->dma_params_rx.addr = res->start + i2s_priv->drvdata->rx_dr;
	if (i2s_priv->board == ZHIHE_A210) {
		dma_pcfg = devm_kzalloc(&pdev->dev, sizeof(*dma_pcfg), GFP_KERNEL);
		if (!dma_pcfg) {
			ret = -ENOMEM;
			goto err_suspend;
		}
		dma_pcfg->dst_addr_incr = false;
		dma_pcfg->src_addr_incr = false;
		dma_pcfg->dst_fixed_burst = ZHIHE_I2S_DMA_FIX_BURST;
		dma_pcfg->src_fixed_burst = ZHIHE_I2S_DMA_FIX_BURST;

		i2s_priv->dma_params_tx.peripheral_config = (void *)dma_pcfg;
		i2s_priv->dma_params_tx.peripheral_size = sizeof(*dma_pcfg);
		i2s_priv->dma_params_rx.peripheral_config = (void *)dma_pcfg;
		i2s_priv->dma_params_rx.peripheral_size = sizeof(*dma_pcfg);
	}

	ret = devm_snd_dmaengine_pcm_register(&pdev->dev, NULL, 0);
	if (ret) {
		dev_err(&pdev->dev, "Could not register PCM\n");
		goto err_suspend;
	}

	dai = devm_kmemdup(&pdev->dev, zhihe_i2s_soc_dai,
			   sizeof(zhihe_i2s_soc_dai), GFP_KERNEL);
	if (!dai) {
		ret = -ENOMEM;
		goto err_suspend;
	}
	dai[DAI_I2S].name = i2s_priv->drvdata->i2s_dai_name;
	dai[DAI_HDMI].name = i2s_priv->drvdata->hdmi_dai_name;

	ret = devm_snd_soc_register_component(&pdev->dev,
					      &zhihe_i2s_soc_component, dai,
					      ARRAY_SIZE(zhihe_i2s_soc_dai));
	if (ret) {
		dev_err(&pdev->dev, "Could not register DAI\n");
		goto err_suspend;
	}

	/**
	 * Notice: In the I2S3 interface, SD0 serves as the master channel for
	 * register control, while SD1, SD2, and SD3 operate as slave channels.
	 */
	if (strstr(i2s_priv->drvdata->name, I2S3)) {
		if (!strcmp(i2s_priv->drvdata->name, "i2s3-8ch-sd0"))
			i2s3_host_regs = i2s_priv->regs;
		i2s3_probe_flag++;

	}

	return ret;

err_suspend:
	if (!pm_runtime_status_suspended(&pdev->dev))
		zhihe_i2s_rt_suspend(&pdev->dev);

err_pm_disable:
	pm_runtime_disable(&pdev->dev);

err_clk:
	clk_disable_unprepare(i2s_priv->pclk);
	return ret;
}

static void zhihe_i2s_remove(struct platform_device *pdev)
{
	struct zhihe_i2s_priv *i2s_priv = dev_get_drvdata(&pdev->dev);

	if (strstr(i2s_priv->drvdata->name, I2S3)) {
		i2s3_probe_flag--;
		if (!i2s_priv->regmap)
			return;
		if (i2s3_probe_flag == 0)
			i2s3_host_regs = NULL;
	}

	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		zhihe_i2s_rt_suspend(&pdev->dev);

	clk_disable_unprepare(i2s_priv->pclk);
}

static const struct dev_pm_ops zhihe_i2s_pm_ops = {
	SET_RUNTIME_PM_OPS(zhihe_i2s_rt_suspend, zhihe_i2s_rt_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(zhihe_i2s_suspend, zhihe_i2s_resume)
};

static struct platform_driver zhihe_i2s_driver = {
	.probe = zhihe_i2s_probe,
	.remove_new = zhihe_i2s_remove,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = of_match_ptr(zhihe_i2s_of_match),
		.pm = &zhihe_i2s_pm_ops,
	},
};
module_platform_driver(zhihe_i2s_driver);

MODULE_AUTHOR("Anonymous <anonymous@zhcomputing.com>");
MODULE_DESCRIPTION("Zhihe I2S PCM Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
