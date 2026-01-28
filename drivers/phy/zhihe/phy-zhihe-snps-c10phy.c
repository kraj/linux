// SPDX-License-Identifier: GPL-2.0
/*
 * phy-zhihe_c10phy.c - ZHIHE C10PHY USB3.1 PHY driver
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio/consumer.h>
#include <linux/usb/typec_mux.h>
#include <linux/usb/typec_dp.h>
#include <linux/gpio/consumer.h>
#include <linux/extcon-provider.h>
#include <linux/usb/role.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>

/* Base DWC3 Control Register */
#define DWC3_LCSR_TX_DEEMPH_2	0xd068

/*
 * DPTX_SYS Register Offsets - From soc sys databook 
 */
#define DPTX_CTRL       0x00
#define DPTX_LSFR_CTRL  0x10
#define DPTX_LSFR_SEED  0x14
#define DPTX_MTN_LINK   0x20
#define DPTX_AUX_CTRL   0x40
#define DPTX_DBG0       0x100
#define DPTX_DBG1       0x104

/* DPTX_AUX_CTRL bits*/
#define AUX_DP_DN_SWAP  BIT(8)

/*
 * TCA Register Offsets - From Databook Section 3.2 (Page 36)
 * These are VERIFIED from the databook
 */
#define TCA_CLK_RST			0x00
#define TCA_CLK_RST_SW			BIT(9)
#define TCA_CLK_RST_REF_CLK_EN		BIT(1)
#define TCA_CLK_RST_SUSPEND_CLK_EN	BIT(0)

#define TCA_INTR_EN			0x04
#define TCA_INTR_STS			0x08

#define TCA_GCFG			0x10
#define TCA_GCFG_ROLE_HSTDEV		BIT(4)
#define TCA_GCFG_OP_MODE		GENMASK(1, 0)
#define TCA_GCFG_OP_MODE_SYSMODE	0
#define TCA_GCFG_OP_MODE_SYNCMODE	1

#define TCA_TCPC			0x14
#define TCA_TCPC_VALID			BIT(4)
#define TCA_TCPC_LOW_POWER_EN		BIT(3)
#define TCA_TCPC_ORIENTATION_NORMAL	BIT(2)
#define TCA_TCPC_MUX_CONTRL		GENMASK(1, 0)
#define TCA_TCPC_MUX_CONTRL_NO_CONN	0
#define TCA_TCPC_MUX_CONTRL_USB_CONN	1

#define TCA_SYSMODE_CFG			0x18
#define TCA_SYSMODE_TCPC_DISABLE	BIT(3)
#define TCA_SYSMODE_TCPC_FLIP		BIT(2)

#define TCA_CTRLSYNCMODE_CFG0		0x20
#define TCA_CTRLSYNCMODE_CFG1           0x20

#define TCA_PSTATE			0x30
#define TCA_PSTATE_CM_STS		BIT(4)
#define TCA_PSTATE_TX_STS		BIT(3)
#define TCA_PSTATE_RX_PLL_STS		BIT(2)
#define TCA_PSTATE_PIPE0_POWER_DOWN	GENMASK(1, 0)

#define TCA_GEN_STATUS			0x34
#define TCA_GEN_DEV_POR			BIT(12)
#define TCA_GEN_REF_CLK_SEL		BIT(8)
#define TCA_GEN_TYPEC_FLIP_INVERT	BIT(4)
#define TCA_GEN_PHY_TYPEC_DISABLE	BIT(3)
#define TCA_GEN_PHY_TYPEC_FLIP		BIT(2)

#define TCA_VBUS_CTRL			0x40
#define TCA_VBUS_STATUS			0x44

#define TCA_INFO			0xfc

/* PHY Operation Modes - Custom flags for internal use */
#define PHY_MODE_DISABLED	0
#define PHY_MODE_USB3		BIT(0)
#define PHY_MODE_DP_CUSTOM	BIT(1)
#define PHY_MODE_USB3_DP	(BIT(0) | BIT(1))

struct zhihe_c10phy_priv {
	struct device		*dev;
	struct phy			*phy;
	void __iomem		*phy_ctrl_base;
	void __iomem		*tca_base;
	void __iomem		*sysreg_base;
	void __iomem		*dptxsys_base;
	struct reset_control	*c10phy_rst;
	enum typec_orientation orientation;			/* Type-C orientation */
	/* GPIO for AUX control */
	struct gpio_desc *aux_p_gpio;	/* AUX_P pull-down control */
	struct gpio_desc *aux_n_gpio;	/* AUX_N pull-up control */
	enum usb_role usb_role;			/* Current USB role: host or device */
	struct typec_switch_dev *sw;
	struct mutex lock;
};

static void tca_blk_orientation_set(struct zhihe_c10phy_priv *priv,
				enum typec_orientation orientation);

static int tca_blk_typec_switch_set(struct typec_switch_dev *sw,
				enum typec_orientation orientation)
{
	struct zhihe_c10phy_priv *priv = typec_switch_get_drvdata(sw);

	if (priv->orientation == orientation)
		return 0;

	tca_blk_orientation_set(priv, orientation);

	return 0;
}

static void tca_blk_orientation_set(struct zhihe_c10phy_priv *priv,
				enum typec_orientation orientation)
{
	u32 val;

	mutex_lock(&priv->lock);

	if (orientation == TYPEC_ORIENTATION_NONE) {
		/*
		 * use Controller Synced Mode for TCA low power enable and
		 * put PHY to USB safe state.
		 */
		val = FIELD_PREP(TCA_GCFG_OP_MODE, TCA_GCFG_OP_MODE_SYNCMODE);
		writel(val, priv->tca_base + TCA_GCFG);

		val = TCA_TCPC_VALID | TCA_TCPC_LOW_POWER_EN;
		writel(val, priv->tca_base + TCA_TCPC);

		goto out;
	}

	/* use System Configuration Mode for TCA mux control. */
	val = FIELD_PREP(TCA_GCFG_OP_MODE, TCA_GCFG_OP_MODE_SYSMODE);
	writel(val, priv->tca_base + TCA_GCFG);

	/* Disable TCA module */
	val = readl(priv->tca_base + TCA_SYSMODE_CFG);
	val |= TCA_SYSMODE_TCPC_DISABLE;
	writel(val, priv->tca_base + TCA_SYSMODE_CFG);

	if (orientation == TYPEC_ORIENTATION_REVERSE)
		val |= TCA_SYSMODE_TCPC_FLIP;
	else if (orientation == TYPEC_ORIENTATION_NORMAL)
		val &= ~TCA_SYSMODE_TCPC_FLIP;

	writel(val, priv->tca_base + TCA_SYSMODE_CFG);

	/* Enable TCA module */
	val &= ~TCA_SYSMODE_TCPC_DISABLE;
	writel(val, priv->tca_base + TCA_SYSMODE_CFG);

out:
	priv->orientation = orientation;
	mutex_unlock(&priv->lock);
}

static void tca_blk_init(struct zhihe_c10phy_priv *priv)
{
	u32 val;

	/* reset XBar block */
	val = readl(priv->tca_base + TCA_CLK_RST);
	val &= ~TCA_CLK_RST_SW;
	writel(val, priv->tca_base + TCA_CLK_RST);

	udelay(100);

	/* clear reset */
	val |= TCA_CLK_RST_SW;
	writel(val, priv->tca_base + TCA_CLK_RST);

	tca_blk_orientation_set(priv, priv->orientation);
}

/* Setup Type-C orientation switch */
static int zhihe_setup_typec_switch(struct zhihe_c10phy_priv *priv)
{
	struct typec_switch_desc sw_desc = {};

	sw_desc.drvdata = priv;
	sw_desc.fwnode = dev_fwnode(priv->dev);
	sw_desc.set = tca_blk_typec_switch_set;
	sw_desc.name = NULL;

	priv->sw = typec_switch_register(priv->dev, &sw_desc);
	if (IS_ERR(priv->sw)) {
		dev_err(priv->dev, "Failed to register typec switch: %ld\n", PTR_ERR(priv->sw));
		return PTR_ERR(priv->sw);
	}

	return 0;
}

static void zhihe_switch_unregister(void *data)
{
	struct zhihe_c10phy_priv *priv = data;
	typec_switch_unregister(priv->sw);
}

static int c10phy_init(struct phy *phy)
{
	struct zhihe_c10phy_priv *priv = phy_get_drvdata(phy);

	reset_control_deassert(priv->c10phy_rst);
	tca_blk_init(priv);
	reset_control_assert(priv->c10phy_rst);

	return 0;
}

static int c10phy_exit(struct phy *phy)
{
	struct zhihe_c10phy_priv *priv = phy_get_drvdata(phy);

	dev_dbg(priv->dev, "Shutting down C10PHY\n");

	/* Assert resets to power down PHY */
	reset_control_assert(priv->c10phy_rst);
	return 0;
}

static const struct phy_ops c10phy_ops = {
	.init = c10phy_init,
	.exit = c10phy_exit,
	.owner = THIS_MODULE,
};

/* Sysfs attribute for orientation control */
static ssize_t orientation_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct phy *phy = dev_get_drvdata(dev);
	struct zhihe_c10phy_priv *priv = phy_get_drvdata(phy);
	const char *orientation_str;

	switch (priv->orientation) {
	case TYPEC_ORIENTATION_NONE:
		orientation_str = "none";
		break;
	case TYPEC_ORIENTATION_NORMAL:
		orientation_str = "normal";
		break;
	case TYPEC_ORIENTATION_REVERSE:
		orientation_str = "reverse";
		break;
	default:
		orientation_str = "unknown";
		break;
	}

	return sprintf(buf, "%s\n", orientation_str);
}

static ssize_t orientation_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct phy *phy = dev_get_drvdata(dev);
	struct zhihe_c10phy_priv *priv = phy_get_drvdata(phy);
	enum typec_orientation orientation;

	if (sysfs_streq(buf, "none"))
		orientation = TYPEC_ORIENTATION_NONE;
	else if (sysfs_streq(buf, "normal"))
		orientation = TYPEC_ORIENTATION_NORMAL;
	else if (sysfs_streq(buf, "reverse"))
		orientation = TYPEC_ORIENTATION_REVERSE;
	else
		return -EINVAL;

	tca_blk_orientation_set(priv, orientation);

	return count;
}

static DEVICE_ATTR_RW(orientation);

static struct attribute *c10phy_attrs[] = {
	&dev_attr_orientation.attr,
	NULL,
};

static const struct attribute_group c10phy_attr_group = {
	.attrs = c10phy_attrs,
};

static int c10phy_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct device *dev = &pdev->dev;
	struct zhihe_c10phy_priv *priv;
	struct phy_provider *phy_provider;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	platform_set_drvdata(pdev, priv);

	priv->usb_role = USB_ROLE_HOST;  /* Default to host mode, can be changed dynamically */
	/* Get TCA register base */
	priv->tca_base = devm_platform_ioremap_resource_byname(pdev, "tca");
	if (IS_ERR(priv->tca_base))
		return dev_err_probe(dev, PTR_ERR(priv->tca_base),
				     "Couldn't get TCA register base\n");

	/* Get PHY_CTRL register base */
	priv->phy_ctrl_base = devm_platform_ioremap_resource_byname(pdev, "phy_ctrl");
	if (IS_ERR(priv->phy_ctrl_base))
		return dev_err_probe(dev, PTR_ERR(priv->phy_ctrl_base),
				     "Couldn't get PHY_CTRL register base\n");

	/* Get SYSREG register base */
	priv->sysreg_base = devm_platform_ioremap_resource_byname(pdev, "sysreg");
	if (IS_ERR(priv->sysreg_base))
		return dev_err_probe(dev, PTR_ERR(priv->sysreg_base),
				     "Couldn't get SYSREG register base\n");

	/* Get dptx SYSREG register base */
	priv->dptxsys_base = devm_platform_ioremap_resource_byname(pdev, "dptx_sys");
	if (IS_ERR(priv->sysreg_base))
		return dev_err_probe(dev, PTR_ERR(priv->sysreg_base),
				     "Couldn't get SYSREG register base\n");

	priv->c10phy_rst = devm_reset_control_get_shared(dev, "phy-rst");
	if (IS_ERR(priv->c10phy_rst))
		return dev_err_probe(dev, PTR_ERR(priv->c10phy_rst),
				     "Couldn't get phy-rst\n");

	/* Get GPIO for AUX control (optional) */
	priv->aux_p_gpio = devm_gpiod_get_optional(dev, "aux-p", GPIOD_OUT_LOW);
	if (IS_ERR(priv->aux_p_gpio)) {
		dev_err(dev, "Failed to get AUX_P GPIO\n");
		return PTR_ERR(priv->aux_p_gpio);
	}
	if (priv->aux_p_gpio)
		dev_info(dev, "AUX_P GPIO configured for pull-down control\n");

	priv->aux_n_gpio = devm_gpiod_get_optional(dev, "aux-n", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->aux_n_gpio)) {
		dev_err(dev, "Failed to get AUX_N GPIO\n");
		return PTR_ERR(priv->aux_n_gpio);
	}
	if (priv->aux_n_gpio)
		dev_info(dev, "AUX_N GPIO configured for pull-up control\n");

	/* Setup Type-C support if enabled */
	if (device_property_present(dev, "orientation-switch")) {
		ret = zhihe_setup_typec_switch(priv);
		if (ret) {
			return ret;
		}

		ret = devm_add_action_or_reset(dev, zhihe_switch_unregister, priv);
		if (ret)
			return ret;
	}

	priv->orientation = TYPEC_ORIENTATION_NORMAL;

	mutex_init(&priv->lock);

	/* Create PHY */
	priv->phy = devm_phy_create(dev, NULL, &c10phy_ops);
	if (IS_ERR(priv->phy))
		return dev_err_probe(dev, PTR_ERR(priv->phy),
				     "Couldn't create phy\n");

	dev_set_drvdata(dev, priv->phy);
	phy_set_drvdata(priv->phy, priv);

	/* Register PHY provider */
	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(phy_provider))
		return dev_err_probe(dev, PTR_ERR(phy_provider),
				     "failed to register phy provider\n");

	/* Create sysfs attribute for orientation control */
	ret = sysfs_create_group(&dev->kobj, &c10phy_attr_group);
	if (ret) {
		dev_err(dev, "Failed to create sysfs group: %d\n", ret);
		return ret;
	}

	return 0;
}

static int c10phy_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	sysfs_remove_group(&dev->kobj, &c10phy_attr_group);

	return 0;
}

static const struct of_device_id c10phy_of_match[] = {
	{ .compatible = "zhihe,a210-c10phy" },
	{ }
};
MODULE_DEVICE_TABLE(of, c10phy_of_match);

static struct platform_driver c10phy_driver = {
	.probe		= c10phy_probe,
	.remove		= c10phy_remove,
	.driver		= {
		.name	= "phy-zhihe-a210-c10phy",
		.of_match_table	= c10phy_of_match,
	},
};

module_platform_driver(c10phy_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ZHIHE C10PHY USB3.1 PHY Driver");
MODULE_AUTHOR("Anonymous <Anonymous@zhcomputing.com>");
