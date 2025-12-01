// SPDX-License-Identifier: GPL-2.0-only
/*
 * Memtester driver for DFMU unit of ZH socs.
 *
 * Copyright © 2025 Zhihe Computing Inc.
 *
 * Authors
 *	Dong Yan <yand@zhcomputing.com>
 *	Chao Cheng <chengchao@zhcomputing.com>
 */

#include <asm/io.h>
#include <linux/cdev.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/pm_runtime.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/debugfs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/of_address.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>

#define ZH_MT_NAME "zhihe-memtester"

#define MT_REG_CTRL  0x0
#define MT_REG_STATUS	0x4
#define MT_REG_CFG0  0x8
#define MT_REG_CFG1  0xc
#define MT_REG_AXI_ATTR  0x10
#define MT_REG_LOOP  0x14
#define MT_REG_PRBS_SEED 0x18
#define MT_REG_WR_SADDR  0x1c
#define MT_REG_ERR_ADDR  0x20
#define MT_REG_ERR_BIT   0x24
#define MT_REG_LOOP_TIME 0x28
#define MT_REG_ADDR_MASK 0x2c
#define MT_REG_FIFO_CTRL 0x30
#define MT_REG_AXI_STATUS	0x40
#define MT_REG_RDATA1	0x44
#define MT_REG_RDATA2	0x48
#define MT_REG_RDATA3	0x4c
#define MT_REG_RDATA4	0x50

#define MAX_POLL_TIMES 50
#define MT_ALLOC_PAGE_CNT 16384

struct zh_memtester_device {
	struct device *dev;
	struct miscdevice misc;
	void __iomem *reg;
	bool lite;
	struct page **pages;
	struct sg_table *sgt;
	dma_addr_t wr_iova;
	phys_addr_t wr_pa;

	/* config time */
	unsigned long test_count;
	unsigned long run_count;
	unsigned long run_fail_timeout_count;
	unsigned long run_fail_poll_count;

	/* check_en(bit3) rd_en(bit2) wr_en(bit1) for MT_REG_CTRL */
	unsigned int ctrl_mode;
	/* range -> mask */
	unsigned int range;

	/* distinguish the master */
	unsigned int pattern; 

	struct delayed_work check_status_work;
	unsigned long check_status_cnt;
	unsigned int fail_status; 
	unsigned int axi_status; 
   
	u64 start_time;
};

void zh_memtester_readafterwrite(struct device *dev)
{
	return;
}

void zh_memtester_readandwrite(struct device *dev)
{
	return;
}

void zh_memtester_readonly(struct device *dev)
{
	struct zh_memtester_device *mt = dev_get_drvdata(dev);

	if (!mt->reg) {
		dev_err(dev, "reg not initialized\n");
		return;
	}
	if (!mt->wr_iova) {
		dev_err(dev, "wr_iova not initialized 0x%llx! dev=0x%px\n", mt->wr_iova, dev);
		return;
	}

	writel(0x00008000, mt->reg + MT_REG_CFG0);
	writel(0x1b11f100, mt->reg + MT_REG_CFG1);
	writel(0x077c077c, mt->reg + MT_REG_AXI_ATTR);
	writel(0x01000010, mt->reg + MT_REG_LOOP);
	writel(0x12153524, mt->reg + MT_REG_PRBS_SEED);
	writel(mt->wr_iova, mt->reg + MT_REG_WR_SADDR);
	writel(0xfffffff0, mt->reg + MT_REG_ADDR_MASK);
	writel(0x01510004, mt->reg + MT_REG_CTRL);
	writel(0x01510005, mt->reg + MT_REG_CTRL);
}

void zh_memtester_writeonly(struct device *dev)
{
	struct zh_memtester_device *mt = dev_get_drvdata(dev);
	if (!mt->reg) {
		dev_err(dev, "reg not initialized\n");
		return;
	}

	if (!mt->wr_iova) {
		dev_err(dev, "wr_iova not initialized 0x%llx! dev=0x%px\n", mt->wr_iova, dev);
		return;
	}

	writel(0x00008000, mt->reg + MT_REG_CFG0);
	writel(0x10110100, mt->reg + MT_REG_CFG1);
	writel(0x037c037c, mt->reg + MT_REG_AXI_ATTR);
	writel(0x04000004, mt->reg + MT_REG_LOOP);
	writel(0x12153524, mt->reg + MT_REG_PRBS_SEED);
	writel(mt->wr_iova, mt->reg + MT_REG_WR_SADDR);
	writel(0xfffffff0, mt->reg + MT_REG_ADDR_MASK);
	writel(0x0151000e, mt->reg + MT_REG_CTRL);
	writel(0x0151000f, mt->reg + MT_REG_CTRL);
}

void zh_memtester_writespecific(struct device *dev, u64 addr)
{
	struct zh_memtester_device *mt = dev_get_drvdata(dev);

	writel(0x00008000, mt->reg + MT_REG_CFG0);
	writel(0x10110100, mt->reg + MT_REG_CFG1);
	writel(0x037c037c, mt->reg + MT_REG_AXI_ATTR);
	writel(0x04000004, mt->reg + MT_REG_LOOP);
	writel(0x12153524, mt->reg + MT_REG_PRBS_SEED);
	writel(addr, mt->reg + MT_REG_WR_SADDR);
	writel(0xfffffff0, mt->reg + MT_REG_ADDR_MASK);
	writel(0x0151000e, mt->reg + MT_REG_CTRL);
	writel(0x0151000f, mt->reg + MT_REG_CTRL);
}

static void check_status_delay_work(struct work_struct *work)
{
	struct zh_memtester_device *mt  =
				container_of(work, struct zh_memtester_device, check_status_work.work);
	u32 v;

	mt->check_status_cnt++;
	v = readl(mt->reg + MT_REG_STATUS);
	/* no-err */
	if ((v & 0x1) == 0) {
		schedule_delayed_work(&mt->check_status_work, HZ);
	/* err fail */
	} else {
		mt->fail_status = v;
		printk(KERN_EMERG "(%s) catch fail @ %ld status: 0x%x", dev_name(mt->dev), mt->check_status_cnt, v);

		/* stop */
		v = readl(mt->reg + MT_REG_CTRL);
		writel(v & 0xfffffff0, mt->reg + MT_REG_CTRL);

		cancel_delayed_work(&mt->check_status_work);
	}
	v = readl(mt->reg + MT_REG_AXI_STATUS);
	mt->axi_status = v;
}

void zh_memtester_writespecific2(struct device *dev, u64 addr)
{
	struct zh_memtester_device *mt = dev_get_drvdata(dev);
	bool need_shift = false;
	int v = 0;
	int mask = 0;
	int prbs_sel_bit = (0x5 << 20);
	int test_en_bit = (0x1 << 0);
	static int pattern = 0;

	if (!mt || !mt->reg) {
		dev_err(dev, "reg not initialized\n");
		return;
	}

	/* disable firstly */
	v = readl(mt->reg + MT_REG_CTRL);
	writel(v & 0xfffffff0, mt->reg + MT_REG_CTRL);
	cancel_delayed_work_sync(&mt->check_status_work);
	writel(0xffffffff, mt->reg + MT_REG_STATUS);

	mt->check_status_cnt = 0;
	mt->axi_status = 0;

	if (!addr)
		return;

	pattern++;
	if (addr >> 32) 
		need_shift = true;

	if (need_shift) {
		/* [25:24] 0x2 left shift 8bit */
		writel(0x02008000, mt->reg + MT_REG_CFG0);
	} else
		writel(0x00008000, mt->reg + MT_REG_CFG0);

	writel(0x10110100, mt->reg + MT_REG_CFG1);
	/* axlen: 8 transfer,  axsize: 0'b100 -> 16 Bytes */
	writel(0x07fc07fc, mt->reg + MT_REG_AXI_ATTR);
	/* infinite loop, 4096 xtran  */
	writel(0xff001000, mt->reg + MT_REG_LOOP);
	writel(0x12153524, mt->reg + MT_REG_PRBS_SEED);
  
	if (need_shift) {
		writel(addr >> 8, mt->reg + MT_REG_WR_SADDR);
	} else {
		writel(addr, mt->reg + MT_REG_WR_SADDR);
	}

	if (need_shift) {
		mask = (addr >> 8);
		mask = mask | ((mt->range - 1) >> 8);
	} else {
		mask = addr;
		mask = mask | (mt->range - 1);
	}
	writel(mask, mt->reg + MT_REG_ADDR_MASK);
	writel((pattern << 24) | prbs_sel_bit | (mt->ctrl_mode << 1) | test_en_bit, mt->reg + MT_REG_CTRL);

	mt->pattern = pattern;
	mt->start_time = ktime_get_ns();

	schedule_delayed_work(&mt->check_status_work, 0);
}

static ssize_t readonly_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return 0;
}

static ssize_t readonly_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	bool enable;

	if (kstrtobool(buf, &enable) < 0)
		return -EINVAL;
	zh_memtester_readonly(dev);

	return count;
}

static DEVICE_ATTR_RW(readonly);

static ssize_t writeonly_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return 0;
}

static ssize_t writeonly_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	bool enable;

	if (kstrtobool(buf, &enable) < 0)
		return -EINVAL;
	zh_memtester_writeonly(dev);

	return count;
}

static DEVICE_ATTR_RW(writeonly);


static ssize_t writespecific_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return 0;
}

static ssize_t writespecific_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	u64 addr;

	if (kstrtou64(buf, 0, &addr) < 0)
		return -EINVAL;
	zh_memtester_writespecific(dev, addr);

	return count;
}
static DEVICE_ATTR_RW(writespecific);

static ssize_t range_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct zh_memtester_device *mt = dev_get_drvdata(dev);
	unsigned long val;
	char unit = '\0';
	int ret;

	ret = sscanf(buf, "%lu%c", &val, &unit);

	if (ret == 1) {
		/* no unit */
	} else if (ret == 2) {
		switch (unit) {
			case 'K':
			case 'k':
				val <<= 10;
				break;
			case 'M':
			case 'm':
				val <<= 20;
				break;
			case 'G':
			case 'g':
				val <<= 30;
				break;
			default:
				return -EINVAL;
		}
	} else {
		return -EINVAL;
	}

	mt->range = val;

	return count;
}

static ssize_t range_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct zh_memtester_device *mt = dev_get_drvdata(dev);
	unsigned long val = mt->range;
	unsigned long display = val;
	const char *unit = "B";

	if (val >= (1UL << 30)) {
		display = val >> 30;
		unit = "G";
	} else if (val >= (1UL << 20)) {
		display = val >> 20;
		unit = "M";
	} else if (val >= (1UL << 10)) {
		display = val >> 10;
		unit = "K";
	}

	return scnprintf(buf, PAGE_SIZE, "%lu%s\n", display, unit);
}
static DEVICE_ATTR_RW(range);

static ssize_t ctrl_mode_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct zh_memtester_device *mt = dev_get_drvdata(dev);
	unsigned int ctrl_mode;

	if (kstrtou32(buf, 0, &ctrl_mode) < 0)
		return -EINVAL;

	ctrl_mode &= 0x7;
	mt->ctrl_mode = ctrl_mode;
	return count;
}

static ssize_t ctrl_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct zh_memtester_device *mt = dev_get_drvdata(dev);
	ssize_t len = 0;

	len += scnprintf(buf + len, PAGE_SIZE - len, "ctrl_mode %d\n", mt->ctrl_mode);
	len += scnprintf(buf + len, PAGE_SIZE - len, " check_en(bit2): %d\n", !!(mt->ctrl_mode & (0x1 << 2)));
	len += scnprintf(buf + len, PAGE_SIZE - len, " rd_en(bit1): %d\n", !!(mt->ctrl_mode & (0x1 << 1)));
	len += scnprintf(buf + len, PAGE_SIZE - len, " wr_en(bit0): %d\n", !!(mt->ctrl_mode & (0x1 << 0)));

	return len;
}
static DEVICE_ATTR_RW(ctrl_mode);

static ssize_t writespecific2_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct zh_memtester_device *mt = dev_get_drvdata(dev);
	ssize_t len = 0;
	int v;
	u64 delta;
	u64 delta_ms;
	u64 loops;
	u64 speed;
	u64 real_size_mb;
	int data_width;

	v = readl(mt->reg + MT_REG_STATUS);
	if ((v & 0x1) == 0)
		len += scnprintf(buf + len, PAGE_SIZE - len, "%s mt ok\n", dev_name(mt->dev));
	else
		len += scnprintf(buf + len, PAGE_SIZE - len, "%s mt err, status: %d\n", dev_name(mt->dev), v);

	len += scnprintf(buf + len, PAGE_SIZE - len, "AXI_status: 0x%x pattern: 0x%x\n",
			mt->axi_status, mt->pattern);
	len += scnprintf(buf + len, PAGE_SIZE - len, "check_status_cnt %ld fail_status: 0x%x\n",
			mt->check_status_cnt, mt->fail_status);

	data_width = (v >> 20) & 0x3;
	if (data_width == 0)
		len += scnprintf(buf + len, PAGE_SIZE - len, "data_width: 64bit\n");
	else if (data_width == 0x1)
		len += scnprintf(buf + len, PAGE_SIZE - len, "data_width: 128bit\n");
	else if (data_width == 0x2)
		len += scnprintf(buf + len, PAGE_SIZE - len, "data_width: 256bit\n");
	else
		len += scnprintf(buf + len, PAGE_SIZE - len, "data_width: invalid\n");

	delta = ktime_get_ns() - mt->start_time;

	/*
	 * calculate the total size simply:
	 *  1loop: axlen * axsize * xact_num --> 8 * 16 * 4096 = 512K = 1/2M
	 *  256loop: MT_REG_LOOP_TIME + 1
	 *           1/2M * 256 = 128M
	 */
	loops = readl(mt->reg + MT_REG_LOOP_TIME);

	/* unit M */
	real_size_mb = loops * 128;
	/* scale the real_size_mb by the data_width */
	if (data_width == 0)
		real_size_mb >>= 1;
	else if (data_width == 2)
		real_size_mb <<= 1;

	delta_ms = delta /(1000000ul);
	speed = (real_size_mb * 1000) / (delta_ms);

	len += scnprintf(buf + len, PAGE_SIZE - len, "total %lld-MB %lld-GB\n", real_size_mb, real_size_mb >> 10);
	len += scnprintf(buf + len, PAGE_SIZE - len, "delta %lld-ns %lld-ms\n", delta, delta_ms);
	len += scnprintf(buf + len, PAGE_SIZE - len, "speed %lld-MBps %lld-GBps\n", speed, speed >> 10);

	return len;
}

static ssize_t writespecific2_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	u64 addr;

	if (kstrtou64(buf, 0, &addr) < 0)
		return -EINVAL;
	zh_memtester_writespecific2(dev, addr);

	return count;
}

static DEVICE_ATTR_RW(writespecific2);

static struct attribute *mt_dev_attrs[] = {
	&dev_attr_writeonly.attr,
	&dev_attr_readonly.attr,
	&dev_attr_ctrl_mode.attr,
	&dev_attr_range.attr,
	&dev_attr_writespecific.attr,
	&dev_attr_writespecific2.attr,
	NULL
};

static const struct attribute_group mt_dev_attr_group = {
	.attrs = mt_dev_attrs
};

static const struct attribute_group *mt_dev_attr_groups[] = {
	&mt_dev_attr_group,
	NULL
};

static struct page** zh_memtester_get_pages(unsigned int pgcount)
{
	int i;
	struct page *p, **pages;
	
	pages = kvmalloc_array(pgcount, sizeof(struct page *), GFP_KERNEL);
	if (!pages)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < pgcount; i++) {
		unsigned long va;
		phys_addr_t pa;
		p = alloc_pages(GFP_KERNEL | GFP_DMA32, 0);
		if (!p)
			goto fail;
		pages[i] = p;
		va = (unsigned long)((void*)page_address(p));
		pa = __pa(va);
		if (i == 0)
			printk("%s alloc one page with va=0x%lx pa=0x%llx\n", __func__, va, pa);
	}

	return pages;

fail:
	for (i = 0; i < pgcount; i++) {
		if (pages[i])
			__free_page(pages[i]);
	}
	kvfree(pages);
	return ERR_PTR(-ENOMEM);
}

static struct sg_table *zh_memtester_pages_to_sg(struct page **pages, unsigned int pgcount)
{
	struct sg_table *sgt;
	int ret;
	
	sgt = kmalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (!sgt)
		return ERR_PTR(-ENOMEM);

	ret = sg_alloc_table_from_pages(sgt, pages, pgcount, 0,
		pgcount << PAGE_SHIFT, GFP_KERNEL);
	if (ret < 0) {
		printk("%s failed to create SG table from pages\n", __func__);
		kfree(sgt);
		sgt = ERR_PTR(ret);
	}
	return sgt;
}

static int zh_memtester_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res = NULL;
	struct zh_memtester_device *mt = NULL;
	int ret = 0;
	unsigned pgcount;
	struct scatterlist *sg;
	unsigned int i = 0;
	char name[50];

	mt = devm_kzalloc(dev, sizeof(*mt), GFP_KERNEL);
	if (!mt)
		return -ENOMEM;

	mt->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "could not find resource for register region\n");
		return -EINVAL;
	}

	mt->reg = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(mt->reg)) {
		ret = dev_err_probe(dev, PTR_ERR(mt->reg),
			"could not map register region\n");
		goto fail;
	}

	mt->misc.minor = MISC_DYNAMIC_MINOR;
	mt->misc.mode = 0644;
	mt->misc.groups = mt_dev_attr_groups;
	mt->misc.parent = dev;

	snprintf(name, sizeof(name), "memtester@%llx", res->start);
	mt->misc.name = kstrdup(name, GFP_KERNEL);
	if (!mt->misc.name) {
		goto fail;
	}

	ret = misc_register(&mt->misc);
	if (ret < 0)
		goto fail;

	dev_set_drvdata(mt->misc.this_device, mt);
	dev_set_drvdata(dev, mt);

	pgcount = MT_ALLOC_PAGE_CNT; // 64MB

	mt->pages = zh_memtester_get_pages(pgcount);

	if (IS_ERR(mt->pages)) {
		ret = PTR_ERR(mt->pages);
		goto fail;
	}
		
	mt->sgt = zh_memtester_pages_to_sg(mt->pages, pgcount);
	if (IS_ERR(mt->sgt)) {
		ret = PTR_ERR(mt->sgt);
		goto fail;
	}

	for_each_sg(mt->sgt->sgl, sg, mt->sgt->nents, i) {
		printk("got sg dma_addr=0x%llx phy=0x%llx vir=0x%px len=%d\n", sg_dma_address(sg), sg_phys(sg), sg_virt(sg), sg->length);
	}

	ret = dma_map_sgtable(dev, mt->sgt, DMA_FROM_DEVICE, 0);
	if (ret)
		goto out_free_sg_table;

	for_each_sg(mt->sgt->sgl, sg, mt->sgt->nents, i) {
		mt->wr_iova = sg_dma_address(sg);
		mt->wr_pa = sg_phys(sg);
		printk("mapped iova=0x%llx phy=0x%llx vir=0x%px len=%d\n", mt->wr_iova, mt->wr_pa, sg_virt(sg), sg->length);
	}

	/* default, all enable */
	mt->ctrl_mode = 0x7;
	mt->range = (1 << 20);

	INIT_DELAYED_WORK(&mt->check_status_work, check_status_delay_work);

	return ret;

out_free_sg_table:
	if (mt->pages) {
		for (i = 0; i < MT_ALLOC_PAGE_CNT; i++) {
			if (mt->pages[i])
				__free_page(mt->pages[i]);
		}
		kvfree(mt->pages);
	}
	sg_free_table(mt->sgt);
fail:
	if (mt->misc.name)
		kfree(mt->misc.name);
	if (mt)
		devm_kfree(dev, mt);
	return ret;
}

static int zh_memtester_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct zh_memtester_device *mt = dev_get_drvdata(dev);

	misc_deregister(&mt->misc);

	cancel_delayed_work_sync(&mt->check_status_work);

	if (mt->pages) {
		unsigned i;
		for (i = 0; i < MT_ALLOC_PAGE_CNT; i++) {
			if (mt->pages[i])
				__free_page(mt->pages[i]);
		}
		kvfree(mt->pages);
	}

	if (mt->sgt) {
		dma_unmap_sgtable(dev, mt->sgt, DMA_FROM_DEVICE, 0);
		sg_free_table(mt->sgt);
		kfree(mt->sgt);
	}

	return 0;
}

static const struct of_device_id zh_memtester_of_match[] = {
	{ .compatible = "zhihe,memtester", },
	{ /* sentinel */ },
};

static struct platform_driver zh_memtester_platform_driver = {
	.probe = zh_memtester_probe,
	.remove = zh_memtester_remove,
	.driver = {
		.name = ZH_MT_NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(zh_memtester_of_match),
	}
};

module_driver(zh_memtester_platform_driver, platform_driver_register,
	platform_driver_unregister);
