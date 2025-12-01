// SPDX-License-Identifier: GPL-2.0
/*
 * dwc3-zhihe.c - ZHIHE platform specific glue layer
 *
 * Inspired by dwc3-of-simple.c
 *
 * Copyright (C) 2025, Anonymous <Anonymous@zhcomputing.com>
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 */

#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/of_platform.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/of_address.h>
#include <linux/gpio/consumer.h>

#include "core.h"

/* USB31 SYSREG registers */
#define USB20_PHY_OTG_CTRL	0xA0

/* Bit fields */
/* USB20_PHY_OTG_CTRL */
#define DRVVBUS0		BIT(12)

/* Base C10PHY_TCA 0x807C000 */
#define TCA_INTR_EN 		0x4
#define TCA_INTR_STS 		0x8
#define TCA_TCPC  		0x14

/* Base DWC3 Control Register */
#define DWC3_LCSR_TX_DEEMPH_2	0xd068

struct dwc3_zhihe {
	struct device		*dev;
	void __iomem		*usb31_sysreg;	
	struct reset_control	*usb31_arst;
	struct reset_control	*usb31_phy_rst;
	struct reset_control	*c10phy_rst;
	struct clk 		*ref_clk;
	struct clk		*slv_aclk;
	struct clk		*cfg_aclk;
	void __iomem		*c10phy_tca;
	void __iomem		*c10phy_sysreg;
	void __iomem		*dwc3_ctrl;
	struct gpio_desc 	*pwren;
};

static int dwc3_zhihe_probe(struct platform_device *pdev)
{
	struct device		*dev = &pdev->dev;
	struct device_node	*np  = dev->of_node;
	struct dwc3_zhihe	*zhihe;
	struct device_node 	*dwc3_np;
        struct resource	        dwc3_res;
	int			ret;

	if (!np) {
		dev_err(dev, "device node not found\n");
		return -ENODEV;
	}

	zhihe = devm_kzalloc(&pdev->dev, sizeof(*zhihe), GFP_KERNEL);
	if (!zhihe)
		return -ENOMEM;

	platform_set_drvdata(pdev, zhihe);
	zhihe->dev = &pdev->dev;
   
	struct resource *res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "usb31-sysreg");
	if (!res) {
		dev_err(dev, "failed to get resource - %ld\n", PTR_ERR(res));
		return -EINVAL;
	}
        zhihe->usb31_sysreg = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(zhihe->usb31_sysreg)) {
		dev_err(dev, "failed to get iomem - %ld\n", PTR_ERR(zhihe->usb31_sysreg));
		return -EINVAL;
	}
	int val = readl(zhihe->usb31_sysreg + USB20_PHY_OTG_CTRL);
	val |= DRVVBUS0;
	writel(val, zhihe->usb31_sysreg + USB20_PHY_OTG_CTRL);

	zhihe->ref_clk = devm_clk_get(&pdev->dev, "ref-clk");
	if(IS_ERR(zhihe->ref_clk))
		return PTR_ERR(zhihe->ref_clk);

	zhihe->slv_aclk = devm_clk_get(&pdev->dev, "slv-aclk");
	if(IS_ERR(zhihe->slv_aclk))
		return PTR_ERR(zhihe->slv_aclk);

	zhihe->cfg_aclk = devm_clk_get(&pdev->dev, "cfg-aclk");
	if(IS_ERR(zhihe->cfg_aclk))
		return PTR_ERR(zhihe->cfg_aclk);
						
	zhihe->usb31_arst = devm_reset_control_get_shared(&pdev->dev, "usb31-arst");
	if(IS_ERR(zhihe->usb31_arst))
		return PTR_ERR(zhihe->usb31_arst);

	zhihe->usb31_phy_rst = devm_reset_control_get_shared(&pdev->dev, "usb31-phy-rst");
	if(IS_ERR(zhihe->usb31_phy_rst))
		return PTR_ERR(zhihe->usb31_phy_rst);

	zhihe->c10phy_rst = devm_reset_control_get_shared(&pdev->dev, "c10phy-rst");
	if(IS_ERR(zhihe->c10phy_rst))
		return PTR_ERR(zhihe->c10phy_rst);

 	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "c10phy-tca");	
	if (!res) {
		dev_err(dev, "failed to get resource - %ld\n", PTR_ERR(res));
		return PTR_ERR(res);
	}
        zhihe->c10phy_tca = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(zhihe->c10phy_tca)) {
		dev_err(dev, "failed to get iomem - %ld\n", PTR_ERR(zhihe->c10phy_tca));
		return PTR_ERR(zhihe->c10phy_tca);
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "c10phy-sysreg");	
	if (!res) {
		dev_err(dev, "failed to get resource - %ld\n", PTR_ERR(res));
		return PTR_ERR(res);
	}
        zhihe->c10phy_sysreg = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(zhihe->c10phy_sysreg)) {
		dev_err(dev, "failed to get iomem - %ld\n", PTR_ERR(zhihe->c10phy_sysreg));
		return PTR_ERR(zhihe->c10phy_sysreg);
	}

	dwc3_np = of_get_child_by_name(np, "dwc3");
	if (!dwc3_np) {
		dev_err(dev, "No DWC3 subnode found\n");
		return -ENODEV;
	}
	ret = of_address_to_resource(dwc3_np, 0, &dwc3_res);
	if (ret) {
		dev_err(dev, "failed to get subnode's resource\n");
		of_node_put(dwc3_np);
		dwc3_np = NULL;
		return ret;
	}
	of_node_put(dwc3_np);
	dwc3_np = NULL;

	zhihe->dwc3_ctrl = devm_ioremap(dev, dwc3_res.start, resource_size(&dwc3_res));
	if (IS_ERR(zhihe->dwc3_ctrl)) {
		dev_err(dev, "dwc3_ctrl has ERROR\n");
		return PTR_ERR(zhihe->dwc3_ctrl);
	}

	ret = of_platform_populate(np, NULL, NULL, dev);
	if (ret) {
		dev_err(dev, "failed to register dwc3 core - %d\n", ret);
		return ret;
	}

	zhihe->pwren = devm_gpiod_get_optional(&pdev->dev, "typec-pwren", GPIOD_OUT_LOW);
	if (IS_ERR(zhihe->pwren))
		zhihe->pwren = NULL;
	else if (zhihe->pwren)
		gpiod_set_value(zhihe->pwren, 1);
	else
		dev_info(dev, "Type-C power enable GPIO not defined in device tree\n");

	/* Update TX deemphasis parameters used in compliance mode, pattern 14 */
	writel(0x10540, zhihe->dwc3_ctrl + DWC3_LCSR_TX_DEEMPH_2);
	devm_release_region(dev, dwc3_res.start, resource_size(&dwc3_res));

	clk_disable(zhihe->ref_clk);
	clk_disable(zhihe->slv_aclk);
	reset_control_deassert(zhihe->usb31_arst);
	reset_control_deassert(zhihe->usb31_phy_rst);
	reset_control_deassert(zhihe->c10phy_rst);

	writel(0x11, zhihe->c10phy_tca + TCA_TCPC);
	writel(0xffff, zhihe->c10phy_tca + TCA_INTR_STS);

	reset_control_assert(zhihe->c10phy_rst);
	reset_control_assert(zhihe->usb31_phy_rst);
	reset_control_assert(zhihe->usb31_arst);
	clk_enable(zhihe->ref_clk);
	clk_enable(zhihe->slv_aclk);
	clk_enable(zhihe->cfg_aclk);

	dev_info(dev,"p100 dwc3-zhihe probe ok!\n");

	return 0;
}

static int dwc3_zhihe_remove(struct platform_device *pdev)
{
        struct dwc3_zhihe *zhihe = platform_get_drvdata(pdev);

        if (zhihe->dwc3_ctrl)
                devm_iounmap(&pdev->dev, zhihe->dwc3_ctrl);
        of_platform_depopulate(zhihe->dev);

        return 0;
}

static const struct of_device_id dwc3_zhihe_of_match[] = {
	{ .compatible = "zhihe,usb31" },
	{ },
};
MODULE_DEVICE_TABLE(of, dwc3_zhihe_of_match);

static struct platform_driver dwc3_zhihe_driver = {
	.probe		= dwc3_zhihe_probe,
	.remove         = dwc3_zhihe_remove,
	.driver		= {
		.name	= "dwc3-zhihe",
		.of_match_table	= dwc3_zhihe_of_match,
	},
};

module_platform_driver(dwc3_zhihe_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DesignWare DWC3 ZHIHE Glue Driver");
MODULE_AUTHOR("Anonymous <Anonymous@zhcomputing.com>");
