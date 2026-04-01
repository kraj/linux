// SPDX-License-Identifier: GPL-2.0-only
/*
 * Memory-mapped interface driver for DW ehance-spi core
 *
 * Copyright (c) 2021-2024, ailibaba-inc corperation.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/scatterlist.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/acpi.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/pm_runtime.h>

#include "spi-dw-quad.h"

#define DRIVER_NAME "dw_qspi_mmio-quad"
#define RSTC_COUNT 2
#define RSTC_ID0 "srst"
#define RSTC_ID1 "prst"

struct dw_qspi_mmio {
	struct dw_spi	dws;
	struct clk		*clk;
	struct clk		*pclk;
	void			*priv;
	struct reset_control_bulk_data resets[RSTC_COUNT];
	unsigned int nresets;
};

static int qspi_clk_prepare_enable(struct dw_qspi_mmio *dwsmmio)
{
	int ret;

	ret = clk_prepare_enable(dwsmmio->pclk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(dwsmmio->clk);
	if (ret) {
		clk_disable_unprepare(dwsmmio->pclk);
		return ret;
	}

	return 0;
}

static void qspi_clk_disable_unprepare(struct dw_qspi_mmio *dwsmmio)
{
	clk_disable_unprepare(dwsmmio->pclk);
	clk_disable_unprepare(dwsmmio->clk);
}

static int __maybe_unused dw_qspi_runtime_suspend(struct device *dev)
{
	struct dw_qspi_mmio *dwsmmio = dev_get_drvdata(dev);

	reset_control_bulk_assert(dwsmmio->nresets, dwsmmio->resets);
	qspi_clk_disable_unprepare(dwsmmio);

	return 0;
}

static int __maybe_unused dw_qspi_runtime_resume(struct device *dev)
{
	struct dw_qspi_mmio *dwsmmio = dev_get_drvdata(dev);
	int ret;

	ret = qspi_clk_prepare_enable(dwsmmio);
	reset_control_bulk_deassert(dwsmmio->nresets, dwsmmio->resets);

	return ret;
}

static int __maybe_unused dw_qspi_mmio_suspend(struct device *dev)
{
	int ret;
	struct dw_qspi_mmio *dwsmmio = dev_get_drvdata(dev);

	ret = dw_qspi_suspend_host(&dwsmmio->dws);

	reset_control_bulk_assert(dwsmmio->nresets, dwsmmio->resets);
	qspi_clk_disable_unprepare(dwsmmio);

	return ret;
}

static int __maybe_unused dw_qspi_mmio_resume(struct device *dev)
{
	struct dw_qspi_mmio *dwsmmio = dev_get_drvdata(dev);
	int ret;

	ret = qspi_clk_prepare_enable(dwsmmio);
	if (ret) {
		dev_err(dev, "failed to enable spi clock(%d)\n", ret);
		return ret;
	}
	reset_control_bulk_deassert(dwsmmio->nresets, dwsmmio->resets);

	return dw_qspi_resume_host(&dwsmmio->dws);
}

static int dw_qspi_mmio_probe(struct platform_device *pdev)
{
	int (*init_func)(struct platform_device *pdev,
			 struct dw_qspi_mmio *dwsmmio);
	struct dw_qspi_mmio *dwsmmio;
	struct dw_spi *dws;
	int ret;
	u32 num_cs, rx_sample_dly = 4;
	u32 swap_data = 0;

	dwsmmio = devm_kzalloc(&pdev->dev, sizeof(struct dw_qspi_mmio),
			GFP_KERNEL);
	if (!dwsmmio)
		return -ENOMEM;

	dws = &dwsmmio->dws;

	/* Get basic io resource and map it */
	dws->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(dws->regs)) {
		dev_err(&pdev->dev, "QSPI region map failed\n");
		return PTR_ERR(dws->regs);
	}

	dws->irq = platform_get_irq(pdev, 0);
	if (dws->irq < 0)
		return dws->irq; /* -ENXIO */

	/* Main clock */
	dwsmmio->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(dwsmmio->clk))
		return PTR_ERR(dwsmmio->clk);
	ret = clk_prepare_enable(dwsmmio->clk);
	if (ret)
		return ret;

	/* Optional clock needed to access the registers */
	dwsmmio->pclk = devm_clk_get_optional(&pdev->dev, "pclk");
	if (IS_ERR(dwsmmio->pclk)) {
		ret = PTR_ERR(dwsmmio->pclk);
		goto out_clk;
	}
	ret = clk_prepare_enable(dwsmmio->pclk);
	if (ret)
		goto out_clk;

	/* Reset ctrl */
	dwsmmio->resets[0].id = RSTC_ID0;
	dwsmmio->resets[1].id = RSTC_ID1;
	dwsmmio->nresets = ARRAY_SIZE(dwsmmio->resets);
	ret = devm_reset_control_bulk_get_optional_exclusive(&pdev->dev, dwsmmio->nresets, dwsmmio->resets);
	if (ret) {
		dev_err(&pdev->dev, "failed to get reset: %d\n", ret);
		goto out_pclk;
	}
	ret = reset_control_bulk_deassert(dwsmmio->nresets, dwsmmio->resets);
	if (ret) {
		dev_err(&pdev->dev, "failed to deassert reset: %d\n", ret);
		goto out_pclk;
	}

	/* set bus number */
	dws->bus_num = pdev->id;

	/* get supported freq in max */
	dws->max_freq = clk_get_rate(dwsmmio->clk);

	//dws->slave_cs = devm_gpiod_get_index_optional(&pdev->dev, "cs", 0,GPIOD_OUT_LOW);
	dws->slave_cs = devm_gpiod_get_optional(&pdev->dev, "cs", GPIOD_OUT_LOW);
	if (IS_ERR(dws->slave_cs))
		return PTR_ERR(dws->slave_cs);
	/* get reg width of controler */
	device_property_read_u32(&pdev->dev, "reg-io-width", &dws->reg_io_width);

	/* get chip select count of controler */
	num_cs = 1;
	device_property_read_u32(&pdev->dev, "num-cs", &num_cs);
	dws->num_cs = num_cs;

	device_property_read_u32(&pdev->dev, "rx-sample-dly", &rx_sample_dly);
	dev_dbg(&pdev->dev, "get rx_sample_dly success %d\n", rx_sample_dly);
	dws->rx_sample_delay = rx_sample_dly;

	device_property_read_u32(&pdev->dev, "spi-swap-data", &swap_data);
	dws->swap_data = swap_data;

	init_func = device_get_match_data(&pdev->dev);
	if (init_func) {
		ret = init_func(pdev, dwsmmio);
		if (ret)
			goto out_resets;
	}
	platform_set_drvdata(pdev, dwsmmio);

	/* Enable runtime pm */
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_set_autosuspend_delay(&pdev->dev, 2000);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	ret = dw_qspi_add_host(&pdev->dev, dws);
	if (ret){
		goto out;
	}

	return 0;
out:
	pm_runtime_disable(&pdev->dev);
out_resets:
	reset_control_bulk_deassert(dwsmmio->nresets, dwsmmio->resets);
out_pclk:
	clk_disable_unprepare(dwsmmio->pclk);
out_clk:
	clk_disable_unprepare(dwsmmio->clk);
	return ret;
}

static int dw_qspi_mmio_remove(struct platform_device *pdev)
{
	struct dw_qspi_mmio *dwsmmio = platform_get_drvdata(pdev);

	dw_qspi_remove_host(&dwsmmio->dws);
	clk_disable_unprepare(dwsmmio->pclk);
	clk_disable_unprepare(dwsmmio->clk);

	return 0;
}

static const struct of_device_id dw_qspi_mmio_of_match[] = {
	{ .compatible = "snps,dw-apb-ssi-quad", },
	{ /* end of table */}
};
MODULE_DEVICE_TABLE(of, dw_qspi_mmio_of_match);

static const struct dev_pm_ops qspi_mmio_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dw_qspi_mmio_suspend, dw_qspi_mmio_resume)
	SET_RUNTIME_PM_OPS(dw_qspi_runtime_suspend, dw_qspi_runtime_resume, NULL)
};

static struct platform_driver dw_qspi_mmio_driver = {
	.probe		= dw_qspi_mmio_probe,
	.remove		= dw_qspi_mmio_remove,
	.driver		= {
		.name	= DRIVER_NAME,
		.of_match_table = dw_qspi_mmio_of_match,
		.pm = &qspi_mmio_pm_ops,
	},
};
module_platform_driver(dw_qspi_mmio_driver);

MODULE_AUTHOR("linghui zeng <linghui.zlh@linux.alibaba.com>");
MODULE_DESCRIPTION("Memory-mapped I/O interface driver for DW ehance-spi Core");
MODULE_LICENSE("GPL v2");
