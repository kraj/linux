// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 1993 by Theodore Ts'o.
 */
#define DEVICE_NAME "mmblk"

#include <linux/major.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/blk-mq.h>
#include <linux/bio.h>
#include <linux/hdreg.h>
#include <linux/of_address.h>

#define DEVICE_SIZE (64 * 1024 * 1024) // 64MB device

static DEFINE_MUTEX(loop_ctl_mutex);

struct pamem_device {
	int major;
	void *data;
	u64 size;
	int flag; // 0: vmalloc, 1: ioremap
	phys_addr_t start, end;

	spinlock_t lock;

	struct blk_mq_tag_set tag_set;
	struct gendisk *gd;
} *memblk = NULL;

static int mm_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	geo->heads = 1;
	geo->cylinders = 1;
	geo->sectors = get_capacity(bdev->bd_disk);
	geo->start = 0;

	return 0;
}

static const struct block_device_operations memblk_fops = {
	.owner = THIS_MODULE,
	.getgeo = mm_getgeo,
};

static inline unsigned int bio_cur_bytes(struct bio *bio)
{
	if (bio_has_data(bio))
		return bio_iovec(bio).bv_len;
	else /* dataless requests such as discard */
		return bio->bi_iter.bi_size;
}

static blk_status_t vdc_queue_rq(struct blk_mq_hw_ctx *hctx,
				 const struct blk_mq_queue_data *bd)
{
	struct request *req = bd->rq;
	struct bio *bio = req->bio;
	struct pamem_device *mb = req->part->bd_disk->private_data;
	unsigned long start = blk_rq_pos(req) << SECTOR_SHIFT;

	blk_mq_start_request(bd->rq);
	spin_lock_irq(&mb->lock);

	for_each_bio(bio) {
		void *buffer = bio_data(bio);
		size_t size = bio_cur_bytes(bio);
		if (rq_data_dir(req) == READ)
			memcpy(buffer, mb->data + start, size);
		else
			memcpy(mb->data + start, buffer, size);
		start += size;
	}

	spin_unlock_irq(&mb->lock);
	blk_mq_end_request(req, BLK_STS_OK);

	return BLK_STS_OK;
}

static const struct blk_mq_ops vdc_mq_ops = {
	.queue_rq = vdc_queue_rq,
};

static int __init memblk_init(void)
{
	int ret = -ENOMEM;

	struct device_node *reserved_memory_node;

	struct gendisk *gd;
	memblk = kzalloc(sizeof(*memblk), GFP_KERNEL);
	if (!memblk)
		return ret;

	memblk->tag_set.ops = &vdc_mq_ops;
	memblk->tag_set.nr_hw_queues = 1;
	memblk->tag_set.nr_maps = 1;
	memblk->tag_set.queue_depth = 16;
	memblk->tag_set.numa_node = NUMA_NO_NODE;
	memblk->tag_set.flags = BLK_MQ_F_SHOULD_MERGE;
	ret = blk_mq_alloc_tag_set(&memblk->tag_set);
	if (ret)
		goto out_unregister_blkdev;

	// Initialize the memory
	reserved_memory_node = of_find_node_by_name(NULL, "memblock-memory");
	if (!reserved_memory_node) {
		memblk->size = DEVICE_SIZE;
		memblk->data = vmalloc(memblk->size);
		memblk->start = (phys_addr_t)memblk->data;
		memblk->flag = 0;
		// ret = -ENODEV;
		// goto out_unregister_blkdev;
	} else {
		const __be32 *reg;
		int len;

		reg = of_get_property(reserved_memory_node, "reg", &len);
		if (!reg) {
			pr_err("Failed to get 'reg' property\n");
			ret = -ENODEV;
			goto out_free_tagset;
		}
		const __be32 *addr = of_get_address(reserved_memory_node, 0,
						    &memblk->size, NULL);
		memblk->start =
			of_translate_address(reserved_memory_node, addr);
		if (!memblk->start) {
			printk(KERN_ERR "memblk_init: Invalid device node !\n");
			ret = -EINVAL;
			goto out_free_tagset;
		}
		memblk->data = ioremap(memblk->start, memblk->size);
		memblk->flag = 1;
	}
	memblk->end = memblk->start + memblk->size;

	memblk->major = register_blkdev(0, DEVICE_NAME);
	spin_lock_init(&memblk->lock);

	if (!memblk->data)
		goto out_free_tagset;

	// Initialize the generic disk structure
	gd = memblk->gd = blk_mq_alloc_disk(&memblk->tag_set, memblk);
	if (!gd)
		goto out_free_tagset;

	gd->major = memblk->major;
	gd->first_minor = 0;
	gd->minors = 1;
	gd->flags |= GENHD_FL_NO_PART;

	gd->fops = &memblk_fops;
	gd->private_data = (void *)memblk;
	strcpy(gd->disk_name, DEVICE_NAME);

	set_capacity(gd, DEVICE_SIZE / SECTOR_SIZE);

	ret = add_disk(gd);
	if (ret)
		goto out_free_tagset;

	pr_info("Memory block init [%s %pa-%pa] (%lluMB)\n",
		memblk->flag == 1 ? "phymem" : "virtmem", &memblk->start,
		&memblk->end, memblk->size >> 20);

	return 0;

out_free_tagset:
	blk_mq_free_tag_set(&memblk->tag_set);
out_unregister_blkdev:
	if (memblk->major)
		unregister_blkdev(memblk->major, DEVICE_NAME);
	if (memblk->data) {
		if (memblk->flag == 0)
			vfree(memblk->data);
		else
			iounmap(memblk->data);
	}
	kfree(memblk);

	return ret;
}

static void __exit memblk_exit(void)
{
	mutex_lock(&loop_ctl_mutex);

	if (!memblk)
		return;

	del_gendisk(memblk->gd);
	blk_mq_free_tag_set(&memblk->tag_set);
	put_disk(memblk->gd);

	unregister_blkdev(memblk->major, DEVICE_NAME);

	kfree(memblk);
	mutex_unlock(&loop_ctl_mutex);
}

module_init(memblk_init);
module_exit(memblk_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("A simple in-memory block device driver.");
