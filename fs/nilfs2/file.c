// SPDX-License-Identifier: GPL-2.0+
/*
 * NILFS regular file handling primitives including fsync().
 *
 * Copyright (C) 2005-2008 Nippon Telegraph and Telephone Corporation.
 *
 * Written by Amagai Yoshiji and Ryusuke Konishi.
 */

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/writeback.h>
#include "nilfs.h"
#include "segment.h"

int nilfs_sync_file(struct file *file, loff_t start, loff_t end, int datasync)
{
	/*
	 * Called from fsync() system call
	 * This is the only entry point that can catch write and synch
	 * timing for both data blocks and intermediate blocks.
	 *
	 * This function should be implemented when the writeback function
	 * will be implemented.
	 */
	struct the_nilfs *nilfs;
	struct inode *inode = file->f_mapping->host;
	int err = 0;

	if (nilfs_inode_dirty(inode)) {
		if (datasync)
			err = nilfs_construct_dsync_segment(inode->i_sb, inode,
							    start, end);
		else
			err = nilfs_construct_segment(inode->i_sb);
	}

	nilfs = inode->i_sb->s_fs_info;
	if (!err)
		err = nilfs_flush_device(nilfs);

	return err;
}

static vm_fault_t nilfs_page_mkwrite(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct folio *folio = page_folio(vmf->page);
	struct inode *inode = file_inode(vma->vm_file);
	struct nilfs_transaction_info ti;
	struct buffer_head *bh, *head;
	int ret = 0;

	if (unlikely(nilfs_near_disk_full(inode->i_sb->s_fs_info)))
		return VM_FAULT_SIGBUS; /* -ENOSPC */

	sb_start_pagefault(inode->i_sb);
	folio_lock(folio);
	if (folio->mapping != inode->i_mapping ||
	    folio_pos(folio) >= i_size_read(inode) ||
	    !folio_test_uptodate(folio)) {
		folio_unlock(folio);
		ret = -EFAULT;	/* make the VM retry the fault */
		goto out;
	}

	/*
	 * check to see if the folio is mapped already (no holes)
	 */
	if (folio_test_mappedtodisk(folio))
		goto mapped;

	head = folio_buffers(folio);
	if (head) {
		int fully_mapped = 1;

		bh = head;
		do {
			if (!buffer_mapped(bh)) {
				fully_mapped = 0;
				break;
			}
		} while (bh = bh->b_this_page, bh != head);

		if (fully_mapped) {
			folio_set_mappedtodisk(folio);
			goto mapped;
		}
	}
	folio_unlock(folio);

	/*
	 * fill hole blocks
	 */
	ret = nilfs_transaction_begin(inode->i_sb, &ti, 1);
	/* never returns -ENOMEM, but may return -ENOSPC */
	if (unlikely(ret))
		goto out;

	file_update_time(vma->vm_file);
	ret = block_page_mkwrite(vma, vmf, nilfs_get_block);
	if (ret) {
		nilfs_transaction_abort(inode->i_sb);
		goto out;
	}
	nilfs_set_file_dirty(inode, 1 << (PAGE_SHIFT - inode->i_blkbits));
	nilfs_transaction_commit(inode->i_sb);

 mapped:
	/*
	 * Since checksumming including data blocks is performed to determine
	 * the validity of the log to be written and used for recovery, it is
	 * necessary to wait for writeback to finish here, regardless of the
	 * stable write requirement of the backing device.
	 */
	folio_wait_writeback(folio);
 out:
	sb_end_pagefault(inode->i_sb);
	return vmf_fs_error(ret);
}

static const struct vm_operations_struct nilfs_file_vm_ops = {
	.fault		= filemap_fault,
	.map_pages	= filemap_map_pages,
	.page_mkwrite	= nilfs_page_mkwrite,
};

static int nilfs_file_mmap_prepare(struct vm_area_desc *desc)
{
	file_accessed(desc->file);
	desc->vm_ops = &nilfs_file_vm_ops;
	return 0;
}

/*
 * We have mostly NULL's here: the current defaults are ok for
 * the nilfs filesystem.
 */
const struct file_operations nilfs_file_operations = {
	.llseek		= generic_file_llseek,
	.read_iter	= generic_file_read_iter,
	.write_iter	= generic_file_write_iter,
	.unlocked_ioctl	= nilfs_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= nilfs_compat_ioctl,
#endif	/* CONFIG_COMPAT */
	.mmap_prepare	= nilfs_file_mmap_prepare,
	.open		= generic_file_open,
	/* .release	= nilfs_release_file, */
	.fsync		= nilfs_sync_file,
	.splice_read	= filemap_splice_read,
	.splice_write   = iter_file_splice_write,
};

const struct inode_operations nilfs_file_inode_operations = {
	.setattr	= nilfs_setattr,
	.permission     = nilfs_permission,
	.fiemap		= nilfs_fiemap,
	.fileattr_get	= nilfs_fileattr_get,
	.fileattr_set	= nilfs_fileattr_set,
};

/* end of file */
