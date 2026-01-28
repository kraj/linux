/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Zhihe Computing Limited.
 */

#include <linux/kernel.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>

DEFINE_PER_CPU(struct page *, a210_iomb_page);
DEFINE_PER_CPU(void *, a210_iomb_flag0);
DEFINE_PER_CPU(void *, a210_iomb_flag1);
DEFINE_PER_CPU(void *, a210_iomb_flag2);
DEFINE_PER_CPU(void *, a210_iomb_flag3);
DEFINE_PER_CPU(u32, a210_iomb_data);

inline void a210_iomb(void)
{
	unsigned long flags;
	unsigned int cpu;

	local_irq_save(flags);

	cpu = smp_processor_id();

	mb();
	*(unsigned int *)per_cpu(a210_iomb_flag0, cpu) = per_cpu(a210_iomb_data, cpu);
	*(unsigned int *)per_cpu(a210_iomb_flag1, cpu) = per_cpu(a210_iomb_data, cpu);
	*(unsigned int *)per_cpu(a210_iomb_flag2, cpu) = per_cpu(a210_iomb_data, cpu);
	*(unsigned int *)per_cpu(a210_iomb_flag3, cpu) = per_cpu(a210_iomb_data, cpu);
	mb();
	while (*(unsigned int *)per_cpu(a210_iomb_flag0, cpu) != per_cpu(a210_iomb_data, cpu)) {;}
	while (*(unsigned int *)per_cpu(a210_iomb_flag1, cpu) != per_cpu(a210_iomb_data, cpu)) {;}
	while (*(unsigned int *)per_cpu(a210_iomb_flag2, cpu) != per_cpu(a210_iomb_data, cpu)) {;}
	while (*(unsigned int *)per_cpu(a210_iomb_flag3, cpu) != per_cpu(a210_iomb_data, cpu)) {;}
	per_cpu(a210_iomb_data, cpu)++;
	mb();

	local_irq_restore(flags);
}
EXPORT_SYMBOL(a210_iomb);

inline void a210_iowmb(void)
{
	unsigned long flags;
	unsigned int cpu;

	local_irq_save(flags);

	cpu = smp_processor_id();

	wmb();
	*(unsigned int *)per_cpu(a210_iomb_flag0, cpu) = per_cpu(a210_iomb_data, cpu);
	*(unsigned int *)per_cpu(a210_iomb_flag1, cpu) = per_cpu(a210_iomb_data, cpu);
	*(unsigned int *)per_cpu(a210_iomb_flag2, cpu) = per_cpu(a210_iomb_data, cpu);
	*(unsigned int *)per_cpu(a210_iomb_flag3, cpu) = per_cpu(a210_iomb_data, cpu);
	wmb();
	while (*(unsigned int *)per_cpu(a210_iomb_flag0, cpu) != per_cpu(a210_iomb_data, cpu)) {;}
	while (*(unsigned int *)per_cpu(a210_iomb_flag1, cpu) != per_cpu(a210_iomb_data, cpu)) {;}
	while (*(unsigned int *)per_cpu(a210_iomb_flag2, cpu) != per_cpu(a210_iomb_data, cpu)) {;}
	while (*(unsigned int *)per_cpu(a210_iomb_flag3, cpu) != per_cpu(a210_iomb_data, cpu)) {;}
	per_cpu(a210_iomb_data, cpu)++;
	wmb();

	local_irq_restore(flags);
}
EXPORT_SYMBOL(a210_iowmb);

static int __init a210_iomb_init(void)
{
	unsigned int cpu;

	for_each_possible_cpu(cpu) {
		per_cpu(a210_iomb_page, cpu) = alloc_page(GFP_KERNEL);
		if (!per_cpu(a210_iomb_page, cpu))
			return -ENOMEM;

		per_cpu(a210_iomb_flag0, cpu) = vmap(&per_cpu(a210_iomb_page, cpu), 1,
						     VM_DMA_COHERENT, pgprot_writecombine(PAGE_KERNEL));
		per_cpu(a210_iomb_flag1, cpu) = per_cpu(a210_iomb_flag0, cpu) + 128;
		per_cpu(a210_iomb_flag2, cpu) = per_cpu(a210_iomb_flag1, cpu) + 128;
		per_cpu(a210_iomb_flag3, cpu) = per_cpu(a210_iomb_flag2, cpu) + 128;

		per_cpu(a210_iomb_data, cpu) = 1;
	}

	return 0;
}

pure_initcall(a210_iomb_init);

MODULE_AUTHOR("dong.yan <yand@zhcomputing.com>");
MODULE_DESCRIPTION("Memory barrier work around for a210 ncore");
MODULE_LICENSE("GPL v2");
