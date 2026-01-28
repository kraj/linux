// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe RC driver for A210
 *
 * Copyright (C) 2025 zhihe computing, Inc.
 *
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/pci.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/resource.h>
#include <linux/types.h>
#include <linux/reset.h>

#include "pcie-designware.h"

#define APP_PCIE_GEN3X4_CTRL_REG         0x000
#define DEVICE_TYPE_EP     0
#define DEVICE_TYPE_RP     BIT(2)
#define DEVICE_TYPE_SYSREG BIT(4)
#define APP_LTSSM_ENABLE   BIT(8)
#define APP_DEVICE_TYPE_MASK GENMASK(3, 0)

#define APP_PCIE_GEN3X4_LINK_STS_REG     0x034
#define RDLH_LINK_UP   BIT(0)
#define SMLH_LINK_UP   BIT(4)
#define SMLH_LTSM_STATE_MASK GENMASK(25, 20)
#define SMLH_LTSM_S_L0 BIT(20) | BIT(24)

#define APP_PCIE_GEN3X4_DBG_INFO_REG0    0x430

#define PCIE_EXTENDED_REG0               0x00000154
#define PCIE_EXTENDED_REG1               0x00000158
#define PCIE_EXTENDED_REG2               0x0000015c
#define PCIE_EXTENDED_REG3               0x00000160

#define TYPE1_STATUS_COMMAND_REG         0x00000004
#define SEC_STAT_IO_LIMIT_IO_BASE_REG    0x0000001c
#define MEM_LIMIT_MEM_BASE_REG           0x00000020
#define PREF_MEM_LIMIT_PREF_MEM_BASE_REG 0x00000024
#define PREF_BASE_UPPER_REG              0x00000028
#define PREF_LIMIT_UPPER_REG             0x0000002c
#define IO_LIMIT_UPPER_IO_BASE_UPPER_REG 0x00000030
#define TRGT_MAP_CTRL_OFF                0x0000081c

#define DEVICE_CONTROL_DEVICE_STATUS     0x00000078
#define LINK_CONTROL2_LINK_STATUS2_REG   0x000000a0

struct zhihe_pcie {
	struct dw_pcie pci;
	enum dw_pcie_device_mode mode;
	void __iomem *app_base;
	struct phy *phy;
	u32 cap_base;
};

#define to_zhihe_pcie(x)	(struct zhihe_pcie*)dev_get_drvdata((x)->dev)

struct zhihe_pcie_of_data {
	enum dw_pcie_device_mode	mode;
};

static void pcie_update_bits(void __iomem *base, u32 ofs, u32 mask, u32 val)
{
	u32 old;

	old = readl(base + ofs);
	val = (old & ~mask) | (val & mask);

	if (val != old)
		writel(val, base + ofs);
}

static inline int pcie_app_rd(struct zhihe_pcie *pcie, u32 reg)
{
	return readl(pcie->app_base + reg);
}

static inline void pcie_app_wr(struct zhihe_pcie *pcie, u32 reg, u32 val)
{
	writel(val, pcie->app_base + reg);
}

static void pcie_app_wr_mask(struct zhihe_pcie *pcie, u32 ofs,
			     u32 mask, u32 val)
{
	pcie_update_bits(pcie->app_base, ofs, mask, val);
}

static void __maybe_unused pcie_dump_mmio(struct device *dev, void __iomem *start,
			unsigned int bytes)
{
	unsigned int b, w, o, offset = 0;
	unsigned char linebuf[38];

	for (b = 0; b < bytes;) {
		for (w = 0, o = 0; b < bytes && w < 4; w++) {
			o += scnprintf(linebuf + o, sizeof(linebuf) - o,
				       "%08x ", readl(start + b));
			b += sizeof(u32);
		}
		dev_info(dev, "%03x: %s\n", offset, linebuf);
		offset += w * sizeof(u32);
	}
}

static void zhihe_pcie_set_device_type(struct zhihe_pcie *pcie, u32 type)
{
	pcie_app_wr_mask(pcie, APP_PCIE_GEN3X4_CTRL_REG,
			DEVICE_TYPE_SYSREG | APP_DEVICE_TYPE_MASK,
			DEVICE_TYPE_SYSREG | type);
}

static void zhihe_pcie_ltssm_enable(struct zhihe_pcie *pcie)
{
	pcie_app_wr_mask(pcie, APP_PCIE_GEN3X4_CTRL_REG, APP_LTSSM_ENABLE, APP_LTSSM_ENABLE);
}

static void zhihe_pcie_ltssm_disable(struct zhihe_pcie *pcie)
{
	pcie_app_wr_mask(pcie, APP_PCIE_GEN3X4_CTRL_REG, APP_LTSSM_ENABLE, 0);
}

static int zhihe_pcie_wait_linkup(struct zhihe_pcie *pcie)
{
	u32 ltssm_stat;
	int count = 20;

	do {
		ltssm_stat = pcie_app_rd(pcie, APP_PCIE_GEN3X4_LINK_STS_REG);
		if (ltssm_stat && (SMLH_LTSM_S_L0 | SMLH_LINK_UP | RDLH_LINK_UP)) {
			dev_info(pcie->pci.dev, "ltssm:link up ok!\n");
			break;
		}
		mdelay(50);
	} while (count--);

	if (count < 0) {
		dev_err(pcie->pci.dev, "ltssm_stat = 0x%x,link up fail!\n",ltssm_stat);
		pcie_dump_mmio(pcie->pci.dev, pcie->app_base, 0x490);
		return -ETIMEDOUT;
	}

	return 0;
}


static int zhihe_pcie_get_resources(struct platform_device *pdev,
				    struct zhihe_pcie *pcie)
{
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get PCIe DBI resource\n");
		return -ENODEV;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "pcie_sysreg");
	pcie->app_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pcie->app_base))
		return dev_err_probe(&pdev->dev, PTR_ERR(pcie->app_base),
				"Failed to get pcie_sysreg resource");

	return 0;
}

static int __maybe_unused zhihe_pcie_phy_init(struct zhihe_pcie *pcie)
{
	struct device *dev = pcie->pci.dev;
	int ret;

	pcie->phy = devm_phy_get(dev, "pcie-phy");
	if (IS_ERR(pcie->phy))
		return dev_err_probe(dev, PTR_ERR(pcie->phy),
				     "missing PHY\n");

	ret = phy_init(pcie->phy);
	if (ret)
		return ret;

	ret = phy_set_mode_ext(pcie->phy, PHY_MODE_PCIE, pcie->mode);
	if (ret) {
		phy_exit(pcie->phy);
		return ret;
	}

	ret = phy_power_on(pcie->phy);
	if (ret)
		phy_exit(pcie->phy);

	return ret;
}

static void __maybe_unused zhihe_pcie_phy_deinit(struct zhihe_pcie *pcie)
{
	phy_exit(pcie->phy);
	phy_power_off(pcie->phy);
}

static int zhihe_pcie_start_link(struct dw_pcie *pci)
{
	struct zhihe_pcie *pcie = to_zhihe_pcie(pci);
	zhihe_pcie_ltssm_enable(pcie);
	return zhihe_pcie_wait_linkup(pcie);
}

static void zhihe_pcie_stop_link(struct dw_pcie *pci)
{
	struct zhihe_pcie *pcie = to_zhihe_pcie(pci);
	zhihe_pcie_ltssm_disable(pcie);
}

static const struct dw_pcie_ops dw_pcie_ops = {
	.start_link = zhihe_pcie_start_link,
	.stop_link = zhihe_pcie_stop_link,
};

static int zhihe_pcie_host_init(struct dw_pcie_rp *pp)
{
	u32 val;
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct zhihe_pcie *pcie = to_zhihe_pcie(pci);

	int ret = zhihe_pcie_phy_init(pcie);
	if (ret)
		return ret;

	/*disable ltssm*/
	zhihe_pcie_ltssm_disable(pcie);
	/* Set RP mode */
	zhihe_pcie_set_device_type(pcie, DEVICE_TYPE_RP);

	if (!pcie->cap_base)
		pcie->cap_base = dw_pcie_find_capability(&pcie->pci, PCI_CAP_ID_EXP);

	/* Disable BARs for inbound access */
	dw_pcie_writel_dbi(pci, PCI_BASE_ADDRESS_0, 0);
	dw_pcie_writel_dbi(pci, PCI_BASE_ADDRESS_1, 0);

	/* Configure Max lane width from DT */
	dw_pcie_dbi_ro_wr_en(pci);
	val = dw_pcie_readl_dbi(pci, pcie->cap_base + PCI_EXP_LNKCAP);
	val &= ~PCI_EXP_LNKCAP_MLW;
	val |= (pci->num_lanes << PCI_EXP_LNKSTA_NLW_SHIFT);
	dw_pcie_writel_dbi(pci, pcie->cap_base + PCI_EXP_LNKCAP, val);
	dw_pcie_dbi_ro_wr_dis(pci);

	val = dw_pcie_readl_dbi(pci, PCIE_PORT_LANE_SKEW);
	val &= ~PORT_LANE_SKEW_INSERT_MASK;
	if (pci->num_lanes < 4)
		val |= BIT(6);
	dw_pcie_writel_dbi(pci, PCIE_PORT_LANE_SKEW, val);

	val = dw_pcie_readl_dbi(pci, GEN3_RELATED_OFF);
	val |= GEN3_RELATED_OFF_EQ_PHASE_2_3;
	dw_pcie_writel_dbi(pci, GEN3_RELATED_OFF, val);
	dw_pcie_writel_dbi(pci, GEN3_EQ_CONTROL_OFF, 0xc020071);

	return 0;
}

static const struct dw_pcie_host_ops zhihe_pcie_host_ops = {
	.host_init = zhihe_pcie_host_init,
};

static int zhihe_add_pcie_port(struct zhihe_pcie *pcie, struct platform_device *pdev)
{
	struct dw_pcie *pci = &pcie->pci;
	struct dw_pcie_rp *pp = &pci->pp;
	struct device *dev = &pdev->dev;
	int ret;

	pp->irq = platform_get_irq(pdev, 0);
	if (pp->irq < 0)
		return pp->irq;

	pp->num_vectors = MAX_MSI_IRQS;
	pp->ops = &zhihe_pcie_host_ops;

	ret = dw_pcie_host_init(pp);
	if (ret) {
		dev_err(dev, "Failed to initialize host\n");
		return ret;
	}

	return 0;
}

static int zhihe_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct zhihe_pcie *pcie;
	struct dw_pcie *pci;
	int ret;

	const struct zhihe_pcie_of_data *data;
	enum dw_pcie_device_mode mode;

	dev_info(dev, "Enter into zh_pci platform!\n");

	data = of_device_get_match_data(dev);
	if (!data)
		goto deinit_phy;

	pcie = devm_kzalloc(dev, sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	pci = &pcie->pci;
	pci->dev = dev;
	mode = pcie->mode =(enum dw_pcie_device_mode)data->mode;;
	pci->ops = &dw_pcie_ops;

	platform_set_drvdata(pdev, pcie);

	ret = zhihe_pcie_get_resources(pdev, pcie);
	if (ret)
		return ret;

	switch (pcie->mode) {
	case DW_PCIE_RC_TYPE:
		ret = zhihe_add_pcie_port(pcie, pdev);
		break;
	default:
		dev_err(dev, "Invalid device type %d\n", pcie->mode);
		ret = -ENODEV;
		break;
	}
	if (!ret)
		return 0;

deinit_phy:
	zhihe_pcie_phy_deinit(pcie);
	return ret;
}

static int zhihe_pcie_remove(struct platform_device *pdev)
{
	struct zhihe_pcie *pcie = platform_get_drvdata(pdev);

	dw_pcie_host_deinit(&pcie->pci.pp);

	return 0;
}

static const struct zhihe_pcie_of_data zhihe_pcie_rc_of_data = {
	.mode = DW_PCIE_RC_TYPE,
};

static const struct of_device_id zhihe_pcie_of_match[] = {
	{
		.compatible = "zhihe,a210-pcie",
		.data = &zhihe_pcie_rc_of_data,
	},
	{},
};

static struct platform_driver zhihe_pcie_driver = {
	.driver = {
		.name	= "zh-pcie",
		.of_match_table = zhihe_pcie_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = zhihe_pcie_probe,
	.remove = zhihe_pcie_remove,
};
module_platform_driver(zhihe_pcie_driver);

MODULE_DEVICE_TABLE(of, zhihe_pcie_of_match);
MODULE_LICENSE("GPL v2");
