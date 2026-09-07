/*
 * fnx/fs/fatfs/file.c — FNX-native FAT file content support.
 *
 * Regular files reuse the generic page-cache read path (file_read in
 * mm/page.c): bread_page maps a page through fsop->bmap() at
 * sb->s_blocksize (512) granularity, so fat_bmap() walks the file's
 * cluster chain to the 512-byte block containing a byte offset.
 * Write support (M1) adds the FAT allocation half of bmap FOR_WRITING.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#include <fnx/types.h>
#include <fnx/fs.h>
#include <fnx/stat.h>
#include <fnx/errno.h>
#include <fnx/string.h>
#include <fnx/buffer.h>
#include <fnx/kernel.h>
#include <fnx/sched.h>
#include "fat.h"

extern int file_read(struct inode *, struct fd *, char *, __size_t);

/* byte offset -> 512-byte device block (partition-relative) */
static __blk_t fat_file_block(struct inode *i, __u32 cluster, __off_t off)
{
	struct fatfs_sb_info *f = &i->sb->u.fatfs;
	unsigned int cluster_bytes = f->sects_per_cluster * 512;
	unsigned int within;

	off %= cluster_bytes;
	within = (unsigned int)off / 512;
	return fat_cluster_sector(i->sb, cluster, within);
}

__blk_t fat_bmap(struct inode *i, __off_t offset, int mode)
{
	struct fatfs_sb_info *f = &i->sb->u.fatfs;
	unsigned int cluster_bytes = f->sects_per_cluster * 512;
	unsigned int want;
	__u32 cluster = i->u.fatfs.cluster;
	unsigned int n;

	if(mode == FOR_READING && (__off_t)offset >= i->i_size) {
		return 0;
	}
	if(!cluster) {
		return 0;	/* empty file */
	}
	want = (unsigned int)(offset / cluster_bytes);
	for(n = 0; n < want; n++) {
		cluster = fat_next_cluster(i->sb, cluster);
		if(cluster < 2 || cluster >= FAT_CLUST_LAST) {
			return 0;	/* chain ended early */
		}
	}
	return fat_file_block(i, cluster, offset);
}

__loff_t fat_file_llseek(struct inode *i, __loff_t offset)
{
	return offset;
}
