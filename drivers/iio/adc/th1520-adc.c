/* SPDX-License-Identifier: GPL-2.0 */
/*
 * THEAD TH1520 ADC driver
 *
 * Copyright (C) 2021-2024 Alibaba Group Holding Limited.
 * Fugang Duan <duanfugang.dfg@linux.alibaba.com>
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/regulator/consumer.h>
#include <linux/of_platform.h>
#include <linux/err.h>
#include <linux/bitfield.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/sysfs.h>

#include "th1520-adc.h"

/*
 * ADC default feature init
 */
static inline void th1520_adc_cfg_init(struct th1520_adc *info)
{
	struct th1520_adc_feature *adc_feature = &info->adc_feature;

	/* set default Configuration for ADC controller */
	adc_feature->resolution = 12;
	adc_feature->input_mode = TH1520_ADC_SINGLE_ENDED_INPUTS;
	adc_feature->vol_ref = TH1520_ADC_VOL_VREF;
	//adc_feature->offset_mode = TH1520_ADC_OFFSET_DIS;
	adc_feature->conv_mode = TH1520_ADC_MODE_SINGLE;
	adc_feature->sampling_freq = TH1520_ADC_FCLK_1M_HZ; //1MSPS

	adc_feature->fclk_div = 4;

	//adc_feature->int_actual = TH1520_ADC_ACTUAL_ALL;
	//adc_feature->int_detal = TH1520_ADC_DETAL_ALL;

	//info->ch0_offmeas = 0;
	//info->ch1_offmeas = 0;
}

/*
 * ADC reset, include reset phy smampling fclk 
 */
static void th1520_adc_reset(struct th1520_adc *info)
{
	u32 tmp = readl(info->regs + TH1520_ADC_PHY_CTRL);

	tmp |= TH1520_ADC_PHY_CTRL_RST_EN;
	writel(tmp, info->regs + TH1520_ADC_PHY_CTRL);

	usleep_range(10, 15);

	/* After reset, the phy enters standby mode. */
	tmp &= ~TH1520_ADC_PHY_CTRL_RST_EN;
	writel(tmp, info->regs + TH1520_ADC_PHY_CTRL);
}

static void th1520_adc_reg_set(struct th1520_adc *info)
{
	u32 phy_cfg = 0;
	u32 op_ctrl = 0;
	struct th1520_adc_feature *adc_feature = &info->adc_feature;

	/* phy_cfg */
	switch (adc_feature->resolution) {
	case 6:
		phy_cfg |= TH1520_ADC_PHY_CFG_SELRES_6BIT;
		break;
	case 8:
		phy_cfg |= TH1520_ADC_PHY_CFG_SELRES_8BIT;
		break;
	case 10:
		phy_cfg |= TH1520_ADC_PHY_CFG_SELRES_10BIT;
		break;
	case 12:
		phy_cfg |= TH1520_ADC_PHY_CFG_SELRES_12BIT;
		break;
	default:
		break;
	}

	switch (adc_feature->input_mode) {
	case TH1520_ADC_SINGLE_ENDED_INPUTS:
		phy_cfg |= TH1520_ADC_PHY_CFG_SELDIFF_SINGLE_ENDED_INPUTS;
		break;
	case TH1520_ADC_DIFFERENTIAL_INPUTS:
		phy_cfg |= TH1520_ADC_PHY_CFG_SELDIFF_DIFFERENTIAL_INPUTS;
		break;
	default:
		break;
	}

	switch (adc_feature->vol_ref) {
	case TH1520_ADC_VOL_VREF:
		phy_cfg |= TH1520_ADC_PHY_CFG_SELBG_EXTERNAL |
			   TH1520_ADC_PHY_CFG_SELREF_EXT;
		break;
	case TH1520_ADC_VOL_INTE:
		phy_cfg |= TH1520_ADC_PHY_CFG_SELBG_INTERNAL |
			   TH1520_ADC_PHY_CFG_SELREF_INTERNAL;
		break;
	default:
		break;
	}

	/* op_ctrl */
	switch (adc_feature->conv_mode) {
	case TH1520_ADC_MODE_SINGLE:
		op_ctrl |= TH1520_ADC_OP_CTRL_MODE_SINGLE;
		break;
	case TH1520_ADC_MODE_CONTINOUS:
		op_ctrl |= TH1520_ADC_OP_CTRL_MODE_CONTINOUS;
		break;
	default:
		break;
	}

	writel(phy_cfg, info->regs + TH1520_ADC_PHY_CFG);
	writel(op_ctrl, info->regs + TH1520_ADC_OP_CTRL);

	writel(TH1520_ADC_PHY_ENCTR, info->regs + TH1520_ADC_PHY_TEST);

	/* disable the irq */
	writel(0xff, info->regs + TH1520_ADC_INT_CTRL1);
	writel(0xff, info->regs + TH1520_ADC_INT_CTRL2);

	if (adc_feature->conv_mode == TH1520_ADC_MODE_CONTINOUS)
		writel(TH1520_ADC_PHY_CTRL_ENADC_EN,
		       info->regs + TH1520_ADC_PHY_CTRL);
}

static int th1520_adc_sampling_freq_set(struct th1520_adc *info, int sampling_freq)
{
	unsigned long apb_clk;
	int sample_time = 0;
	struct th1520_adc_feature *adc_feature = &info->adc_feature;

	if (sampling_freq < 0) {
		sampling_freq = adc_feature->sampling_freq;
	}

	if (sampling_freq == TH1520_ADC_FCLK_1M_HZ || sampling_freq == TH1520_ADC_FCLK_2_5M_HZ) {
		apb_clk = clk_get_rate(info->clk);

		int fclk_freq = DIV_ROUND_CLOSEST(apb_clk, adc_feature->fclk_div);

		/* cal sampling div */
		sample_time = DIV_ROUND_CLOSEST(fclk_freq, sampling_freq) - 1;

		writel(sample_time, info->regs + TH1520_ADC_SAMPLE_TIME);

		/* update current sampling_freq */
		adc_feature->sampling_freq = sampling_freq;
	} else {
		return -EINVAL;
	}

	return 0;
}

static void th1520_adc_fclk_set(struct th1520_adc *info)
{
	unsigned long apb_clk;

	int fclk_ctrl = 0;
	int start_time = 0;
	struct th1520_adc_feature *adc_feature = &info->adc_feature;

	fclk_ctrl = adc_feature->fclk_div;
	fclk_ctrl |= FIELD_PREP(TH1520_ADC_FCLK_CTRL_PHASE_SEL, TH1520_ADC_FCLK_CTRL_PHASE_SEL_VAL);

	apb_clk = clk_get_rate(info->clk);

	/* start delay > 5us, base fclk */
	int fclk_freq = DIV_ROUND_CLOSEST(apb_clk, adc_feature->fclk_div);
	start_time = 5 * DIV_ROUND_UP(fclk_freq, 1000000);

	writel(fclk_ctrl, info->regs + TH1520_ADC_FCLK_CTRL);
	writel(start_time, info->regs + TH1520_ADC_START_TIME);
}

static void th1520_adc_hw_init(struct th1520_adc *info)
{
	th1520_adc_reg_set(info);
	th1520_adc_fclk_set(info);

	/* -1, use default freq */
	th1520_adc_sampling_freq_set(info, -1);
}

static irqreturn_t th1520_adc_isr(int irq, void *dev_id)
{
	struct iio_dev *indio_dev = dev_id;
	struct th1520_adc *info = iio_priv(indio_dev);
	/* TBD */
	complete(&info->completion);
	return IRQ_HANDLED;
}

/*
 * IIO base ops
 */
static const struct iio_chan_spec th1520_adc_iio_channels[] = {
	TH1520_ADC_CHAN(0, IIO_VOLTAGE),
	TH1520_ADC_CHAN(1, IIO_VOLTAGE),
	TH1520_ADC_CHAN(2, IIO_VOLTAGE),
	TH1520_ADC_CHAN(3, IIO_VOLTAGE),
	TH1520_ADC_CHAN(4, IIO_VOLTAGE),
	TH1520_ADC_CHAN(5, IIO_VOLTAGE),
	TH1520_ADC_CHAN(6, IIO_VOLTAGE),
	TH1520_ADC_CHAN(7, IIO_VOLTAGE),
	/* sentinel */
};

static const struct iio_chan_spec a210_adc_iio_channels[] = {
	TH1520_ADC_CHAN(0, IIO_VOLTAGE),
	TH1520_ADC_CHAN(1, IIO_VOLTAGE),
	TH1520_ADC_CHAN(2, IIO_VOLTAGE),
	TH1520_ADC_CHAN(3, IIO_VOLTAGE),
	/* sentinel */
};

static int th1520_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	int tmp;
	long ret;
	struct device *dev = indio_dev->dev.parent;
	struct th1520_adc *info = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		pm_runtime_get_sync(dev);
		mutex_lock(&info->mlock);

		if (info->adc_feature.conv_mode == TH1520_ADC_MODE_SINGLE) {
			uint ievent;
			uint vld_flag;
			uint phy_ctrl;
			uint real_chan;
			uint op_ctrl = 0;
			uint single_retry = TH1520_ADC_FIFO_DATA_SIZE;

			op_ctrl = readl(info->regs + TH1520_ADC_OP_CTRL);
			op_ctrl &= ~TH1520_ADC_OP_CTRL_CH_EN_ALL;
			op_ctrl |= (BIT(chan->channel +	TH1520_ADC_OP_CTRL_CH_EN_0) &
					TH1520_ADC_OP_CTRL_CH_EN_ALL);
			writel(op_ctrl, info->regs + TH1520_ADC_OP_CTRL);

			/* Enable phy */
			writel(TH1520_ADC_PHY_CTRL_ENADC_EN,
			       info->regs + TH1520_ADC_PHY_CTRL);

			vld_flag = TH1520_ADC_SAMPLE_DATA_CH0_VLD;

			while (single_retry--) {
				writel(TH1520_ADC_OP_SINGLE_START_EN,
				       info->regs + TH1520_ADC_OP_SINGLE_START);
				/* wait the sampling result */
				ret = readl_poll_timeout(info->regs +
							 TH1520_ADC_SAMPLE_DATA,
							 ievent,
							 ievent & vld_flag, 100,
							 TH1520_ADC_TIMEOUT);
				if (ret)
					pr_err("wait the sampling timeout\n");

				real_chan =
				(ievent & TH1520_ADC_SAMPLE_DATA_CH0_NUMBER) >>
				TH1520_ADC_SAMPLE_DATA_CH0_NUMBER_OFF;
				if (real_chan == chan->channel)
					break;
			}

			/* Standby phy */
			phy_ctrl = readl(info->regs + TH1520_ADC_PHY_CTRL);
			phy_ctrl &= ~TH1520_ADC_PHY_CTRL_ENADC_EN;
			writel(phy_ctrl, info->regs + TH1520_ADC_PHY_CTRL);

			/* read the sampling data */
			*val = (ievent & TH1520_ADC_SAMPLE_DATA_CH0) >>
			       TH1520_ADC_SAMPLE_DATA_CH0_OFF;
		} else {
			uint ievent;
			uint vld_flag;
			uint op_single;
			uint op_ctrl = 0;

			op_ctrl = readl(info->regs + TH1520_ADC_OP_CTRL);
			op_ctrl &= ~TH1520_ADC_OP_CTRL_CH_EN_ALL;
			op_ctrl |= (BIT(chan->channel + TH1520_ADC_OP_CTRL_CH_EN_0) &
				   TH1520_ADC_OP_CTRL_CH_EN_ALL);
			writel(op_ctrl, info->regs + TH1520_ADC_OP_CTRL);

			op_single = readl(info->regs +
					  TH1520_ADC_OP_SINGLE_START);
			op_single &= ~TH1520_ADC_OP_SINGLE_START_EN;
			writel(op_single,
			       info->regs + TH1520_ADC_OP_SINGLE_START);

			vld_flag = TH1520_ADC_SAMPLE_DATA_CH0_VLD |
				   TH1520_ADC_SAMPLE_DATA_CH1_VLD;

			/* wait the sampling result */
			ret  = readl_poll_timeout(info->regs +
							TH1520_ADC_SAMPLE_DATA,
						  ievent, ievent & vld_flag, 10,
						  TH1520_ADC_TIMEOUT);
			if (ret)
				pr_err("wait the sampling timeout\n");

			/* read the sampling data */
			tmp = readl(info->regs + TH1520_ADC_SAMPLE_DATA);
			if (tmp & TH1520_ADC_SAMPLE_DATA_CH0_VLD)
				*val = (tmp & TH1520_ADC_SAMPLE_DATA_CH0) >>
				       TH1520_ADC_SAMPLE_DATA_CH0_OFF;
			else
				*val = (tmp & TH1520_ADC_SAMPLE_DATA_CH1) >>
				       TH1520_ADC_SAMPLE_DATA_CH1_OFF;
		}

		/* 
		 * The hardware normalizes the sampled value to 12 bits;
		 * restore the original value based on the actual bit resolution.
		 */
		*val >>= (12 - info->adc_feature.resolution);

		mutex_unlock(&info->mlock);
		pm_runtime_put_sync(dev);
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		*val = info->vref_uv / 1000;
		*val2 = info->adc_feature.resolution;
		return IIO_VAL_FRACTIONAL_LOG2;

	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = info->adc_feature.sampling_freq;
		*val2 = 0;
		return IIO_VAL_INT;

	default:
		break;
	}

	return -EINVAL;
}

static int th1520_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	int ret;
	struct device *dev = indio_dev->dev.parent;
	struct th1520_adc *info = iio_priv(indio_dev);

	if (mask != IIO_CHAN_INFO_SAMP_FREQ)
		return -EINVAL;

	pm_runtime_get_sync(dev);
	mutex_lock(&info->mlock);
	ret = th1520_adc_sampling_freq_set(info, val);
	mutex_unlock(&info->mlock);
	pm_runtime_put_sync(dev);

	return ret;
}

/*
 * IIO custom attr
 * Add device attr to extend adc resolution config. 
 */
static ssize_t resolution_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct th1520_adc *info = iio_priv(indio_dev);

	/* buf size = PAGE_SIZE */
	return sprintf(buf, "%u\n", info->adc_feature.resolution);
}

static ssize_t resolution_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t size)
{
	unsigned long res;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct th1520_adc *info = iio_priv(indio_dev);

	if (kstrtoul(buf, 0, &res))
		return -EINVAL;

	switch (res) {
	case 6:
	case 8:
	case 10:
	case 12:
		info->adc_feature.resolution = res;
#ifndef CONFIG_PM
		th1520_adc_resume(dev);
#endif
		break;
	default:
		dev_err(dev, "Resolution %ldbit not support. Configure it to 12, 10, 8, or 6.\n", res);
		return -EINVAL;
	}

	return size;
}

static DEVICE_ATTR_RW(resolution);

static struct attribute *th1520_attrs[] = {
    &dev_attr_resolution.attr,
    NULL,
};

static const struct attribute_group th1520_attribute_group = {
    .attrs = th1520_attrs,
};

static const struct iio_info th1520_adc_iio_info = {
	.read_raw = &th1520_read_raw,
	.write_raw = &th1520_write_raw,
	.attrs = &th1520_attribute_group,
};

/*
 * Driver match & probe
 */
static int a210_adc_init(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);

	indio_dev->channels = a210_adc_iio_channels;
	indio_dev->num_channels = ARRAY_SIZE(a210_adc_iio_channels);

	return 0;
}

static int th1520_adc_init(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);

	indio_dev->channels = th1520_adc_iio_channels;
	indio_dev->num_channels = ARRAY_SIZE(th1520_adc_iio_channels);

	return 0;
}
static const struct of_device_id th1520_adc_match[] = {
	{ .compatible = "thead,th1520-adc", .data = th1520_adc_init},
	{ .compatible = "zhihe,a210-adc", .data = a210_adc_init},
	{ /* end */ }
};
MODULE_DEVICE_TABLE(of, th1520_adc_match);

static int th1520_adc_probe(struct platform_device *pdev)
{
	int irq;
	int ret;
	struct resource *mem;
	struct th1520_adc *info;
	struct iio_dev *indio_dev;
	u32 sampling_freq, resolution, fclk_div;
	struct device_node *np = pdev->dev.of_node;
	int (*init_func)(struct platform_device *pdev);

	indio_dev = devm_iio_device_alloc(&pdev->dev,
					  sizeof(struct th1520_adc));
	if (!indio_dev) {
		dev_err(&pdev->dev, "Failed allocating iio device\n");
		return -ENOMEM;
	}

	info = iio_priv(indio_dev);
	info->dev = &pdev->dev;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(info->dev, irq, th1520_adc_isr, 0,
			       dev_name(&pdev->dev), indio_dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed requesting irq, irq = %d\n", irq);
		return ret;
	}

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	info->regs = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(info->regs))
		return PTR_ERR(info->regs);

	info->clk = devm_clk_get(&pdev->dev, "adc");
	if (IS_ERR(info->clk)) {
		dev_err(&pdev->dev, "failed getting clock, err = %ld\n",
			PTR_ERR(info->clk));
		return PTR_ERR(info->clk);
	}

	info->vref = devm_regulator_get(&pdev->dev, "vref");
	if (IS_ERR(info->vref))
		return PTR_ERR(info->vref);

	info->vref_uv = regulator_get_voltage(info->vref);

	info->reset = of_reset_control_get_optional_exclusive(np, NULL);
	if (IS_ERR(info->reset)) {
		return PTR_ERR(info->reset);
	}

	platform_set_drvdata(pdev, indio_dev);

	mutex_init(&info->mlock);
	init_completion(&info->completion);

	indio_dev->name = dev_name(&pdev->dev);
	indio_dev->dev.parent = &pdev->dev;
	indio_dev->dev.of_node = pdev->dev.of_node;
	indio_dev->info = &th1520_adc_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	th1520_adc_cfg_init(info);

	if (!of_property_read_u32(np, "resolution", &resolution)) {
		if (resolution == 6 || resolution == 8 || resolution == 10 || resolution == 12) {
			info->adc_feature.resolution = resolution;
		} else {
			dev_warn(&pdev->dev, "ADC resolution unspported, use 12BIT default.\n");
		}
	} else {
		dev_info(&pdev->dev, "ADC resolution missing in DT, use 12BIT default.\n");
	}

	if (!of_property_read_u32(np, "sampling-frequency", &sampling_freq)) {
		if (sampling_freq == TH1520_ADC_FCLK_1M_HZ || sampling_freq == TH1520_ADC_FCLK_2_5M_HZ) {
			info->adc_feature.sampling_freq = sampling_freq;
		} else {
			dev_info(&pdev->dev, "ADC sampling-frequency unsupported in DT, use 1MSPS default.\n");
		}
	} else {
		dev_info(&pdev->dev, "ADC sampling-frequency missing in DT, use 1MSPS default.\n");
	}

	if (!of_property_read_u32(np, "fclk-div", &fclk_div)) {
		info->adc_feature.fclk_div = fclk_div;
	} else {
		dev_info(&pdev->dev, "ADC fclk-div missing in DT, use 4 default.\n");
	}

	init_func = device_get_match_data(&pdev->dev);
	if (init_func) {
		ret = init_func(pdev);
		if (ret)
			goto error_iio_device_register;
	}

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(&pdev->dev, "Couldn't register the device.\n");
		goto error_iio_device_register;
	}

	ret = sysfs_create_file(&pdev->dev.kobj, &dev_attr_resolution.attr);
	if (ret) {
		dev_err(&pdev->dev, "Failed to create adc debug sysfs.\n");
		goto error_iio_device_register;
	}

#ifdef CONFIG_PM
	/* Enalbe pm runtime & set suspened status */
	pm_runtime_set_suspended(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
#else
	th1520_adc_resume(&pdev->dev);
#endif

	dev_info(&pdev->dev, "THEAD TH1520 adc registered.\n");
	return 0;

error_iio_device_register:
	return ret;
}

static int th1520_adc_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct th1520_adc *info = iio_priv(indio_dev);

	sysfs_remove_file(&pdev->dev.kobj, &dev_attr_resolution.attr);
	iio_device_unregister(indio_dev);
	regulator_disable(info->vref);
	clk_disable_unprepare(info->clk);

	return 0;
}

static int __maybe_unused th1520_adc_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct th1520_adc *info = iio_priv(indio_dev);

	clk_disable_unprepare(info->clk);
	regulator_disable(info->vref);
	reset_control_assert(info->reset);

	return 0;
}

static int __maybe_unused th1520_adc_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct th1520_adc *info = iio_priv(indio_dev);
	int ret;

	reset_control_deassert(info->reset);

	ret = regulator_enable(info->vref);
	if (ret)
		return ret;

	ret = clk_prepare_enable(info->clk);
	if (ret)
		goto disable_reg;

	th1520_adc_reset(info);
	th1520_adc_hw_init(info);

	return 0;

disable_reg:
	regulator_disable(info->vref);
	return ret;
}

static DEFINE_RUNTIME_DEV_PM_OPS(th1520_adc_pm_ops,
			 th1520_adc_suspend, th1520_adc_resume, NULL);

static struct platform_driver th1520_adc_driver = {
	.probe          = th1520_adc_probe,
	.remove         = th1520_adc_remove,
	.driver         = {
		.name   = DRIVER_NAME,
		.of_match_table = th1520_adc_match,
		.pm     = pm_ptr(&th1520_adc_pm_ops),
	},
};
module_platform_driver(th1520_adc_driver);

MODULE_AUTHOR("fugang.duan <duanfugang.dfg@linux.alibaba.com>");
MODULE_DESCRIPTION("Thead TH1520 ADC driver");
MODULE_LICENSE("GPL");
