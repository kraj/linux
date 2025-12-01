// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe RC driver for zh P100
 *
 * Copyright (C) 2025 zh computing, Inc.
 *
 * Author: Ya.Huang
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/resource.h>
#include <linux/types.h>
#include <linux/reset.h>
#include <linux/gpio/consumer.h>

#include "pcie-designware.h"

#define PCIE_X4_TYPE				0x00000000
#define PCIE_X1_TYPE				0x00000001

#define PCIE_GEN3X4_DBI_BADDR			0x0b000000
#define PCIE_GEN3X4_CTRL_REG			0x00000000
#define PCIE_GEN3X4_DBG_INFO_REG0		0x00000430

#define E16PHY_GLB_CTRL_REG			0x00000000
#define E16PHY_SRC_SEL_REG			0x00000004
#define E16PHY_PROTLCOL_REG			0x00000008
#define E16PHY_RES_RTURN_REG			0x00000048

#define PORT_LINK_CTRL_OFF			0x00000710
#define LANE_SKEW_OFF				0x00000714
#define GEN2_CTRL_OFF				0x0000080c
#define GEN3_EQ_CONTROL_OFF			0x000008a8
#define GEN3_EQ_FB_MODE_DIR_CHANGE_OFF		0x000008ac

#define PCIE_EXTENDED_REG0			0x00000154
#define PCIE_EXTENDED_REG1			0x00000158
#define PCIE_EXTENDED_REG2			0x0000015c
#define PCIE_EXTENDED_REG3			0x00000160

#define TYPE1_STATUS_COMMAND_REG		0x00000004
#define DBI2_BAR0_REG				0x00100010
#define DBI2_BAR1_REG				0x00100014
#define SEC_STAT_IO_LIMIT_IO_BASE_REG		0x0000001c
#define MEM_LIMIT_MEM_BASE_REG			0x00000020
#define PREF_MEM_LIMIT_PREF_MEM_BASE_REG	0x00000024
#define PREF_BASE_UPPER_REG			0x00000028
#define PREF_LIMIT_UPPER_REG			0x0000002c
#define IO_LIMIT_UPPER_IO_BASE_UPPER_REG	0x00000030
#define TRGT_MAP_CTRL_OFF			0x0000081c

#define DEVICE_CONTROL_DEVICE_STATUS		0x00000078
#define LINK_CONTROL2_LINK_STATUS2_REG		0x000000a0

#define LINK_UP_IS_OK	0x11

struct p100_plat_pcie {
	struct dw_pcie			*pci;
	enum dw_pcie_device_mode	mode;
	unsigned int			ip_type;
	void __iomem			*apb_base;
	void __iomem			*wrap_base;
	void __iomem			*phy_base;
	void __iomem			*cpr_base;
	struct reset_control		*pcie_rst;
	struct reset_control		*pcie_prst;
	struct gpio_desc		*pcie_bat_en;
	struct gpio_desc		*pcie_3v3_en;
	struct gpio_desc		*pcie_12v_en;
	struct gpio_desc		*pcie_clk_en;
	struct gpio_desc		*minipcie_1v5_pwren;
	struct gpio_desc		*minipcie_3v3_pwren;
	struct gpio_desc		*minipcie_perst;
	struct gpio_desc		*pcie_clk_pwren;
};

struct p100_plat_pcie_of_data {
	enum dw_pcie_device_mode	mode;
};

static const struct dw_pcie_host_ops p100_plat_pcie_host_ops = {
};

static inline int p100_pcie_readl_apb(struct p100_plat_pcie *p100_plat_pcie,
					     u32 reg)
{
	return readl(p100_plat_pcie->apb_base + reg);
}

static inline void p100_pcie_writel_apb(struct p100_plat_pcie *p100_plat_pcie,
						u32 reg, u32 val)
{
	writel(val, p100_plat_pcie->apb_base + reg);
}

static inline int p100_pcie_readl_wrap(struct p100_plat_pcie *p100_plat_pcie,
					     u32 reg)
{
	return readl(p100_plat_pcie->wrap_base + reg);
}

static inline void p100_pcie_writel_wrap(struct p100_plat_pcie *p100_plat_pcie,
						u32 reg, u32 val)
{
	writel(val, p100_plat_pcie->wrap_base + reg);
}

static inline int p100_pcie_readl_phy(struct p100_plat_pcie *p100_plat_pcie,
					     u32 reg)
{
	return readl(p100_plat_pcie->phy_base + reg);
}

static inline void p100_pcie_writel_phy(struct p100_plat_pcie *p100_plat_pcie,
						u32 reg, u32 val)
{
	writel(val, p100_plat_pcie->phy_base + reg);
}

static inline int p100_pcie_readl_cpr(struct p100_plat_pcie *p100_plat_pcie,
					     u32 reg)
{
	return readl(p100_plat_pcie->cpr_base + reg);
}

static inline void p100_pcie_writel_cpr(struct p100_plat_pcie *p100_plat_pcie,
						u32 reg, u32 val)
{
	writel(val, p100_plat_pcie->cpr_base + reg);
}

static void p100_pcie_ipctrl_init(struct p100_plat_pcie *p100_plat_pcie)
{
	int rdata = 0;

	/*disable ltssm*/
	p100_pcie_writel_wrap(p100_plat_pcie, PCIE_GEN3X4_CTRL_REG, 0x1014);
	/*enable rp && sata*/
	if (p100_plat_pcie->ip_type == PCIE_X1_TYPE) {
		p100_pcie_writel_phy(p100_plat_pcie, E16PHY_GLB_CTRL_REG, 0x110101);
		p100_pcie_writel_phy(p100_plat_pcie, E16PHY_SRC_SEL_REG, 0x1100);
		p100_pcie_writel_phy(p100_plat_pcie, E16PHY_PROTLCOL_REG, 0x2200);
	}
	/*phy deassert*/
	if (p100_plat_pcie->ip_type == PCIE_X4_TYPE) {
		reset_control_deassert(p100_plat_pcie->pcie_rst);
		reset_control_deassert(p100_plat_pcie->pcie_prst);
	}
	/*resistor tune request && res_ack_in*/
	p100_pcie_writel_phy(p100_plat_pcie, E16PHY_RES_RTURN_REG, 0x10001);
	if (p100_plat_pcie->ip_type == PCIE_X4_TYPE) {
		/*cfg x4 lane*/
		p100_pcie_writel_apb(p100_plat_pcie, PORT_LINK_CTRL_OFF, 0x70120);
		p100_pcie_writel_apb(p100_plat_pcie, LANE_SKEW_OFF, 0x1c000000);
		p100_pcie_writel_apb(p100_plat_pcie, GEN3_EQ_CONTROL_OFF, 0xc020071);
		p100_pcie_writel_apb(p100_plat_pcie, GEN2_CTRL_OFF, 0x304be);
	} else {
		/*cfg x1 lane*/
		p100_pcie_writel_apb(p100_plat_pcie, PORT_LINK_CTRL_OFF, 0x10120);
		p100_pcie_writel_apb(p100_plat_pcie, LANE_SKEW_OFF, 0x4000000);
		p100_pcie_writel_apb(p100_plat_pcie, GEN3_EQ_CONTROL_OFF, 0xc020071);
		p100_pcie_writel_apb(p100_plat_pcie, GEN2_CTRL_OFF, 0x101be);
	}
	/*ip ctrl cfg*/
	p100_pcie_writel_apb(p100_plat_pcie, GEN3_RELATED_OFF, 0x2000);
	p100_pcie_writel_apb(p100_plat_pcie, GEN3_RELATED_OFF, 0x2a00);
	p100_pcie_writel_apb(p100_plat_pcie, PCIE_EXTENDED_REG0, 0x21614536);
	p100_pcie_writel_apb(p100_plat_pcie, PCIE_EXTENDED_REG1, 0x6337451);
	p100_pcie_writel_apb(p100_plat_pcie, PCIE_EXTENDED_REG2, 0x8553824);
	p100_pcie_writel_apb(p100_plat_pcie, PCIE_EXTENDED_REG3, 0x47373650);
	p100_pcie_writel_apb(p100_plat_pcie, GEN3_EQ_FB_MODE_DIR_CHANGE_OFF, 0x0);
	p100_pcie_writel_apb(p100_plat_pcie, DEVICE_CONTROL_DEVICE_STATUS, 0x2130);
	/*cfg Gen3*/
	rdata = p100_pcie_readl_apb(p100_plat_pcie, LINK_CONTROL2_LINK_STATUS2_REG);
	rdata &= 0xfffffff0;
	rdata |= 0x3;
	p100_pcie_writel_apb(p100_plat_pcie, LINK_CONTROL2_LINK_STATUS2_REG, rdata);
	/*config space setup*/
	p100_pcie_writel_apb(p100_plat_pcie, DBI2_BAR0_REG, 0x0);
	p100_pcie_writel_apb(p100_plat_pcie, DBI2_BAR1_REG, 0x0);
	p100_pcie_writel_apb(p100_plat_pcie, TRGT_MAP_CTRL_OFF, 0x40);
	p100_pcie_writel_apb(p100_plat_pcie, SEC_STAT_IO_LIMIT_IO_BASE_REG, 0x4f40);
	p100_pcie_writel_apb(p100_plat_pcie, IO_LIMIT_UPPER_IO_BASE_UPPER_REG, 0x0);
	p100_pcie_writel_apb(p100_plat_pcie, MEM_LIMIT_MEM_BASE_REG, 0xc91fc800);
	p100_pcie_writel_apb(p100_plat_pcie, PREF_MEM_LIMIT_PREF_MEM_BASE_REG, 0xfff0);
	p100_pcie_writel_apb(p100_plat_pcie, PREF_BASE_UPPER_REG, 0x0);
	p100_pcie_writel_apb(p100_plat_pcie, PREF_LIMIT_UPPER_REG, 0x0);
	p100_pcie_writel_apb(p100_plat_pcie, TYPE1_STATUS_COMMAND_REG, 0x100007);
	/*enable ltssm*/
	p100_pcie_writel_wrap(p100_plat_pcie, PCIE_GEN3X4_CTRL_REG, 0x1114);
}

static int p100_plat_add_pcie_port(struct p100_plat_pcie *p100_plat_pcie,
				   struct platform_device *pdev)
{
	struct dw_pcie *pci = p100_plat_pcie->pci;
	struct dw_pcie_rp *pp = &pci->pp;
	struct device *dev = &pdev->dev;
	int ret;

	pp->irq = platform_get_irq(pdev, 0);
	if (pp->irq < 0)
		return pp->irq;

	pp->num_vectors = MAX_MSI_IRQS;
	pp->ops = &p100_plat_pcie_host_ops;

	ret = dw_pcie_host_init(pp);
	if (ret) {
		dev_err(dev, "Failed to initialize host\n");
		return ret;
	}

	return 0;
}

static void p100_pcie_wait_linkup(struct platform_device *pdev,struct p100_plat_pcie *p100_plat_pcie)
{
	u32 ltssm_stat = 0;
	unsigned long cnt = 0;
	unsigned int TIME_OUT_CNT = 20;
	unsigned int DELAY_MS = 50;

	do {
		mdelay(DELAY_MS);
		ltssm_stat = p100_pcie_readl_wrap(p100_plat_pcie, \
						PCIE_GEN3X4_DBG_INFO_REG0);
		ltssm_stat &= 0x3f;
		if (ltssm_stat == LINK_UP_IS_OK) {
			dev_info(&pdev->dev, "ltssm:link up ok!\n");
			break;
		}
		if (cnt > TIME_OUT_CNT) {
			dev_err(&pdev->dev, "ltssm_stat = 0x%x,link up fail!\n",ltssm_stat);
			break;
		}
		cnt++;
	} while (ltssm_stat != LINK_UP_IS_OK);
}

static int p100_pcie_get_resource(struct platform_device *pdev,
				    struct p100_plat_pcie *p100_plat_pcie)
{
	struct resource *res;
	static void __iomem *ep16phy_base;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get PCIe DBI resource\n");
		return -ENODEV;
	}

	if(res->start == PCIE_GEN3X4_DBI_BADDR)
		p100_plat_pcie->ip_type = PCIE_X4_TYPE;
	else
		p100_plat_pcie->ip_type = PCIE_X1_TYPE;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "apb");
	p100_plat_pcie->apb_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(p100_plat_pcie->apb_base))
		return PTR_ERR(p100_plat_pcie->apb_base);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "wrap_sysreg");
	p100_plat_pcie->wrap_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(p100_plat_pcie->wrap_base))
		return PTR_ERR(p100_plat_pcie->wrap_base);

	if (p100_plat_pcie->ip_type == PCIE_X4_TYPE) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "phy_sysreg");
		p100_plat_pcie->phy_base = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(p100_plat_pcie->phy_base))
			return PTR_ERR(p100_plat_pcie->phy_base);
		ep16phy_base = p100_plat_pcie->phy_base;
	}

	if (p100_plat_pcie->ip_type == PCIE_X1_TYPE)
		p100_plat_pcie->phy_base = ep16phy_base;

	if (p100_plat_pcie->ip_type == PCIE_X4_TYPE) {
		p100_plat_pcie->pcie_rst = devm_reset_control_get_shared(&pdev->dev, "pcie-rst");
		if(IS_ERR(p100_plat_pcie->pcie_rst))
			return PTR_ERR(p100_plat_pcie->pcie_rst);
		p100_plat_pcie->pcie_prst = devm_reset_control_get_shared(&pdev->dev, "pcie-prst");
		if(IS_ERR(p100_plat_pcie->pcie_prst))
			return PTR_ERR(p100_plat_pcie->pcie_prst);
	}

	return 0;
}

static void p100_pcie_ltssm_disable(struct p100_plat_pcie *p100_plat_pcie)
{
	p100_pcie_writel_wrap(p100_plat_pcie, PCIE_GEN3X4_CTRL_REG, 0x1014);
}

static void p100_pcie_stop_link(struct dw_pcie *pci)
{
	struct p100_plat_pcie *p100_plat_pcie = dev_get_drvdata(pci->dev);

	p100_pcie_ltssm_disable(p100_plat_pcie);
}

static const struct dw_pcie_ops dw_pcie_ops = {
	.stop_link = p100_pcie_stop_link,
};

static int p100_plat_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct p100_plat_pcie *p100_plat_pcie;
	struct dw_pcie *pci;
	int ret;
	const struct p100_plat_pcie_of_data *data;
	enum dw_pcie_device_mode mode;

	dev_info(dev, "Enter into zh_pci platform!\n");

	data = of_device_get_match_data(dev);
	if (!data)
		return -EINVAL;
	mode = (enum dw_pcie_device_mode)data->mode;

	p100_plat_pcie = devm_kzalloc(dev, sizeof(*p100_plat_pcie), GFP_KERNEL);
	if (!p100_plat_pcie)
		return -ENOMEM;

	pci = devm_kzalloc(dev, sizeof(*pci), GFP_KERNEL);
	if (!pci)
		return -ENOMEM;

	pci->dev = dev;
	p100_plat_pcie->pci = pci;
	p100_plat_pcie->mode = mode;

	p100_plat_pcie->pci->ops = &dw_pcie_ops;
	ret = p100_pcie_get_resource(pdev, p100_plat_pcie);
	if (ret)
		return ret;

	p100_pcie_ipctrl_init(p100_plat_pcie);
	p100_pcie_wait_linkup(pdev, p100_plat_pcie);

	platform_set_drvdata(pdev, p100_plat_pcie);
	switch (p100_plat_pcie->mode) {
	case DW_PCIE_RC_TYPE:
		/* Get GPIO descriptors for PCIe power control */
		p100_plat_pcie->pcie_bat_en = devm_gpiod_get_optional(&pdev->dev,
								      "pcie-bat-en",
								      GPIOD_OUT_LOW);
		if (IS_ERR(p100_plat_pcie->pcie_bat_en)) {
			dev_err(&pdev->dev, "Failed to get pcie-bat-en GPIO\n");
			return PTR_ERR(p100_plat_pcie->pcie_bat_en);
		}

		p100_plat_pcie->pcie_3v3_en = devm_gpiod_get_optional(&pdev->dev,
								      "pcie-3v3-en",
								      GPIOD_OUT_LOW);
		if (IS_ERR(p100_plat_pcie->pcie_3v3_en)) {
			dev_err(&pdev->dev, "Failed to get pcie-3v3-en GPIO\n");
			return PTR_ERR(p100_plat_pcie->pcie_3v3_en);
		}

		p100_plat_pcie->pcie_12v_en = devm_gpiod_get_optional(&pdev->dev,
								      "pcie-12v-en",
								      GPIOD_OUT_LOW);
		if (IS_ERR(p100_plat_pcie->pcie_12v_en)) {
			dev_err(&pdev->dev, "Failed to get pcie-12v-en GPIO\n");
			return PTR_ERR(p100_plat_pcie->pcie_12v_en);
		}

		p100_plat_pcie->pcie_clk_en = devm_gpiod_get_optional(&pdev->dev,
								      "pcie-clk-en",
								      GPIOD_OUT_LOW);
		if (IS_ERR(p100_plat_pcie->pcie_clk_en)) {
			dev_err(&pdev->dev, "Failed to get pcie-clk-en GPIO\n");
			return PTR_ERR(p100_plat_pcie->pcie_clk_en);
		}

		if (p100_plat_pcie->pcie_bat_en)
			gpiod_set_value(p100_plat_pcie->pcie_bat_en, 1);

                if (p100_plat_pcie->pcie_3v3_en)
			gpiod_set_value(p100_plat_pcie->pcie_3v3_en, 1);

		if (p100_plat_pcie->pcie_12v_en)
			gpiod_set_value(p100_plat_pcie->pcie_12v_en, 1);

		if (p100_plat_pcie->pcie_clk_en)
			gpiod_set_value(p100_plat_pcie->pcie_clk_en, 1);

		p100_plat_pcie->minipcie_1v5_pwren = devm_gpiod_get_optional(&pdev->dev,
								      "minipcie-1v5-pwren",
								      GPIOD_OUT_LOW);
		if (IS_ERR(p100_plat_pcie->minipcie_1v5_pwren)) {
			dev_err(&pdev->dev, "Failed to get minipcie-1v5-pwren GPIO\n");
			return PTR_ERR(p100_plat_pcie->minipcie_1v5_pwren);
		}

		if (p100_plat_pcie->minipcie_1v5_pwren)
			gpiod_set_value(p100_plat_pcie->minipcie_1v5_pwren, 1);

		p100_plat_pcie->minipcie_3v3_pwren = devm_gpiod_get_optional(&pdev->dev,
								      "minipcie-3v3-pwren",
								      GPIOD_OUT_LOW);
		if (IS_ERR(p100_plat_pcie->minipcie_3v3_pwren)) {
			dev_err(&pdev->dev, "Failed to get minipcie-3v3-pwren GPIO\n");
			return PTR_ERR(p100_plat_pcie->minipcie_3v3_pwren);
		}

		if (p100_plat_pcie->minipcie_3v3_pwren)
			gpiod_set_value(p100_plat_pcie->minipcie_3v3_pwren, 1);

		p100_plat_pcie->minipcie_perst = devm_gpiod_get_optional(&pdev->dev,
								      "minipcie-perst",
								      GPIOD_OUT_LOW);
		if (IS_ERR(p100_plat_pcie->minipcie_perst)) {
			dev_err(&pdev->dev, "Failed to get minipcie-perst GPIO\n");
			return PTR_ERR(p100_plat_pcie->minipcie_perst);
		}

		if (p100_plat_pcie->minipcie_perst)
			gpiod_set_value(p100_plat_pcie->minipcie_perst, 1);

		p100_plat_pcie->pcie_clk_pwren = devm_gpiod_get_optional(&pdev->dev,
								      "pcie-clk-pwren",
								      GPIOD_OUT_LOW);
		if (IS_ERR(p100_plat_pcie->pcie_clk_pwren)) {
			dev_err(&pdev->dev, "Failed to get pcie-clk-pwren GPIO\n");
			return PTR_ERR(p100_plat_pcie->pcie_clk_pwren);
		}

		if (p100_plat_pcie->pcie_clk_pwren)
			gpiod_set_value(p100_plat_pcie->pcie_clk_pwren, 1);
		p100_plat_pcie->pci->dbi_base = p100_plat_pcie->apb_base;

		ret = p100_plat_add_pcie_port(p100_plat_pcie, pdev);
		if (ret < 0)
			return ret;
		break;
	default:
		dev_err(dev, "INVALID device type %d\n", p100_plat_pcie->mode);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int p100_plat_pcie_remove(struct platform_device *pdev)
{
	struct p100_plat_pcie *p100_plat_pcie = platform_get_drvdata(pdev);

	dw_pcie_host_deinit(&p100_plat_pcie->pci->pp);

	reset_control_assert(p100_plat_pcie->pcie_rst);
	reset_control_assert(p100_plat_pcie->pcie_prst);

	return 0;
}

static const struct p100_plat_pcie_of_data p100_plat_pcie_rc_of_data = {
	.mode = DW_PCIE_RC_TYPE,
};

static const struct of_device_id p100_plat_pcie_of_match[] = {
	{
		.compatible = "zh,p100-pcie",
		.data = &p100_plat_pcie_rc_of_data,
	},
	{},
};

static struct platform_driver p100_plat_pcie_driver = {
	.driver = {
		.name	= "zh-pcie",
		.of_match_table = p100_plat_pcie_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = p100_plat_pcie_probe,
	.remove = p100_plat_pcie_remove,
};
module_platform_driver(p100_plat_pcie_driver);
MODULE_LICENSE("GPL v2");
