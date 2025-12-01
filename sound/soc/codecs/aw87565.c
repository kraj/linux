/*
* Copyright (c) 2025 Zhihe Computing CO., LTD
*
*
* This program is free software; you can redistribute  it and/or modify it
* under  the terms of  the GNU General  Public License as published by the
* Free Software Foundation;  either version 2 of the  License, or (at your
* option) any later version.
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/version.h>
#include <linux/types.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <sound/soc-dapm.h>
#include <linux/of_platform.h>
#include <linux/gpio/consumer.h>
#include "aw87565.h"

/*******************************************************************************
 * aw87565 marco
 ******************************************************************************/
#define AW87565_I2C_NAME	"aw87565_pa"
#define AW87565_DRIVER_VERSION	"v1.0.0"

/*******************************************************************************
 * aw87565 variable
 ******************************************************************************/
struct aw87565_container *aw87565_spk_cnt;
struct aw87565_container *aw87565_rcv_cnt;

/*******************************************************************************
 * i2c write and read
 ******************************************************************************/
static int aw87565_i2c_write(struct aw87565 *aw87565,
		  unsigned char reg_addr, unsigned char reg_data)
{
	int ret = -1;
	unsigned char cnt = 0;

	while (cnt < AW_I2C_RETRIES) {
		ret = i2c_smbus_write_byte_data(aw87565->i2c_client,
					reg_addr,
					reg_data);
		if (ret < 0) {
			return -1;
		} else {
			break;
		}
		cnt++;
		usleep_range(2000, 2500);
	 }

	 return ret;
}

static int aw87565_i2c_read(struct aw87565 *aw87565,
		 unsigned char reg_addr, unsigned char *reg_data)
{
	int ret = -1;
	unsigned char cnt = 0;

	while (cnt < AW_I2C_RETRIES) {
		ret = i2c_smbus_read_byte_data(aw87565->i2c_client, reg_addr);
		if (ret < 0) {
			return -1;
		} else {
			 *reg_data = ret;
			 break;
		}
		cnt++;
		usleep_range(2000, 2500);
	 }

	return ret;
}

/*******************************************************************************
 * aw87565 control interface
 ******************************************************************************/
static unsigned char aw87565_audio_receiver(struct aw87565 *aw87565)
{
	unsigned int i;
	unsigned int length;

	if (aw87565 == NULL)
		return 2;

	length = sizeof(aw87565_rcv_cfg_default)/sizeof(char);
	if (aw87565->rcv_cfg_update_flag == 0) {   /*update array data*/
		for (i = 0; i < length; i = i+2)
			aw87565_i2c_write(aw87565,
					aw87565_rcv_cfg_default[i],
					aw87565_rcv_cfg_default[i+1]);
	}
	if (aw87565->rcv_cfg_update_flag == 1) {  /*update bin data*/
		for (i = 0; i < aw87565_rcv_cnt->len; i = i+2)
			aw87565_i2c_write(aw87565,
				aw87565_rcv_cnt->data[i],
				aw87565_rcv_cnt->data[i+1]);
	}
	mdelay(50); // aw87565芯片需要50ms设置时间

	return 0;
}

static unsigned char aw87565_audio_speaker(struct aw87565 *aw87565)
{
	unsigned int i;
	unsigned int length;

	if (aw87565 == NULL)
		return 2;

	length = sizeof(aw87565_spk_cfg_default)/sizeof(char);
	if (aw87565->spk_cfg_update_flag == 0) {   /*update array data*/
		for (i = 0; i < length; i = i+2)
			aw87565_i2c_write(aw87565,
					aw87565_spk_cfg_default[i],
					aw87565_spk_cfg_default[i+1]);
	}
	if (aw87565->spk_cfg_update_flag == 1) {  /*update bin data*/
		for (i = 0; i < aw87565_spk_cnt->len; i = i+2)
			aw87565_i2c_write(aw87565,
				aw87565_spk_cnt->data[i],
				aw87565_spk_cnt->data[i+1]);
	}
	mdelay(50); // aw87565芯片需要50ms设置时间

	return 0;
}

static unsigned char aw87565_audio_off(struct aw87565 *aw87565)
{
	if (aw87565 == NULL)
		return 2;

	aw87565_i2c_write(aw87565, 0x01, 0x80); /*芯片禁用*/

	return 0;
}

/*******************************************************************************
 * aw87565 attribute
 ******************************************************************************/
static ssize_t aw87565_get_reg(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct aw87565 *aw87565;
	ssize_t len = 0;
	unsigned int i = 0;
	unsigned char reg_val = 0;

	aw87565 = dev_get_drvdata(dev);
	if (!aw87565)
		return -EINVAL;

	for (i = 0; i < AW87565_REG_MAX; i++) {
		aw87565_i2c_read(aw87565, i, &reg_val);
		len += snprintf(buf+len, PAGE_SIZE-len, "reg:0x%02x=0x%02x\n",
				i, reg_val);
	}
	return len;
}

static ssize_t aw87565_set_reg(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct aw87565 *aw87565;
	unsigned int databuf[2] = {0, 0};

	aw87565 = dev_get_drvdata(dev);
	if (!aw87565)
		return -EINVAL;

	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2)
		aw87565_i2c_write(aw87565, databuf[0], databuf[1]);
	return len;
}

static ssize_t aw87565_get_hwen(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct aw87565 *aw87565;
	ssize_t len = 0;

	aw87565 = dev_get_drvdata(dev);
	if (!aw87565)
		return -EINVAL;

	len += snprintf(buf+len, PAGE_SIZE-len, "hwen: %d\n",
			aw87565->hwen_flag);
	return len;
}

static ssize_t aw87565_set_hwen(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct aw87565 *aw87565;
	ssize_t ret;
	unsigned int state;

	aw87565 = dev_get_drvdata(dev);
	if (!aw87565)
		return -EINVAL;

	ret = kstrtouint(buf, 10, &state);
	if (ret) {
		dev_err(&aw87565->i2c_client->dev, "%s: fail to change str to int\n",
			__func__);
		return ret;
	}

	return len;
}

static ssize_t aw87565_get_mode(struct device *cd,
	struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;

	len += snprintf(buf+len, PAGE_SIZE-len, "0: off mode\n");
	len += snprintf(buf+len, PAGE_SIZE-len, "1: spk mode\n");
	len += snprintf(buf+len, PAGE_SIZE-len, "2: rcv mode\n");

	return len;
}

static ssize_t aw87565_set_mode(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t len)
{
	struct aw87565 *aw87565;
	ssize_t ret;
	unsigned int state;

	aw87565 = dev_get_drvdata(dev);
	if (!aw87565)
		return -EINVAL;

	ret = kstrtouint(buf, 10, &state);
		ret = kstrtouint(buf, 10, &state);
	if (ret)
		goto out_strtoint;
	if (state == 0)
		aw87565_audio_off(aw87565);
	else if (state == 1)
		aw87565_audio_speaker(aw87565);
	else if (state == 2)
		aw87565_audio_receiver(aw87565);
	else
		aw87565_audio_off(aw87565);

	if (ret < 0)
		goto out;

	return len;

out:
	dev_err(&aw87565->i2c_client->dev, "%s: i2c access fail to register\n",
		__func__);
out_strtoint:
	dev_err(&aw87565->i2c_client->dev, "%s: fail to change str to int\n",
		__func__);
	return ret;
}

static DEVICE_ATTR(reg, 0660, aw87565_get_reg, aw87565_set_reg);
static DEVICE_ATTR(hwen, 0660, aw87565_get_hwen, aw87565_set_hwen);
static DEVICE_ATTR(mode, 0660, aw87565_get_mode, aw87565_set_mode);

static struct attribute *aw87565_attributes[] = {
	&dev_attr_reg.attr,
	&dev_attr_hwen.attr,
	&dev_attr_mode.attr,
	NULL
};

static struct attribute_group aw87565_attribute_group = {
	.attrs = aw87565_attributes
};

/*****************************************************
 * device tree
 *****************************************************/
int aw87565_hw_reset(struct aw87565 *aw87565)
{
	return 0;
}

/*****************************************************
 * check chip id
 *****************************************************/
int aw87565_read_chipid(struct aw87565 *aw87565)
{
	unsigned int cnt = 0;
	unsigned int retry_count = 1;
	int ret = -1;
	unsigned char reg_val = 0;

retry:
	while (cnt < AW_READ_CHIPID_RETRIES) {
		ret = aw87565_i2c_read(aw87565, REG_CHIPID, &reg_val);
		if (reg_val == AW87565_CHIPID) {
			return 0;
		}
		cnt++;
		usleep_range(2500, 3000);
	}

	if (retry_count > 0) {
		aw87565_hw_reset(aw87565);
		cnt = 0;
		retry_count--;
		goto retry;
	}

	return -EINVAL;
}

static const DECLARE_TLV_DB_RANGE(aw87565_tlv,
	0, 2, TLV_DB_SCALE_ITEM(0, 300, 0),	// 0db ~ 6db
	3, 7, TLV_DB_SCALE_ITEM(1200, 300, 0)	// 12db ~ 24db
);

static const struct snd_kcontrol_new aw87565_controls[] = {
	SOC_SINGLE_TLV("Speaker Playback Volume", REG_PAG, 0, 0x7, 0, aw87565_tlv),
};

static int aw87565_component_probe(struct snd_soc_component* component)
{
	return 0;
}

static int  aw87565_power_event(struct snd_soc_dapm_widget *w,
				 struct snd_kcontrol *kctrl, int event)
{
	struct snd_soc_component *c = snd_soc_dapm_to_component(w->dapm);
	struct aw87565 *pa = snd_soc_component_get_drvdata(c);

	if (SND_SOC_DAPM_EVENT_ON(event)) {
		aw87565_hw_reset(pa);
		/* Before widget power up: turn chip on, sync registers */
		aw87565_audio_speaker(pa);
	} else {
		/* After widget power down: turn chip off */
		aw87565_audio_off(pa);
	}

	return 0;
}

static const struct snd_soc_dapm_widget aw87565_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("IN"),
	SND_SOC_DAPM_OUTPUT("VO"),
	SND_SOC_DAPM_PGA("PGA", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Power", SND_SOC_NOPM, 0, 0, 
		aw87565_power_event, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
};

static const struct snd_soc_dapm_route aw87565_dapm_routes[] = {
	{ "PGA", NULL, "IN"},
	{ "VO", NULL, "PGA"},
	{ "PGA", NULL, "Power"},
};

static const struct reg_default aw87565_reg_defaults[] = {
	{ 0x01, 0x80 }, { 0x21, 0x6B }, { 0x7E, 0xD2 }, { 0x28, 0x00 },
	{ 0x02, 0x33 }, { 0x03, 0x29 }, { 0x04, 0xFC }, { 0x05, 0x45 },
	{ 0x06, 0x49 }, { 0x07, 0x1A }, { 0x08, 0x23 }, { 0x09, 0x4E },
	{ 0x0A, 0x7E }, { 0x0B, 0x2B }, { 0x0C, 0x25 }, { 0x0D, 0xA8 }, 
	{ 0x0E, 0x05 }, { 0x0F, 0x34 }, { 0x10, 0x90 }, { 0x11, 0x50 },
	{ 0x12, 0x63 }, { 0x13, 0X3F }, { 0x14, 0x41 }, { 0x15, 0x09 },
	{ 0x16, 0x53 }, { 0x17, 0X51 }, { 0x18, 0x5D }, { 0x19, 0xCB },
	{ 0x1A, 0x87 }, { 0x1B, 0X13 }, { 0x1C, 0x89 }, { 0x1D, 0x48 },
	{ 0x1E, 0x1C }, { 0x1F, 0xC4 }, { 0x20, 0x0F }, { 0x22, 0x8D },
	{ 0x23, 0x69 }, { 0x29, 0x00 }, { 0x2A, 0x00 }, { 0x2B, 0X00 },
	{ 0x2C, 0x00 }, { 0x2D, 0x2E }, { 0x2E, 0x06 }, { 0x2F, 0x10 },
	{ 0x30, 0x00 }, { 0x3E, 0x00 }, { 0x3F, 0x00 }, { 0x50, 0x0C },
	{ 0x51, 0x00 }, { 0x7F, 0x00 }, { 0x01, 0xBE }, { 0x01, 0xBC },
};

const struct regmap_config aw87565_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.max_register	= 0xFF,
	.cache_type	= REGCACHE_RBTREE,
	.reg_defaults	= aw87565_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(aw87565_reg_defaults),
};

static const struct snd_soc_component_driver aw87565_component_driver = {
	.name = "aw87565",
	.probe = aw87565_component_probe,
	.dapm_widgets = aw87565_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(aw87565_dapm_widgets),
	.dapm_routes = aw87565_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(aw87565_dapm_routes),
};

/*******************************************************************************
 * aw87565 i2c driver
 ******************************************************************************/
static int aw87565_i2c_probe(struct i2c_client *client)
{
	struct aw87565 *aw87565;
	int ret = -1;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "%s: i2c check failed\n", __func__);
		return -ENODEV;
	}

	aw87565 = devm_kzalloc(&client->dev, sizeof(struct aw87565), GFP_KERNEL);
	if (aw87565 == NULL) {
		dev_err(&client->dev, "Failed to alloc priv data\n");
		return -ENOMEM;
	}

	aw87565->regmap = devm_regmap_init_i2c(client, &aw87565_regmap_config);
	if (IS_ERR(aw87565->regmap)) {
		ret = PTR_ERR(aw87565->regmap);
		dev_err(&client->dev, "Failed to init regmap: %d\n", ret);
		return ret;
	}

	aw87565->i2c_client = client;
	i2c_set_clientdata(client, aw87565);

	aw87565->audio_parst0_desc = devm_gpiod_get_optional(&client->dev, "reset", GPIOD_OUT_LOW);
	if (aw87565->audio_parst0_desc) {
		gpiod_set_value_cansleep(aw87565->audio_parst0_desc, 1);
		msleep(1);
		gpiod_set_value_cansleep(aw87565->audio_parst0_desc, 0);
		msleep(5);
	}

	ret = aw87565_read_chipid(aw87565);
	if (ret < 0) {
		dev_err(&client->dev, "%s: aw87565_read_chipid failed %d\n",
			__func__, ret);
		return ret;
	}

	/* 创建sysfs属性文件 */
	ret = sysfs_create_group(&client->dev.kobj, &aw87565_attribute_group);
	if (ret < 0) {
		dev_info(&client->dev, "%s error creating sysfs attr files\n",
			__func__);
	}

	aw87565->spk_cfg_update_flag = 0;
	aw87565->rcv_cfg_update_flag = 0;

	return devm_snd_soc_register_component(&client->dev, &aw87565_component_driver, NULL, 0);
}

static void aw87565_i2c_remove(struct i2c_client *client)
{
	struct aw87565 *aw87565 = i2c_get_clientdata(client);

	if (aw87565->audio_parst0_desc)
		gpiod_put(aw87565->audio_parst0_desc);
	return;
}

static const struct i2c_device_id aw87565_i2c_id[] = {
	{ AW87565_I2C_NAME, 0 },
	{ }
};

static const struct of_device_id extpa_of_match[] = {
	{.compatible = "awinic,aw87565_pa"},
	{},
};

static struct i2c_driver aw87565_i2c_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= AW87565_I2C_NAME,
		.of_match_table = extpa_of_match,
	},
	.probe 		= aw87565_i2c_probe,
	.remove 	= aw87565_i2c_remove,
	.id_table	= aw87565_i2c_id,
};

static int __init aw87565_pa_init(void)
{
	int ret;

	ret = i2c_add_driver(&aw87565_i2c_driver);
	if (ret) {
		pr_info("%s Unable to register driver (%d)\n", __func__, ret);
		return ret;
	}
	return 0;
}

static void __exit aw87565_pa_exit(void)
{
	pr_info("%s enter\n", __func__);
	i2c_del_driver(&aw87565_i2c_driver);
}

module_init(aw87565_pa_init);
module_exit(aw87565_pa_exit);

MODULE_DESCRIPTION("AWINIC AW87565 PA driver");
MODULE_LICENSE("GPL v2");
