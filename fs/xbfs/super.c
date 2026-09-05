/*
 * fnx/fs/xbfs/super.c
 *
 * XBFS (BeOS) filesystem: superblock, bitmap cache, registration, mount.
 *
 * The 512-byte superblock lives at offset 512 of block 0 (the first
 * 512 bytes are the boot block). Free space is tracked by a per-group
 * bit bitmap stored in the first blocks of each allocation group
 * (Haiku's disk_super_block convention: the sb's blocks_per_ag field
 * holds the number of bitmap blocks per group); the whole bitmap is
 * cached in memory and flushed by write_superblock.
 *
 * Copyright 2024, the FNX project.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/kernel.h>
#include <fnx/types.h>
#include <fnx/errno.h>
#include <fnx/fs.h>
#include <fnx/filesystems.h>
#include <fnx/xbfs.h>
#include <fnx/buffer.h>
#include <fnx/dirent.h>
#include <fnx/mm.h>
#include <fnx/statfs.h>
#include <fnx/sched.h>
#include <fnx/stdio.h>
#include <fnx/string.h>

/* file operations */
extern int xbfs_open(struct inode *, struct fd *);
extern int xbfs_close(struct inode *, struct fd *);
extern int file_read(struct inode *, struct fd *, char *, __size_t);
extern int xbfs_file_write(struct inode *, struct fd *, const char *, __size_t);
extern int xbfs_ioctl(struct inode *, struct fd *, int, addr_t);
extern __loff_t xbfs_file_llseek(struct inode *, __loff_t);
extern int xbfs_dir_read(struct inode *, struct fd *, char *, __size_t);
extern int xbfs_readdir(struct inode *, struct fd *, struct dirent *, __size_t);
extern int xbfs_readdir64(struct inode *, struct fd *, struct dirent64 *, __size_t);

/* inode operations */
extern int xbfs_read_inode(struct inode *);
extern int xbfs_write_inode(struct inode *);
extern int xbfs_bmap(struct inode *, __off_t, int);
extern int xbfs_lookup(const char *, struct inode *, struct inode **);
extern int xbfs_truncate(struct inode *, __off_t);
extern int xbfs_create(struct inode *, char *, int, __mode_t, struct inode **);
extern int xbfs_mkdir(struct inode *, char *, __mode_t);
extern int xbfs_mknod(struct inode *, char *, __mode_t, __dev_t);
extern int xbfs_link(struct inode *, struct inode *, char *);
extern int xbfs_unlink(struct inode *, struct inode *, char *);
extern int xbfs_rmdir(struct inode *, struct inode *);
extern int xbfs_rename(struct inode *, struct inode *, struct inode *, struct inode *, char *, char *);
extern int xbfs_symlink(struct inode *, char *, char *);
extern int xbfs_readlink(struct inode *, char *, __size_t);
extern int xbfs_followlink(struct inode *, struct inode *, struct inode **);
extern int xbfs_ialloc(struct inode *, int);
extern void xbfs_ifree(struct inode *);
extern int xbfs_getxattr(struct inode *, const char *, char *, __size_t);
extern int xbfs_setxattr(struct inode *, const char *, const char *, __size_t, int);
extern int xbfs_listxattr(struct inode *, char *, __size_t);
extern int xbfs_removexattr(struct inode *, const char *);

/* superblock operations */
static void xbfs_statfs(struct superblock *, struct statfs *);
static void xbfs_release_superblock(struct superblock *);
static int xbfs_write_superblock(struct superblock *);
static int xbfs_read_superblock(__dev_t, struct superblock *);

struct fs_operations xbfs_fsop = {
	FSOP_REQUIRES_DEV,
	0,

	xbfs_open,		/* open */
	xbfs_close,		/* close */
	xbfs_file_read,		/* read (inline files; else generic) */
	xbfs_file_write,		/* write */
	xbfs_ioctl,		/* ioctl */
	xbfs_file_llseek,	/* llseek */
	xbfs_readdir,		/* readdir */
	xbfs_readdir64,		/* readdir64 */
	NULL,			/* mmap */
	NULL,			/* select */

	xbfs_readlink,		/* readlink */
	xbfs_followlink,		/* followlink */
	xbfs_bmap,		/* bmap */
	xbfs_lookup,		/* lookup */
	xbfs_rmdir,		/* rmdir */
	xbfs_link,		/* link */
	xbfs_unlink,		/* unlink */
	xbfs_symlink,		/* symlink */
	xbfs_mkdir,		/* mkdir */
	xbfs_mknod,		/* mknod */
	xbfs_truncate,		/* truncate */
	xbfs_create,		/* create */
	xbfs_rename,		/* rename */

	NULL,			/* read_block */
	NULL,			/* write_block */

	xbfs_read_inode,		/* read_inode */
	xbfs_write_inode,	/* write_inode */
	xbfs_ialloc,		/* ialloc */
	xbfs_ifree,		/* ifree */
	xbfs_statfs,		/* statfs */
	xbfs_read_superblock,	/* read_superblock */
	NULL,			/* remount_fs */
	xbfs_write_superblock,	/* write_superblock */
	xbfs_release_superblock,	/* release_superblock */

	xbfs_getxattr,		/* getxattr */
	xbfs_setxattr,		/* setxattr */
	xbfs_listxattr,		/* listxattr */
	xbfs_removexattr,	/* removexattr */
	xbfs_destroy_inode,	/* destroy_inode (kept last) */
};

static void xbfs_statfs(struct superblock *sb, struct statfs *buf)
{
	buf->f_type = XBFS_SUPER_MAGIC1;
	buf->f_bsize = sb->s_blocksize;
	buf->f_blocks = sb->u.xbfs.num_blocks;
	buf->f_bfree = sb->u.xbfs.num_blocks - sb->u.xbfs.used_blocks;
	buf->f_bavail = buf->f_bfree;
	/* inodes are ordinary data blocks in XBFS; report the capacity and
	 * the remaining free blocks as the inode counts */
	buf->f_files = sb->u.xbfs.num_blocks;
	buf->f_ffree = buf->f_bfree;
	buf->f_namelen = XBFS_NAME_LEN;
}

static void xbfs_free_bitmap(struct superblock *sb)
{
	__u32 i;

	if(!sb->u.xbfs.bitmap) {
		return;
	}
	for(i = 0; i < sb->u.xbfs.bitmap_chunks; i++) {
		if(sb->u.xbfs.bitmap[i]) {
			kfree((addr_t)sb->u.xbfs.bitmap[i]);
		}
	}
	kfree((addr_t)sb->u.xbfs.bitmap);
	sb->u.xbfs.bitmap = NULL;
}

static void xbfs_release_superblock(struct superblock *sb)
{
	xbfs_unreg_sb(sb);
	struct buffer *buf;
	__u64 i;

	if(sb->flags & MS_RDONLY) {
		return;
	}

	/* stop journaling: from here on inode flushes write through
	 * directly, so nothing can re-populate the log after
	 * xbfs_write_superblock drains it (the VFS flushes more inodes
	 * after release_superblock) */
	sb->u.xbfs.log_draining = 1;

	/* flush the dirty inodes now: their writes go through the
	 * journal (still active), landing in the log before the drain */
	sync_inodes(sb->dev);

	superblock_lock(sb);
	sb->u.xbfs.flags = XBFS_SUPER_CLEAN;
	sb->state = SUPERBLOCK_DIRTY;
	superblock_unlock(sb);

	/* keep the bitmap: the final write_superblock drain must flush it
	 * (a mid-session write-through can leave the on-disk bitmap at an
	 * intermediate state that the drain would otherwise skip - the
	 * release_superblock bitmap free used to orphan the last frees).
	 * write_superblock frees it once the drain has flushed. */
}

/*
 * Write the superblock + the cached bitmap back to disk.
 */
static int xbfs_write_superblock(struct superblock *sb)
{
	struct buffer *buf;
	struct xbfs_superblock *bsb;
	unsigned char bsb_name[32];
	__u32 i;
	__u64 l;

	superblock_lock(sb);

	/* close any pending group-commit batch before the drain: its log
	 * entries + real blocks must be published + synced first, or the
	 * drain would zero the log over transactions that were never made
	 * durable */
	xbfs_log_flush(sb);

	/* empty the log: by the time the superblock is synced every
	 * committed transaction is already applied + synced, so the log
	 * can be drained (log_start == log_end == 0 is the clean state).
	 * This runs after the last dirty inode has been flushed (the
	 * umount path iputs root/dir before sync_superblocks), so a
	 * clean unmount leaves nothing to replay. The journal lock keeps
	 * an in-flight commit from writing the superblock (log_end) in
	 * the middle of the drain. */
	xbfs_log_lock(sb);
	for(l = 0; l < sb->u.xbfs.log_blocks.len; l++) {
		if((buf = bread(sb->dev,
				xbfs_log_run_abs(sb, &sb->u.xbfs.log_blocks) + l,
				sb->u.xbfs.block_size))) {
			memset_b(buf->data, 0, sb->u.xbfs.block_size);
			bwrite(buf);
		}
	}
	sb->u.xbfs.log_start = 0;
	sb->u.xbfs.log_end = 0;
	sb->u.xbfs.flags = XBFS_SUPER_CLEAN;
	xbfs_log_unlock(sb);

	/* flush the bitmap blocks. The bitmap lives until the umount
	 * drain flushes it (release_superblock no longer frees it), so
	 * this always runs and the on-disk bitmap matches the final
	 * used_blocks counter. */
	if(sb->u.xbfs.bitmap) {
		for(i = 0; i < sb->u.xbfs.bitmap_blocks; i++) {
			struct buffer *bb;
			__u32 chunk = (i * sb->u.xbfs.block_size) >> 12;
			__u32 coff = (i * sb->u.xbfs.block_size) & (PAGE_SIZE - 1);
			if(!(bb = bread(sb->dev, 1 + i, sb->u.xbfs.block_size))) {
				superblock_unlock(sb);
				return -EIO;
			}
			memcpy_b(bb->data, sb->u.xbfs.bitmap[chunk] + coff,
				sb->u.xbfs.block_size);
			bwrite(bb);
		}
		/* final drain: the bitmap is no longer needed */
		if(sb->u.xbfs.log_draining) {
			xbfs_free_bitmap(sb);
		}
	}

	/* rebuild the superblock */
	if(!(buf = bread(sb->dev, 0, sb->u.xbfs.block_size))) {
		superblock_unlock(sb);
		return -EIO;
	}
	bsb = (struct xbfs_superblock *)(buf->data + 512);
	/* clear only the struct (0..0x84); the superblock's reserved area
	 * (0x84..0x200) is NOT ours to zero — on real Haiku volumes it
	 * holds the boot loader's second stage (stage1 reads it), so a
	 * full memset here breaks booting the volume under Haiku */
	memcpy_b(bsb_name, bsb->name, 32);
	memset_b(bsb, 0, 0x84);
	/* preserve the on-disk volume name (mkxbfs volumes keep "XBFS",
	 * Haiku BFS volumes keep e.g. "Haiku" — clobbering it changes the
	 * label Haiku displays on volumes we mount read-write) */
	memcpy_b(bsb->name, bsb_name, 32);
	bsb->magic1 = XBFS_SUPER_MAGIC1;
	bsb->fs_byte_order = XBFS_SUPER_BYTEORDER;
	bsb->block_size = sb->u.xbfs.block_size;
	bsb->block_shift = sb->s_blocksize_bits;
	bsb->num_blocks = sb->u.xbfs.num_blocks;
	bsb->used_blocks = sb->u.xbfs.used_blocks;
	bsb->inode_size = sb->s_blocksize;
	bsb->magic2 = XBFS_SUPER_MAGIC2;
	bsb->blocks_per_ag = sb->u.xbfs.blocks_per_ag;
	bsb->ag_shift = sb->u.xbfs.ag_shift;
	bsb->num_ags = sb->u.xbfs.num_ags;
	bsb->flags = sb->u.xbfs.flags;
	bsb->log_blocks = sb->u.xbfs.log_blocks;
	bsb->log_start = sb->u.xbfs.log_start;
	bsb->log_end = sb->u.xbfs.log_end;
	bsb->magic3 = XBFS_SUPER_MAGIC3;
	/* the in-memory root/indices inode numbers are ABSOLUTE blocks;
	 * the on-disk block_run stores them split into (ag, start) —
	 * an absolute number in the u16 start field truncates at block
	 * 65535 (Haiku's root sits at block 131072: 0x20000 -> 0). */
	xbfs_run_encode(&bsb->root_dir, sb->u.xbfs.root_inode, sb->u.xbfs.ag_shift);
	bsb->root_dir.len = 1;
	/* preserve the indices directory run (Haiku keeps it across
	 * unmount; the in-memory form is the indices inode's block) */
	if(sb->u.xbfs.indices_inode) {
		xbfs_run_encode(&bsb->indices, sb->u.xbfs.indices_inode,
			       sb->u.xbfs.ag_shift);
		bsb->indices.len = 1;
	} else {
		bsb->indices.allocation_group = 0;
		bsb->indices.start = 0;
		bsb->indices.len = 0;
	}
	xbfs_sb_dual_write(sb, buf);

	sb->state &= ~SUPERBLOCK_DIRTY;
	superblock_unlock(sb);
	return 0;
}

/* Dual-copy superblock flush: the caller filled the copy-A struct at
 * XBFS_SB_A_OFF in buf->data; stamp both copies with the same new
 * sequence + a checksum of the struct, then write the block + sync.
 * Each copy is its own 512-byte sector, so a host/device-level kill
 * between the two sector writes can tear only the later one; the mount
 * (xbfs_read_superblock) takes the valid copy with the highest
 * sequence. */
/* stamp both superblock copies + write the buffer. With flush=1 the
 * write is barriered by a full sync_buffers (the tear-atomic publish
 * used by the wrap and the write-through path); with flush=0 the
 * caller owns the barrier (the group-commit flush selectively syncs
 * the sb + bitmap BEFORE the real metadata blocks, which are already
 * dirty in the cache). */
static void xbfs_sb_dual_stamp(struct superblock *sb, struct buffer *buf)
{
	unsigned char *a, *b;
	__u64 seq;
	__u32 cksum;
	int j;

	a = buf->data + XBFS_SB_A_OFF;
	b = buf->data + XBFS_SB_B_OFF;
	seq = ++sb->u.xbfs.sb_seq;
	/* the struct is identical in both copies; re-stamp the tail */
	memcpy_b(b, a, 512);
	memset_b(a + XBFS_SB_SEQ_OFF, 0, 512 - XBFS_SB_SEQ_OFF);
	memset_b(b + XBFS_SB_SEQ_OFF, 0, 512 - XBFS_SB_SEQ_OFF);
	cksum = 0;
	for(j = 0; j < XBFS_SB_SEQ_OFF; j += 4) {
		cksum += *(const __u32 *)(a + j);
	}
	*(__u64 *)(a + XBFS_SB_SEQ_OFF) = seq;
	*(__u64 *)(b + XBFS_SB_SEQ_OFF) = seq;
	*(__u32 *)(a + XBFS_SB_CKSUM_OFF) = cksum;
	*(__u32 *)(b + XBFS_SB_CKSUM_OFF) = cksum;
	bwrite(buf);
}

int xbfs_sb_dual_write(struct superblock *sb, struct buffer *buf)
{
	xbfs_sb_dual_stamp(sb, buf);
	sync_buffers(sb->dev);
	return 0;
}

int xbfs_sb_dual_write_nosync(struct superblock *sb, struct buffer *buf)
{
	xbfs_sb_dual_stamp(sb, buf);
	return 0;
}

static int xbfs_read_superblock(__dev_t dev, struct superblock *sb)
{
	struct buffer *buf;
	struct xbfs_superblock *bsb;
	struct xbfs_block_run *root;
	__u32 root_block;
	__u32 i;

	superblock_lock(sb);
	/* the volume's block size is unknown yet; the 512-byte superblock
	 * lives in the first 1024 bytes of block 0 for every valid size,
	 * so a 1KB probe read gets it (the block_size field decides the
	 * size of every later read) */
	if(!(buf = bread(dev, 0, XBFS_BLOCK_SIZE))) {
		printk("WARNING: %s(): I/O error on device %d,%d.\n",
		       __FUNCTION__, MAJOR(dev), MINOR(dev));
		superblock_unlock(sb);
		return -EIO;
	}

	/* dual-copy superblock: both 512-byte copies sit inside block 0
	 * (copy A @512, copy B @0 = the boot sector, free on XBFS
	 * volumes). Validate both and take the valid one with the highest
	 * sequence; a torn write can damage only the copy it was
	 * mid-write on (each copy is its own sector), so the other copy
	 * recovers the mount. */
	bsb = (struct xbfs_superblock *)(buf->data + XBFS_SB_A_OFF);
	{
		__u32 best_seq = 0, i;
		int best = -1;
		struct xbfs_superblock *cand;
		for(i = 0; i < 2; i++) {
			unsigned char *area = buf->data +
				(i ? XBFS_SB_B_OFF : XBFS_SB_A_OFF);
			__u32 cksum = 0, stored;
			int j;
			cand = (struct xbfs_superblock *)area;
			if(cand->magic1 != XBFS_SUPER_MAGIC1 ||
			   cand->magic2 != XBFS_SUPER_MAGIC2 ||
			   cand->magic3 != XBFS_SUPER_MAGIC3) {
				continue;
			}
			for(j = 0; j < XBFS_SB_SEQ_OFF; j += 4) {
				cksum += *(const __u32 *)(area + j);
			}
			stored = *(const __u32 *)(area + XBFS_SB_CKSUM_OFF);
			if(*(const __u64 *)(area + XBFS_SB_SEQ_OFF) == 0) {
				/* pre-dual-copy format: no checksum */
				if(best < 0) {
					best = i;
				}
				continue;
			}
			if(cksum != stored) {
				continue;
			}
			if(*(const __u64 *)(area + XBFS_SB_SEQ_OFF) > best_seq) {
				best_seq = *(const __u64 *)(area + XBFS_SB_SEQ_OFF);
				best = i;
			}
		}
		if(best < 0) {
			printk("WARNING: %s(): invalid filesystem type or bad superblock on device %d,%d.\n",
			       __FUNCTION__, MAJOR(dev), MINOR(dev));
			superblock_unlock(sb);
			brelse(buf);
			return -EINVAL;
		}
		if(best == 1 && best_seq > 0) {
			printk("XBFS: superblock copy B (block 0, seq %lu) recovered "
			       "a torn copy A.\n", best_seq);
		}
		bsb = (struct xbfs_superblock *)(buf->data + XBFS_SB_A_OFF);
		if(best == 1) {
			/* mount from copy B: mirror it into copy A's slot so the
			 * in-memory state and every later write start from it */
			memcpy_b(buf->data + XBFS_SB_A_OFF, buf->data + XBFS_SB_B_OFF,
				 512);
			bsb = (struct xbfs_superblock *)(buf->data + XBFS_SB_A_OFF);
		}
		sb->u.xbfs.sb_seq = best_seq;
	}

	if(bsb->block_size < XBFS_MIN_BLOCK_SIZE
	   || bsb->block_size > XBFS_MAX_BLOCK_SIZE
	   || (bsb->block_size & (bsb->block_size - 1))) {
		printk("WARNING: %s(): unsupported XBFS block size %d.\n",
		       __FUNCTION__, bsb->block_size);
		superblock_unlock(sb);
		brelse(buf);
		return -EINVAL;
	}
	/* inode_size must equal block_size (Haiku's IsValid) */
	if(bsb->inode_size != bsb->block_size) {
		printk("WARNING: %s(): inode_size %d != block_size %d.\n",
		       __FUNCTION__, bsb->inode_size, bsb->block_size);
		superblock_unlock(sb);
		brelse(buf);
		return -EINVAL;
	}

	sb->dev = dev;
	sb->fsop = &xbfs_fsop;
	sb->s_blocksize_bits = XBFS_MIN_BLOCK_SHIFT;
	while((1u << sb->s_blocksize_bits) < bsb->block_size) {
		sb->s_blocksize_bits++;
	}
	sb->s_blocksize = bsb->block_size;
	sb->u.xbfs.block_size = bsb->block_size;
	sb->u.xbfs.blocks_per_ag = bsb->blocks_per_ag;
	sb->u.xbfs.ag_shift = bsb->ag_shift;
	sb->u.xbfs.num_ags = bsb->num_ags;
	sb->u.xbfs.num_blocks = bsb->num_blocks;
	sb->u.xbfs.used_blocks = bsb->used_blocks;
	sb->u.xbfs.flags = bsb->flags;
	sb->u.xbfs.next_free = 0;

	root = &bsb->root_dir;
	root_block = (root->allocation_group << bsb->ag_shift) + root->start;
	sb->u.xbfs.root_inode = root_block;

	/* the indices directory (0 when the volume has none) */
	if(bsb->indices.len) {
		sb->u.xbfs.indices_inode = (bsb->indices.allocation_group
			<< bsb->ag_shift) + bsb->indices.start;
	} else {
		sb->u.xbfs.indices_inode = 0;
	}

	/* journal (log) state: the extent + positions come from the disk */
	sb->u.xbfs.log_blocks = bsb->log_blocks;
	sb->u.xbfs.log_start = bsb->log_start;
	sb->u.xbfs.log_end = bsb->log_end;
	sb->u.xbfs.tx_depth = 0;
	sb->u.xbfs.tx_nblocks = 0;
	sb->u.xbfs.journal_locked = 0;
	sb->u.xbfs.journal_wanted = 0;

	/* load the bitmap into memory (chunked: a large volume's bitmap is
	 * many times the kmalloc cap of one page) */
	sb->u.xbfs.bitmap_blocks = sb->u.xbfs.num_ags * sb->u.xbfs.blocks_per_ag;
	sb->u.xbfs.bitmap_chunks =
		((sb->u.xbfs.bitmap_blocks * sb->u.xbfs.block_size)
		 + PAGE_SIZE - 1) / PAGE_SIZE;
	if(!(sb->u.xbfs.bitmap = (unsigned char **)kmalloc(
			sb->u.xbfs.bitmap_chunks * sizeof(unsigned char *)))) {
		printk("WARNING: %s(): unable to allocate the bitmap table.\n",
		       __FUNCTION__);
		superblock_unlock(sb);
		brelse(buf);
		return -ENOMEM;
	}
	for(i = 0; i < (int)sb->u.xbfs.bitmap_chunks; i++) {
		sb->u.xbfs.bitmap[i] = NULL;
	}
	for(i = 0; i < (int)sb->u.xbfs.bitmap_blocks; i++) {
		struct buffer *bb;
		__u32 chunk = (i * sb->u.xbfs.block_size) >> 12;
		__u32 coff = (i * sb->u.xbfs.block_size) & (PAGE_SIZE - 1);
		if(!sb->u.xbfs.bitmap[chunk]) {
			if(!(sb->u.xbfs.bitmap[chunk] =
					(unsigned char *)kmalloc(PAGE_SIZE))) {
				printk("WARNING: %s(): unable to allocate the bitmap.\n",
				       __FUNCTION__);
				xbfs_free_bitmap(sb);
				superblock_unlock(sb);
				brelse(buf);
				return -ENOMEM;
			}
			memset_b(sb->u.xbfs.bitmap[chunk], 0, PAGE_SIZE);
		}
		if(!(bb = bread(dev, 1 + i, sb->u.xbfs.block_size))) {
			superblock_unlock(sb);
			brelse(buf);
			return -EIO;
		}
		memcpy_b(sb->u.xbfs.bitmap[chunk] + coff,
			bb->data, sb->u.xbfs.block_size);
		brelse(bb);
	}

	/* replay any uncommitted transactions in the log before the fs
	 * becomes usable; a partial replay (I/O error / bad entry) leaves
	 * the log intact and refuses the mount rather than mounting on
	 * an unrecovered filesystem */
	if(xbfs_log_replay(sb) < 0) {
		printk("WARNING: %s(): log replay failed, refusing mount.\n",
		       __FUNCTION__);
		xbfs_free_bitmap(sb);
		superblock_unlock(sb);
		brelse(buf);
		return -EIO;
	}

	if(!(sb->root = iget(sb, root_block))) {
		printk("WARNING: %s(): unable to get root inode.\n",
		       __FUNCTION__);
		xbfs_free_bitmap(sb);
		superblock_unlock(sb);
		brelse(buf);
		return -EINVAL;
	}

	if(!(sb->flags & MS_RDONLY)) {
		sb->u.xbfs.flags = XBFS_SUPER_DIRTY;
		sb->state |= SUPERBLOCK_DIRTY;
	}
	brelse(buf);
	xbfs_reg_sb(sb);
	superblock_unlock(sb);
	return 0;
}

int xbfs_init(void)
{
	return register_filesystem("xbfs", &xbfs_fsop);
}
