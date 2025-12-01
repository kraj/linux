// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Zhihe Group Holding Limited.
 */
 
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/of.h>
#include <linux/slab.h>

#define WL2866D_REG_DVDD1_VOUT    0x03
#define WL2866D_REG_DVDD2_VOUT    0x04
#define WL2866D_REG_AVDD1_VOUT    0x05
#define WL2866D_REG_AVDD2_VOUT    0x06
#define WL2866D_REG_ENABLE        0x0E
#define WL2866D_REG_CHIP_REV      0x00

#define WL2866D_EN_DVDD1          BIT(0)
#define WL2866D_EN_DVDD2          BIT(1)
#define WL2866D_EN_AVDD1          BIT(2)
#define WL2866D_EN_AVDD2          BIT(3)

enum wl2866d_regulator_id {
	WL2866D_DVDD1_MIPICSI0,	        /* dvdd_mipicsi0 */
	WL2866D_DVDD2_MIPICSI0,         /* dvdd_mipicsi0 */
	WL2866D_AVDD1_MIPICSI0,         /* avdd_mipicsi0 */
	WL2866D_AVDD2_MIPICSI0,         /* avdd_mipicsi0 */
	WL2866D_DVDD1_MIPICSI1,         /* dvdd_mipicsi1 */
	WL2866D_DVDD2_MIPICSI1,         /* dvdd_mipicsi1 */
	WL2866D_AVDD1_MIPICSI1,         /* avdd_mipicsi1 */
	WL2866D_AVDD2_MIPICSI1,         /* avdd_mipicsi1 */
	WL2866D_NUM_REGULATORS,
};

struct wl2866d {
	struct device *dev;
	struct regmap *regmap;
};

static inline u8 wl2866d_clamp_sel(unsigned int sel)
{
	if (sel > 0xFF)
		return 0xFF;
	return sel & 0xFF;
}

static int wl2866d_enable(struct regulator_dev *rdev)
{
	struct regmap *regmap = rdev->regmap;
	unsigned int mask = rdev->desc->enable_mask;
	int ret;

	if (!rdev->desc->enable_reg)
		return -EINVAL;

	ret = regmap_update_bits(regmap, rdev->desc->enable_reg, mask, mask);
	if (ret)
		dev_err(&rdev->dev, "%s: enable write failed: %d\n",
			rdev->desc->name, ret);
	return ret;
}

static int wl2866d_disable(struct regulator_dev *rdev)
{
	struct regmap *regmap = rdev->regmap;
	unsigned int mask = rdev->desc->enable_mask;
	int ret;

	if (!rdev->desc->enable_reg)
		return -EINVAL;

	ret = regmap_update_bits(regmap, rdev->desc->enable_reg, mask, 0);
	if (ret)
		dev_err(&rdev->dev, "%s: disable write failed: %d\n",
			rdev->desc->name, ret);
	return ret;
}

static int wl2866d_is_enabled(struct regulator_dev *rdev)
{
	struct regmap *regmap = rdev->regmap;
	unsigned int val;
	int ret;

	if (!rdev->desc->enable_reg)
		return -EINVAL;

	ret = regmap_read(regmap, rdev->desc->enable_reg, &val);
	if (ret) {
		dev_err(&rdev->dev, "%s: read enable_reg failed: %d\n",
			rdev->desc->name, ret);
		return ret;
	}
	return (val & rdev->desc->enable_mask);
}

static int wl2866d_set_voltage_sel(struct regulator_dev *rdev,
				   unsigned int sel)
{
	struct regmap *regmap = rdev->regmap;
	u8 val = wl2866d_clamp_sel(sel);
	int ret;

	if (!rdev->desc->vsel_reg)
		return -EINVAL;

	ret = regmap_write(regmap, rdev->desc->vsel_reg, val);
	if (ret)
		dev_err(&rdev->dev, "%s: set vsel failed: %d\n",
			rdev->desc->name, ret);
	return ret;
}

static int wl2866d_get_voltage_sel(struct regulator_dev *rdev)
{
	struct regmap *regmap = rdev->regmap;
	int val;
	int ret;

	if (!rdev->desc->vsel_reg)
		return -EINVAL;

	ret = regmap_read(regmap, rdev->desc->vsel_reg, &val);
	if (ret) {
		dev_err(&rdev->dev, "%s: get vsel failed: %d\n",
			rdev->desc->name, ret);
		return ret;
	}
	return val & 0xFF;
}

int wl2866d_match_regulator_name(struct regulator_desc *regus_set, const char *string)
{
	int index;
	const char *item;

	for (index = 0; index < WL2866D_NUM_REGULATORS; index++) {
		item = regus_set[index].name;
		if (!item)
			break;
		if (!strcmp(item, string))
			return index;
	}
	return -EINVAL;
}

static const struct regulator_ops wl2866d_regulator_ops = {
	.enable = wl2866d_enable,
	.disable = wl2866d_disable,
	.is_enabled = wl2866d_is_enabled,
	.set_voltage_sel = wl2866d_set_voltage_sel,
	.get_voltage_sel = wl2866d_get_voltage_sel,
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
};

#define WL2866D_DESC(_id, _name, _min_uv, _step_uv, _vsel_reg, _en_bit) { \
	.name = _name,													  \
	.of_match = of_match_ptr(_name),								  \
	.regulators_node = of_match_ptr("regulators"),					  \
	.id = _id,											              \
	.ops = &wl2866d_regulator_ops,									  \
	.n_voltages = 256, 											      \
	.min_uV = (_min_uv),											  \
	.uV_step = (_step_uv),											  \
	.vsel_reg = (_vsel_reg),										  \
	.vsel_mask = 0xFF,												  \
	.enable_reg = WL2866D_REG_ENABLE,								  \
	.enable_mask = (_en_bit),										  \
	.type = REGULATOR_VOLTAGE,										  \
	.owner = THIS_MODULE,											  \
}

static const struct regulator_desc wl2866d_descs[] = {
	WL2866D_DESC(WL2866D_DVDD1_MIPICSI0, "dvdd1_mipicsi0", 600000, 6000, WL2866D_REG_DVDD1_VOUT, WL2866D_EN_DVDD1),
	WL2866D_DESC(WL2866D_DVDD2_MIPICSI0, "dvdd2_mipicsi0", 600000, 6000, WL2866D_REG_DVDD2_VOUT, WL2866D_EN_DVDD2),
	WL2866D_DESC(WL2866D_AVDD1_MIPICSI0, "avdd1_mipicsi0", 1200000, 12500, WL2866D_REG_AVDD1_VOUT, WL2866D_EN_AVDD1),
	WL2866D_DESC(WL2866D_AVDD2_MIPICSI0, "avdd2_mipicsi0", 1200000, 12500, WL2866D_REG_AVDD2_VOUT, WL2866D_EN_AVDD2),
	WL2866D_DESC(WL2866D_DVDD1_MIPICSI1, "dvdd1_mipicsi1", 600000, 6000, WL2866D_REG_DVDD1_VOUT, WL2866D_EN_DVDD1),
	WL2866D_DESC(WL2866D_DVDD2_MIPICSI1, "dvdd2_mipicsi1", 600000, 6000, WL2866D_REG_DVDD2_VOUT, WL2866D_EN_DVDD2),
	WL2866D_DESC(WL2866D_AVDD1_MIPICSI1, "avdd1_mipicsi1", 1200000, 12500, WL2866D_REG_AVDD1_VOUT, WL2866D_EN_AVDD1),
	WL2866D_DESC(WL2866D_AVDD2_MIPICSI1, "avdd2_mipicsi1", 1200000, 12500, WL2866D_REG_AVDD2_VOUT, WL2866D_EN_AVDD2),
};

static const struct regmap_config wl2866d_regmap_cfg = {
	.reg_bits = 8,
	.val_bits = 8,
	.cache_type = REGCACHE_NONE,
};

static int wl2866d_probe(struct i2c_client *client)
{
	struct wl2866d *pmic;
	struct device_node *regulators, *child;
	struct regulator_config cfg = { };
	struct device_node *node = client->dev.of_node;
	struct regulator_dev *rdev;
	const char *regulator_name;
	int index, ret;
	unsigned int chiprev;

	if (!node)
		dev_warn(&client->dev, "No device-tree node\n");

	pmic = devm_kzalloc(&client->dev, sizeof(*pmic), GFP_KERNEL);
	if (!pmic)
		return -ENOMEM;

	pmic->dev = &client->dev;
	pmic->regmap = devm_regmap_init_i2c(client, &wl2866d_regmap_cfg);
	if (IS_ERR(pmic->regmap))
		return dev_err_probe(&client->dev, PTR_ERR(pmic->regmap),
					 "regmap init failed\n");

	/* optional: read chip revision to sanity-check */
	ret = regmap_read(pmic->regmap, WL2866D_REG_CHIP_REV, &chiprev);
	if (ret == 0)
		dev_info(&client->dev, "WL2866D chip rev 0x%02x\n", chiprev);
	else
		dev_info(&client->dev, "WL2866D (chip rev read failed: %d)\n", ret);

	regulators = of_get_child_by_name(node, "regulators");
	if (!regulators) {
		dev_err(&client->dev, "No regulators node found\n");
		return -ENODEV;
	}

	for_each_child_of_node(regulators, child) {
		of_property_read_string(child, "regulator-name", &regulator_name);
		if (!regulator_name) {
			dev_err(&client->dev, "failed to get a regulator-name\n");
			return -EINVAL;
		}

		index = wl2866d_match_regulator_name((struct regulator_desc *)wl2866d_descs, regulator_name);
		if (index < 0) {
			dev_err(&client->dev, "no regulator matches %s\n", regulator_name);
			return -EINVAL;
		}

		cfg.dev = &client->dev;
		cfg.regmap = pmic->regmap;
		cfg.of_node = child;
		cfg.init_data = of_get_regulator_init_data(&client->dev, child, &wl2866d_descs[index]);

		rdev = devm_regulator_register(&client->dev, &wl2866d_descs[index], &cfg);
		if (IS_ERR(rdev)) {
			dev_err(&client->dev, "Failed to register regulator %s\n", regulator_name);
			return PTR_ERR(rdev);
		}
	}

	dev_info(&client->dev, "WL2866D regulators registered\n");
	return 0;
}

static const struct of_device_id wl2866d_of_match[] = {
	{ .compatible = "willsemi,wl2866d" },
	{ }
};

MODULE_DEVICE_TABLE(of, wl2866d_of_match);

static struct i2c_driver wl2866d_driver = {
	.driver = {
		.name = "wl2866d-regulator",
		.of_match_table = wl2866d_of_match,
	},
	.probe = wl2866d_probe,
};
module_i2c_driver(wl2866d_driver);

MODULE_AUTHOR("hongkun.xu <xuhongkun@zhcomputing.com>");
MODULE_DESCRIPTION("wl2866d regulator driver");
MODULE_LICENSE("GPL");

