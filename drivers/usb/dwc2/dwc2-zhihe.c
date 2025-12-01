// SPDX-License-Identifier: GPL-2.0
/*
 * dwc2-zhihe.c - ZHIHE platform specific glue layer for DWC2
 *
 * Inspired by dwc3-zhihe.c and dwc2-platform.c
 *
 * Copyright (C) 2025, Anonymous <Anonymous@zhcomputing.com>
 */

#include <linux/io.h>
#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/usb/of.h>
#include <linux/reset.h>
#include <linux/gpio/consumer.h>

#include "core.h"
#include "hcd.h"

/* USB20 BLK SYSREG registers */
#define USB20_PHY_ANA_CFG	0x0
#define USB20_PHY_CFG		0x4
#define USB21_PHY_ANA_CFG	0x1000
#define USB21_PHY_CFG		0x1004

/* Bit fields */
/* USB20_PHY_CFG */
#define USB0_PHY_DM_PULLDOWN	BIT(1)
#define USB0_PHY_DP_PULLDOWN	BIT(0)

/* USB21_PHY_CFG */
#define USB1_PHY_DM_PULLDOWN	BIT(3)
#define USB1_PHY_DP_PULLDOWN	BIT(2)

/* USB2.0 PHY TxVRefTune Mask */
#define USB20_PHY_TXVREFTUNE_MASK	0x1E0000
#define HS_DV_VOLTAGE_LEVEL_POS_16_PER	(0xB << 17)

struct dwc2_zhihe {
	struct device		*dev;
	void __iomem		*usb20_blk_sysreg;
	struct reset_control	*usb0_phy_rst;
	struct reset_control	*usb1_phy_rst;
	struct gpio_desc 	*usb20_pwren;
	struct gpio_desc 	*usb21_pwren;
};

static int dwc2_zhihe_probe(struct platform_device *pdev)
{
	struct device		*dev = &pdev->dev;
	struct device_node	*np = dev->of_node;
	struct dwc2_zhihe	*zhihe;
	int			ret, val;

	if (!np) {
		dev_err(dev, "device node not found\n");
		return -ENODEV;
	}

	zhihe = devm_kzalloc(dev, sizeof(*zhihe), GFP_KERNEL);
	if (!zhihe)
		return -ENOMEM;

	platform_set_drvdata(pdev, zhihe);
	zhihe->dev = dev;

	/* Get USB20 system registers */
	struct resource *res = platform_get_resource_byname(pdev, 
					IORESOURCE_MEM, "usb20-blk-sysreg");
	if (!res) {
		dev_err(dev, "failed to get resource - %ld\n", PTR_ERR(res));
		return PTR_ERR(res);
	}					

	zhihe->usb20_blk_sysreg = devm_ioremap_resource(dev, res);
	if (IS_ERR(zhihe->usb20_blk_sysreg)) {
		dev_err(dev, "failed to get iomem - %ld\n", PTR_ERR(zhihe->usb20_blk_sysreg));
		return PTR_ERR(zhihe->usb20_blk_sysreg);
	}

	zhihe->usb0_phy_rst = devm_reset_control_get_shared(&pdev->dev, "usb0-phy-rst");
	if (IS_ERR(zhihe->usb0_phy_rst))
		return PTR_ERR(zhihe->usb0_phy_rst);
	
	zhihe->usb1_phy_rst = devm_reset_control_get_shared(&pdev->dev, "usb1-phy-rst");
	if (IS_ERR(zhihe->usb1_phy_rst))
		return PTR_ERR(zhihe->usb1_phy_rst);

	/* Pull-up the  PHY reset */
	val = readl(zhihe->usb20_blk_sysreg + USB20_PHY_CFG);
	val |= USB0_PHY_DM_PULLDOWN | USB0_PHY_DP_PULLDOWN;
	writel(val, zhihe->usb20_blk_sysreg + USB20_PHY_CFG);

	val = readl(zhihe->usb20_blk_sysreg + USB21_PHY_CFG);
	val |= USB1_PHY_DM_PULLDOWN | USB1_PHY_DP_PULLDOWN;
	writel(val, zhihe->usb20_blk_sysreg + USB21_PHY_CFG);

	reset_control_deassert(zhihe->usb0_phy_rst);
	reset_control_deassert(zhihe->usb1_phy_rst);

	val = readl(zhihe->usb20_blk_sysreg + USB20_PHY_ANA_CFG);
	val &= ~USB20_PHY_TXVREFTUNE_MASK;
	val |= HS_DV_VOLTAGE_LEVEL_POS_16_PER;
	writel(val, zhihe->usb20_blk_sysreg + USB20_PHY_ANA_CFG);

	val = readl(zhihe->usb20_blk_sysreg + USB21_PHY_ANA_CFG);
	val &= ~USB20_PHY_TXVREFTUNE_MASK;
	val |= HS_DV_VOLTAGE_LEVEL_POS_16_PER;
	writel(val, zhihe->usb20_blk_sysreg + USB21_PHY_ANA_CFG);

	reset_control_assert(zhihe->usb0_phy_rst);
	reset_control_assert(zhihe->usb1_phy_rst);

	/* Populate child nodes (dwc2 controllers) */
	ret = of_platform_populate(np, NULL, NULL, dev);
	if (ret) {
		dev_err(dev, "failed to populate child nodes: %d\n", ret);
		return ret;
	}

	zhihe->usb20_pwren = devm_gpiod_get_optional(&pdev->dev,
						     "usb20-pwren",
						     GPIOD_OUT_LOW);
	if (IS_ERR(zhihe->usb20_pwren)) {
		dev_err(&pdev->dev, "Failed to get usb20-pwren GPIO\n");
		return PTR_ERR(zhihe->usb20_pwren);
	}

	zhihe->usb21_pwren = devm_gpiod_get_optional(&pdev->dev,
						     "usb21-pwren",
						     GPIOD_OUT_LOW);
	if (IS_ERR(zhihe->usb21_pwren)) {
		dev_err(&pdev->dev, "Failed to get usb21-pwren GPIO\n");
		return PTR_ERR(zhihe->usb21_pwren);
	}

	if (zhihe->usb20_pwren)
		gpiod_set_value(zhihe->usb20_pwren, 1);

	if (zhihe->usb21_pwren)
		gpiod_set_value(zhihe->usb21_pwren, 1);

	dev_info(dev, "ZHIHE DWC2 glue layer initialized\n");
	return 0;
}

static int dwc2_zhihe_remove(struct platform_device *pdev)
{
	struct dwc2_zhihe *zhihe = platform_get_drvdata(pdev);

	of_platform_depopulate(zhihe->dev);
	return 0;
}

static const struct of_device_id dwc2_zhihe_of_match[] = {
	{ .compatible = "zhihe,usb20" },
	{ },
};
MODULE_DEVICE_TABLE(of, dwc2_zhihe_of_match);

static struct platform_driver dwc2_zhihe_driver = {
	.probe		= dwc2_zhihe_probe,
	.remove		= dwc2_zhihe_remove,
	.driver		= {
		.name	= "dwc2-zhihe",
		.of_match_table = dwc2_zhihe_of_match,
	},
};

module_platform_driver(dwc2_zhihe_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ZHIHE DWC2 Glue Layer");
MODULE_AUTHOR("Anonymous <Anonymous@zhcomputing.com>");
