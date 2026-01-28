// SPDX-License-Identifier: GPL-2.0-only
/*
 * RISC-V IOMMU as a platform device
 *
 * Copyright © 2023 FORTH-ICS/CARV
 *
 * Author: Nick Kossifidis <mick@ics.forth.gr>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/of_platform.h>
#include <linux/bitfield.h>
#include <linux/pm_runtime.h>
#include <linux/pm_domain.h>
#include <linux/clk.h>
#include <linux/of_clk.h>
#include <linux/clk-provider.h>

#include "iommu-bits.h"
#include "iommu.h"

#define A210_IOMMU_CAP 0x28d0008210

static int riscv_iommu_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct zh_iommu_device *iommus = NULL;
	struct riscv_iommu_device *iommu = NULL;
	struct resource *res = NULL;
	struct device_node *np = dev->of_node;
	u32 fctl = 0;
	int irq = 0;
	int ret = 0;
	int count;

	iommus = devm_kzalloc(dev, sizeof(*iommus), GFP_KERNEL);
	if (!iommus)
		return -ENOMEM;
	iommus->dev = dev;
	mutex_init(&iommus->lock);
	INIT_LIST_HEAD(&iommus->iommus);
	INIT_LIST_HEAD(&iommus->groups);
	dev_set_drvdata(dev, iommus);
	dev_set_name(dev, "%s", "zhihe,iommu");

	count = of_count_phandle_with_args(np, "power-domains", "#power-domain-cells");
	if (count > 0) {
		iommus->pd_devs = devm_kcalloc(dev, count, sizeof(*iommus->pd_devs), GFP_KERNEL);
		if (!iommus->pd_devs)
			return -ENOMEM;
		iommus->num_pds = count;

		for (int i = 0; i < count; i++) {
			iommus->pd_devs[i] = dev_pm_domain_attach_by_id(dev, i);
			if (IS_ERR(iommus->pd_devs[i]))
				return PTR_ERR(iommus->pd_devs[i]);
			device_link_add(dev, iommus->pd_devs[i], DL_FLAG_PM_RUNTIME | DL_FLAG_STATELESS);
		}
	}

	/*
	 * Get and prepare clocks early, before any hardware access.
	 * Do NOT use pm_runtime_get_sync() before this, as it may trigger power
	 * domain operations that themselves need to prepare clocks, causing
	 * circular locking: prepare_lock → genpd_runtime_resume → genpd->mlock
	 * → a210_pd_power_on → clk_prepare → prepare_lock (deadlock).
	 */
	iommus->num_clks = devm_clk_bulk_get_all(dev, &iommus->clks);
	if (iommus->num_clks < 0)
		return dev_err_probe(dev, iommus->num_clks,
				     "Failed to get iommus's clocks\n");

	ret = clk_bulk_prepare_enable(iommus->num_clks, iommus->clks);
	if (ret)
		return ret;

	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	for (int i = 0; i < pdev->num_resources; i++) {
		const char *name;

		iommu = devm_kzalloc(dev, sizeof(*iommu), GFP_KERNEL);
		if (!iommu)
			return -ENOMEM;

		iommu->dev = dev;
		INIT_LIST_HEAD(&iommu->list);

		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!res) {
			dev_err(dev, "could not find resource for register region\n");
			return -EINVAL;
		}

		iommu->reg = devm_platform_get_and_ioremap_resource(pdev, i, &res);
		if (IS_ERR(iommu->reg)) {
			ret = dev_err_probe(dev, PTR_ERR(iommu->reg),
					"could not map register region\n");
			goto fail;
		}

		of_property_read_string_index(np, "reg-names", i, &name);
		iommu->name = kstrdup(name, GFP_KERNEL);

		iommu->reg_phys = res->start;

		ret = -ENODEV;

		/* Sanity check: Did we get the whole register space ? */
		if ((res->end - res->start + 1) < RISCV_IOMMU_REG_SIZE) {
			dev_err(dev, "device region smaller than register file (0x%llx)\n",
				res->end - res->start);
			goto fail;
		}

		iommu->cap = riscv_iommu_readq(iommu, RISCV_IOMMU_REG_CAP);
		if (iommu->cap != A210_IOMMU_CAP) {
			dev_err_probe(dev, -ENODEV,
				"IOMMU:%s Capacity Reg=0x%llx Error.Check clock power reset!\n", iommu->name, iommu->cap);
			goto fail;
		}

		/* For now we only support WSIs until we have AIA support */
		ret = FIELD_GET(RISCV_IOMMU_CAP_IGS, iommu->cap);
		if (ret == RISCV_IOMMU_CAP_IGS_MSI) {
			dev_err(dev, "IOMMU only supports MSIs\n");
			goto fail;
		}

		/* Parse IRQ assignment */
		irq = platform_get_irq(pdev, i);
		if (irq > 0)
			iommu->irq = irq;
		else {
			dev_err(dev, "no IRQ provided for iommu\n");
			goto fail;
		}

		/* Make sure fctl.WSI is set */
		fctl = riscv_iommu_readl(iommu, RISCV_IOMMU_REG_FCTL);
		fctl |= RISCV_IOMMU_FCTL_WSI;
		riscv_iommu_writel(iommu, RISCV_IOMMU_REG_FCTL, fctl);

		/* Parse Queue lengts */
		ret = of_property_read_u32(pdev->dev.of_node, "cmdq_len", &iommu->cmdq_len);
		if (!ret)
			dev_info(dev, "command queue length set to %i\n", iommu->cmdq_len);

		ret = of_property_read_u32(pdev->dev.of_node, "fltq_len", &iommu->fltq_len);
		if (!ret)
			dev_info(dev, "fault/event queue length set to %i\n", iommu->fltq_len);

		ret = riscv_iommu_init(iommu);
		if (ret) {
			dev_err(dev, "riscv_iommu_init failed %d", ret);
			goto fail;
		}

		list_add_tail(&iommu->list, &iommus->iommus);
	}

	dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));

	riscv_iommu_register(iommus);

	list_for_each_entry(iommu, &iommus->iommus, list) {
		//clear tlb with a tmp domain
		struct riscv_iommu_domain iommu_domain;
		iommu_domain.iommus = iommus;
		iommu_domain.pscid = 0;
		iommu_domain.domain.ops = iommus->iommu.ops->default_domain_ops;
		iommu_flush_iotlb_all(&iommu_domain.domain);
	}

 fail:
	/* Note: devres_release_all() will release iommu and iommu->reg */
	return ret;
};

static void riscv_iommu_platform_remove(struct platform_device *pdev)
{
	riscv_iommu_remove(dev_get_drvdata(&pdev->dev));
}

static void riscv_iommu_platform_shutdown(struct platform_device *pdev)
{
	return;
}

static const struct of_device_id riscv_iommu_of_match[] = {
	{.compatible = "zhihe,iommu",},
	{},
};

MODULE_DEVICE_TABLE(of, riscv_iommu_of_match);

static struct platform_driver riscv_iommu_platform_driver = {
	.driver = {
		   .name = "zhihe,iommu",
		   .of_match_table = riscv_iommu_of_match,
		   .suppress_bind_attrs = true,
		   },
	.probe = riscv_iommu_platform_probe,
	.remove_new = riscv_iommu_platform_remove,
	.shutdown = riscv_iommu_platform_shutdown,
};

module_driver(riscv_iommu_platform_driver, platform_driver_register,
	      platform_driver_unregister);
