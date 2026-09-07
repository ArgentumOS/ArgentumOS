/*
 * fnx/fs/fatfs/file.c — FNX-native FAT file content support.
 *
 * Regular files reuse the generic page-cache read path (file_read in
 * mm/page.c): bread_page maps a page through fsop->bmap() at
 * sb->s_blocksize (512) granularity. fat_bmap() FOR_READING walks the
 * file's cluster chain; FOR_WRITING additionally allocates + links
 * clusters (under superblock_lock) so the generic writers can extend the
 * file. The first cluster allocation is persisted to the parent dir
 * entry by write_inode (INODE_DIRTY).
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
	int errno;

	if(mode == FOR_READING && (__off_t)offset >= i->i_size) {
		return 0;
	}
	if(f->fs_type == FAT_EXFAT) {
		/* exFAT: contiguous streams map linearly (no FAT chain);
		 * fragmented streams walk the (full-32-bit) FAT */
		unsigned int idx = (unsigned int)(offset / cluster_bytes);
		unsigned int within = (unsigned int)(offset % cluster_bytes) / 512;

		if(mode != FOR_READING) {
			return ex_fat_bmap_write(i, offset);
		}
		if(i->u.fatfs.contiguous) {
			__u32 cl = i->u.fatfs.cluster + idx;

			if(cl < 2) {
				return 0;
			}
			return (__blk_t)(f->data_sector +
					 ((__u64)(cl - 2) *
					  f->sects_per_cluster) + within);
		}
		{
			__u32 cl = i->u.fatfs.cluster;
			unsigned int k;

			for(k = 0; k < idx; k++) {
				cl = fat_next_cluster(i->sb, cl);
				if(cl < 2 || cl >= 0xFFFFFFFFu) {
					return 0;
				}
			}
			return (__blk_t)(f->data_sector +
					 ((__u64)(cl - 2) *
					  f->sects_per_cluster) + within);
		}
	}
	if(!cluster) {
		if(mode != FOR_WRITING) {
			return 0;	/* empty file */
		}
		/* first allocation: extend the chain from nothing */
		superblock_lock(i->sb);
		if(!cluster && i->u.fatfs.cluster == 0) {
			if((errno = fat_alloc_cluster(i->sb, &cluster))) {
				superblock_unlock(i->sb);
				return errno;
			}
			i->u.fatfs.cluster = cluster;
		}
		superblock_unlock(i->sb);
	}
	want = (unsigned int)(offset / cluster_bytes);
	for(n = 0; n < want; n++) {
		__u32 next;

		superblock_lock(i->sb);
		if(fat_read_entry(i->sb, cluster, &next)) {
			superblock_unlock(i->sb);
			return -EIO;
		}
		if(next >= 2 && next < FAT_CLUST_LAST) {
			cluster = next;
			superblock_unlock(i->sb);
			continue;
		}
		if(mode != FOR_WRITING) {
			superblock_unlock(i->sb);
			return 0;	/* chain ended early (hole) */
		}
		/* extend: allocate + link */
		{
			__u32 nc;

			if((errno = fat_alloc_cluster(i->sb, &nc))) {
				superblock_unlock(i->sb);
				return errno;
			}
			fat_set_eoc(i->sb, nc);
			{
				/* fat_link writes 'next' at 'cluster' */
				struct buffer *b;
				__blk_t sect = (__blk_t)f->fat_sector +
					       cluster / 128;

				if(!(b = bread(i->sb->dev, sect, 512))) {
					superblock_unlock(i->sb);
					return -EIO;
				}
				((__u32 *)b->data)[cluster % 128] =
					(((__u32 *)b->data)[cluster % 128] &
					 0xF0000000) | nc;
				bwrite(b);
				brelse(b);
			}
			cluster = nc;
		}
		superblock_unlock(i->sb);
	}
	return fat_file_block(i, cluster, offset);
}

__loff_t fat_file_llseek(struct inode *i, __loff_t offset)
{
	return offset;
}
