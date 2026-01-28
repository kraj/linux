// SPDX-License-Identifier: GPL-2.0

#include <linux/module.h>
#include <linux/bitfield.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_net.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include "stmmac_platform.h"

/* SYSCFG registers */
#define GMAC_CLKCTRL (0x00)
#define GMAC_CTRL    (0x04)
#define GMAC_ST      (0x08)
#define GMAC_TMI     (0x0C)
#define GMAC_PMI     (0x10)
#define GMAC_SBDI    (0x14)

/* SYSCFG value */
#define GMAC_CLKTRL_EN_ALL        0x1F
#define GMAC_CLKCTRL_SPEED_MASK  (0x3 << 8)
#define GMAC_CLKCTRL_1G          (0x0 << 8)
#define GMAC_CLKCTRL_10M         (0x2 << 8)
#define GMAC_CLKCTRL_100M        (0x3 << 8)

#define GMAC_CTRL_INTF_MASK 0xF
#define GMAC_CTRL_RGMII     0x1
#define GMAC_CTRL_RMII      0x4

struct zhihe_dwmac_ops {
	void (*set_speed)(struct plat_stmmacenet_data *plat_dat,
			    unsigned int speed);
	void (*enable_clk)(struct plat_stmmacenet_data *plat_dat);
};

struct zhihe_dwmac_priv_data {
	struct device *dev;
	struct regmap *sys_regmap;
	struct clk *gmac_aclk;
	struct clk *gmac_hclk;
	const struct zhihe_dwmac_ops *ops;
	struct plat_stmmacenet_data *plat_dat;
};

#define pm_debug \
	dev_dbg // for suspend/resume interface debug info show,replace to dev_info

static void zhihe_dwmac_set_speed(struct plat_stmmacenet_data *plat_dat,
					  unsigned int speed)
{
	struct zhihe_dwmac_priv_data *zhihe_plat_dat = plat_dat->bsp_priv;
	phy_interface_t interface = plat_dat->phy_interface;
	struct device *dev = zhihe_plat_dat->dev;
	unsigned int reg = 0;

	/* Configure mac speed */
	regmap_read(zhihe_plat_dat->sys_regmap, GMAC_CLKCTRL, &reg);
	switch(speed) {
	case SPEED_10:
		reg &= ~GMAC_CLKCTRL_SPEED_MASK;
		reg |= GMAC_CLKCTRL_10M;
		break;
	case SPEED_100:
		reg &= ~GMAC_CLKCTRL_SPEED_MASK;
		reg |= GMAC_CLKCTRL_100M;
		break;
	case SPEED_1000:
		reg &= ~GMAC_CLKCTRL_SPEED_MASK;
		reg |= GMAC_CLKCTRL_1G;
		break;
	default:
		dev_err(dev, "unsupported speed: %d\n", speed);
		return;
	}
	regmap_write(zhihe_plat_dat->sys_regmap, GMAC_CLKCTRL, reg);

	/* Configure phy interface */
	regmap_read(zhihe_plat_dat->sys_regmap, GMAC_CTRL, &reg);
	switch(interface) {
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
		reg &= ~GMAC_CTRL_INTF_MASK;
		reg |= GMAC_CTRL_RGMII;
		break;
	case PHY_INTERFACE_MODE_RMII:
		reg &= ~GMAC_CTRL_INTF_MASK;
		reg |= GMAC_CTRL_RMII;
		break;
	default:
		dev_err(dev, "unsupported phy interface: %d\n", interface);
		return;
	}
	regmap_write(zhihe_plat_dat->sys_regmap, GMAC_CTRL, reg);

	return;
}

static void zhihe_dwmac_enable_clk(struct plat_stmmacenet_data *plat_dat)
{
	struct zhihe_dwmac_priv_data *zhihe_plat_dat = plat_dat->bsp_priv;

	regmap_write(zhihe_plat_dat->sys_regmap, GMAC_CLKCTRL, GMAC_CLKTRL_EN_ALL);

	return;
}

static void zhihe_dwmac_fix_speed(void *priv, unsigned int speed,
				  unsigned int mode)
{
	struct zhihe_dwmac_priv_data *zhihe_plat_dat = priv;
	struct plat_stmmacenet_data *plat_dat = zhihe_plat_dat->plat_dat;

	if (zhihe_plat_dat->ops->set_speed)
		zhihe_plat_dat->ops->set_speed(plat_dat, speed);

	return;
}

/**
 * dwmac1000_validate_mcast_bins - validates the number of Multicast filter bins
 * @mcast_bins: Multicast filtering bins
 * Description:
 * this function validates the number of Multicast filtering bins specified
 * by the configuration through the device tree. The Synopsys GMAC supports
 * 64 bins, 128 bins, or 256 bins. "bins" refer to the division of CRC
 * number space. 64 bins correspond to 6 bits of the CRC, 128 corresponds
 * to 7 bits, and 256 refers to 8 bits of the CRC. Any other setting is
 * invalid and will cause the filtering algorithm to use Multicast
 * promiscuous mode.
 */
static int dwmac1000_validate_mcast_bins(int mcast_bins)
{
	int x = mcast_bins;

	switch (x) {
	case HASH_TABLE_SIZE:
	case 128:
	case 256:
		break;
	default:
		x = 0;
		pr_info("Hash table entries set to unexpected value %d",
			mcast_bins);
		break;
	}
	return x;
}

/**
 * dwmac1000_validate_ucast_entries - validate the Unicast address entries
 * @ucast_entries: number of Unicast address entries
 * Description:
 * This function validates the number of Unicast address entries supported
 * by a particular Synopsys 10/100/1000 controller. The Synopsys controller
 * supports 1..32, 64, or 128 Unicast filter entries for it's Unicast filter
 * logic. This function validates a valid, supported configuration is
 * selected, and defaults to 1 Unicast address if an unsupported
 * configuration is selected.
 */
static int dwmac1000_validate_ucast_entries(int ucast_entries)
{
	int x = ucast_entries;

	switch (x) {
	case 1 ... 32:
	case 64:
	case 128:
		break;
	default:
		x = 1;
		pr_info("Unicast table entries set to unexpected value %d\n",
			ucast_entries);
		break;
	}
	return x;
}

int zhihe_dwmac_clk_enable(struct platform_device *pdev, void *bsp_priv)
{
	struct zhihe_dwmac_priv_data *zhihe_plat_dat = bsp_priv;
	struct device *dev = &pdev->dev;
	int ret;

	pm_debug(dev, "enter %s()\n", __func__);
	ret = clk_prepare_enable(zhihe_plat_dat->gmac_aclk);
	if (ret) {
		dev_err(dev, "Failed to enable clk 'gmac_aclk'\n");
		return -1;
	}

	ret = clk_prepare_enable(zhihe_plat_dat->gmac_hclk);
	if (ret) {
		clk_disable_unprepare(zhihe_plat_dat->gmac_aclk);
		dev_err(dev, "Failed to enable clk 'gmac_hclk'\n");
		return -1;
	}

	return ret;
}

int zhihe_dwmac_clk_init(struct platform_device *pdev, void *bsp_priv)
{
	struct zhihe_dwmac_priv_data *zhihe_plat_dat = bsp_priv;
	struct device *dev = &pdev->dev;
	int ret = 0;
	unsigned int reg = 0;

	regmap_read(zhihe_plat_dat->sys_regmap, GMAC_CLKCTRL, &reg);
	reg &= ~GMAC_CLKTRL_EN_ALL;
	reg |= GMAC_CLKTRL_EN_ALL;
	regmap_write(zhihe_plat_dat->sys_regmap, GMAC_CLKCTRL, reg);

	pm_debug(dev, "enter %s()\n", __func__);

	return ret;
}

void zhihe_dwmac_clk_disable(struct platform_device *pdev, void *bsp_priv)
{
	struct zhihe_dwmac_priv_data *zhihe_plat_dat = bsp_priv;
	struct device *dev = &pdev->dev;
	pm_debug(dev, "enter %s()\n", __func__);

	clk_disable_unprepare(zhihe_plat_dat->gmac_aclk);
	clk_disable_unprepare(zhihe_plat_dat->gmac_hclk);

	return;
}

static int zhihe_dwmac_probe(struct platform_device *pdev)
{
	struct plat_stmmacenet_data *plat_dat;
	struct stmmac_resources stmmac_res;
	struct zhihe_dwmac_priv_data *zhihe_plat_dat;
	struct device *dev = &pdev->dev;
	const struct zhihe_dwmac_ops *data;
	struct device_node *np = pdev->dev.of_node;
	int ret;

	dma_set_mask(dev, DMA_BIT_MASK(32));
	zhihe_plat_dat = devm_kzalloc(dev, sizeof(*zhihe_plat_dat), GFP_KERNEL);
	if (zhihe_plat_dat == NULL) {
		dev_err(&pdev->dev, "allocate memory failed\n");
		return -ENOMEM;
	}

	ret = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (ret)
		return ret;

	if (pdev->dev.of_node) {
		plat_dat = stmmac_probe_config_dt(pdev, stmmac_res.mac);
		if (IS_ERR(plat_dat)) {
			dev_err(&pdev->dev, "dt configuration failed\n");
			return PTR_ERR(plat_dat);
		}

		data = of_device_get_match_data(&pdev->dev);
		if (!data) {
			dev_err(&pdev->dev, "failed to get match data\n");
			ret = -EINVAL;
			return ret;
		}

		zhihe_plat_dat->ops = data;
	} else {
		plat_dat = dev_get_platdata(&pdev->dev);
		if (!plat_dat) {
			dev_err(&pdev->dev, "no platform data provided\n");
			return -EINVAL;
		}

		/* Set default value for multicast hash bins */
		plat_dat->multicast_filter_bins = HASH_TABLE_SIZE;

		/* Set default value for unicast filter entries */
		plat_dat->unicast_filter_entries = 1;
	}

	/* populate bsp private data */
	zhihe_plat_dat->dev = &pdev->dev;
	plat_dat->bsp_priv = zhihe_plat_dat;
	plat_dat->fix_mac_speed = zhihe_dwmac_fix_speed;
	plat_dat->init = zhihe_dwmac_clk_init;

	of_property_read_u32(np, "max-frame-size", &plat_dat->maxmtu);
	of_property_read_u32(np, "snps,multicast-filter-bins",
			     &plat_dat->multicast_filter_bins);
	of_property_read_u32(np, "snps,perfect-filter-entries",
			     &plat_dat->unicast_filter_entries);
	plat_dat->unicast_filter_entries = dwmac1000_validate_ucast_entries(
		plat_dat->unicast_filter_entries);
	plat_dat->multicast_filter_bins =
		dwmac1000_validate_mcast_bins(plat_dat->multicast_filter_bins);

	zhihe_plat_dat->plat_dat = plat_dat;

	zhihe_plat_dat->sys_regmap = syscon_regmap_lookup_by_phandle(np, "zhihe,gmacsys");
	if (IS_ERR(zhihe_plat_dat->sys_regmap)) {
		ret = dev_err_probe(&pdev->dev, PTR_ERR(zhihe_plat_dat->sys_regmap),
				     "Failed to get gmac sysreg\n");
		goto err_remove_config_dt;
	}

	/* get gmac pll clk */
	zhihe_plat_dat->gmac_aclk = devm_clk_get(dev, "gmac_aclk");
	if (IS_ERR(zhihe_plat_dat->gmac_aclk)) {
		dev_err(dev, "gmac_aclk not exist, dts error\n");
		goto err_remove_config_dt;
	}
	zhihe_plat_dat->gmac_hclk = devm_clk_get(dev, "gmac_hclk");
	if (IS_ERR(zhihe_plat_dat->gmac_hclk)) {
		dev_err(dev, "gmac_hclk not exist, skipped it\n");
	}

	/* Custom initialisation (if needed) -- init clks*/
	if (plat_dat->init) {
		ret = plat_dat->init(pdev, plat_dat->bsp_priv);
		if (ret)
			goto err_exit;
	}

	ret = stmmac_dvr_probe(&pdev->dev, plat_dat, &stmmac_res);
	if (ret)
		goto err_exit;

	return 0;

err_exit:
	if (plat_dat->exit)
		plat_dat->exit(pdev, plat_dat->bsp_priv);
	zhihe_dwmac_clk_disable(pdev, plat_dat->bsp_priv);
err_remove_config_dt:
	if (pdev->dev.of_node)
		stmmac_remove_config_dt(pdev, plat_dat);

	return ret;
}

/**
 * stmmac_pltfr_suspend
 * @dev: device pointer
 * Description: this function is invoked when suspend the driver and it direcly
 * call the main suspend function and then, if required, on some platform, it
 * can call an exit helper.
 */
static int __maybe_unused zhihe_dwmac_suspend(struct device *dev)
{
	int ret;
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct platform_device *pdev = to_platform_device(dev);
	pm_debug(dev, "enter %s()\n", __func__);
	ret = stmmac_suspend(dev);
	if (priv->plat->exit)
		priv->plat->exit(pdev, priv->plat->bsp_priv);

	return ret;
}

/**
 * zhihe_dwmac_resume
 * @dev: device pointer
 * Description: this function is invoked when resume the driver before calling
 * the main resume function, on some platforms, it can call own init helper
 * if required.
 */
static int __maybe_unused zhihe_dwmac_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct platform_device *pdev = to_platform_device(dev);
	pm_debug(dev, "enter %s()\n", __func__);

	if (priv->plat->init)
		priv->plat->init(pdev, priv->plat->bsp_priv);

	return stmmac_resume(dev);
}

static int __maybe_unused zhihe_dwmac_runtime_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct platform_device *pdev = to_platform_device(dev);
	pm_debug(dev, "enter %s()\n", __func__);
	stmmac_bus_clks_config(priv, false);
	zhihe_dwmac_clk_disable(pdev, priv->plat->bsp_priv);
	return 0;
}

static int __maybe_unused zhihe_dwmac_runtime_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct platform_device *pdev = to_platform_device(dev);
	int ret;
	pm_debug(dev, "enter %s()\n", __func__);
	ret = stmmac_bus_clks_config(priv, true);
	if (ret)
		return ret;
	ret = zhihe_dwmac_clk_enable(pdev, priv->plat->bsp_priv);
	if (ret)
		return ret;
	return 0;
}

static int __maybe_unused zhihe_dwmac_noirq_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	int ret;
	pm_debug(dev, "enter %s()\n", __func__);
	if (!netif_running(ndev))
		return 0;

	if (!device_may_wakeup(priv->device) || !priv->plat->pmt) {
		/* Disable clock in case of PWM is off */
		clk_disable_unprepare(priv->plat->clk_ptp_ref);

		ret = pm_runtime_force_suspend(dev);
		if (ret)
			return ret;
	}

	return 0;
}

static int __maybe_unused zhihe_dwmac_noirq_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	int ret;
	pm_debug(dev, "enter %s()\n", __func__);
	if (!netif_running(ndev))
		return 0;

	if (!device_may_wakeup(priv->device) || !priv->plat->pmt) {
		/* enable the clk previously disabled */
		ret = pm_runtime_force_resume(dev);
		if (ret)
			return ret;

		ret = clk_prepare_enable(priv->plat->clk_ptp_ref);
		if (ret < 0) {
			netdev_warn(
				priv->dev,
				"failed to enable PTP reference clock: %pe\n",
				ERR_PTR(ret));
			return ret;
		}
	}

	return 0;
}

/*similar with stmmac_pltfr_pm_ops,but clks enable/disable add this drv need */
const struct dev_pm_ops zhihe_dwmac_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(zhihe_dwmac_suspend, zhihe_dwmac_resume)
		SET_RUNTIME_PM_OPS(zhihe_dwmac_runtime_suspend,
				   zhihe_dwmac_runtime_resume, NULL)
			SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(zhihe_dwmac_noirq_suspend,
						      zhihe_dwmac_noirq_resume)
};

static struct zhihe_dwmac_ops zhihe_dwmac_data = {
	.set_speed = zhihe_dwmac_set_speed,
	.enable_clk = zhihe_dwmac_enable_clk,
};

static const struct of_device_id zhihe_dwmac_match[] = {
	{ .compatible = "zhihe,a210-dwmac", .data = &zhihe_dwmac_data },
	{}
};
MODULE_DEVICE_TABLE(of, zhihe_dwmac_match);

static struct platform_driver zhihe_dwmac_driver = {
	.probe  = zhihe_dwmac_probe,
	.remove_new = stmmac_pltfr_remove,
	.driver = {
		.name	= "zhihe-dwmac",
		.pm		= &zhihe_dwmac_pm_ops,
		.of_match_table = of_match_ptr(zhihe_dwmac_match),
	},
};
module_platform_driver(zhihe_dwmac_driver);

MODULE_AUTHOR("ZHIHE");
MODULE_DESCRIPTION("ZHIHE dwmac platform driver");
MODULE_LICENSE("GPL");
