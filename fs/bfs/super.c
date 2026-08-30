/*
 * fnx/fs/bfs/super.c
 *
 * BFS (BeOS) filesystem: superblock, registration, mount.
 *
 * Read-only support. The 512-byte superblock lives at offset 512 of
 * block 0 (the first 512 bytes are the boot block). Layout verified
 * against Linux fs/befs and Haiku's BFS implementation.
 *
 * Copyright 2024, the FNX project.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/kernel.h>
#include <fnx/types.h>
#include <fnx/errno.h>
#include <fnx/fs.h>
#include <fnx/filesystems.h>
#include <fnx/bfs.h>
#include <fnx/buffer.h>
#include <fnx/dirent.h>
#include <fnx/mm.h>
#include <fnx/statfs.h>
#include <fnx/sched.h>
#include <fnx/stdio.h>
#include <fnx/string.h>

/* file operations */
extern int bfs_open(struct inode *, struct fd *);
extern int bfs_close(struct inode *, struct fd *);
extern int file_read(struct inode *, struct fd *, char *, __size_t);
extern int bfs_dir_read(struct inode *, struct fd *, char *, __size_t);
extern int bfs_readdir(struct inode *, struct fd *, struct dirent *, __size_t);
extern int bfs_readdir64(struct inode *, struct fd *, struct dirent64 *, __size_t);

/* inode operations */
extern int bfs_read_inode(struct inode *);
extern int bfs_bmap(struct inode *, __off_t, int);
extern int bfs_lookup(const char *, struct inode *, struct inode **);

/* superblock operations */
static void bfs_statfs(struct superblock *, struct statfs *);
static void bfs_release_superblock(struct superblock *);
static int bfs_read_superblock(__dev_t, struct superblock *);

struct fs_operations bfs_fsop = {
	FSOP_REQUIRES_DEV,
	0,

	bfs_open,		/* open */
	bfs_close,		/* close */
	file_read,		/* read */
	NULL,			/* write */
	NULL,			/* ioctl */
	NULL,			/* llseek */
	bfs_readdir,		/* readdir */
	bfs_readdir64,		/* readdir64 */
	NULL,			/* mmap */
	NULL,			/* select */

	NULL,			/* readlink */
	NULL,			/* followlink */
	bfs_bmap,		/* bmap */
	bfs_lookup,		/* lookup */
	NULL,			/* rmdir */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* mknod */
	NULL,			/* truncate */
	NULL,			/* create */
	NULL,			/* rename */

	NULL,			/* read_block */
	NULL,			/* write_block */

	bfs_read_inode,		/* read_inode */
	NULL,			/* write_inode */
	NULL,			/* ialloc */
	NULL,			/* ifree */
	bfs_statfs,		/* statfs */
	bfs_read_superblock,	/* read_superblock */
	NULL,			/* remount_fs */
	NULL,			/* write_superblock */
	bfs_release_superblock,	/* release_superblock */
};

static void bfs_statfs(struct superblock *sb, struct statfs *buf)
{
	buf->f_type = BFS_SUPER_MAGIC1;
	buf->f_bsize = sb->s_blocksize;
	buf->f_blocks = sb->u.bfs.num_blocks;
	buf->f_bfree = 0;
	buf->f_bavail = 0;
	buf->f_files = 0;
	buf->f_ffree = 0;
	buf->f_namelen = BFS_NAME_LEN;
}

static void bfs_release_superblock(struct superblock *sb)
{
	/* nothing to do (read-only) */
}

static int bfs_read_superblock(__dev_t dev, struct superblock *sb)
{
	struct buffer *buf;
	struct bfs_superblock *bsb;
	struct bfs_block_run *root;
	__u32 root_block;

	superblock_lock(sb);
	if(!(buf = bread(dev, 0, BFS_BLOCK_SIZE))) {
		printk("WARNING: %s(): I/O error on device %d,%d.\n",
		       __FUNCTION__, MAJOR(dev), MINOR(dev));
		superblock_unlock(sb);
		return -EIO;
	}

	bsb = (struct bfs_superblock *)(buf->data + 512);
	if(bsb->magic1 != BFS_SUPER_MAGIC1 ||
	   bsb->magic2 != BFS_SUPER_MAGIC2 ||
	   bsb->magic3 != BFS_SUPER_MAGIC3) {
		printk("WARNING: %s(): invalid filesystem type or bad superblock on device %d,%d.\n",
		       __FUNCTION__, MAJOR(dev), MINOR(dev));
		superblock_unlock(sb);
		brelse(buf);
		return -EINVAL;
	}

	if(bsb->block_size != BFS_BLOCK_SIZE) {
		printk("WARNING: %s(): unsupported BFS block size %d.\n",
		       __FUNCTION__, bsb->block_size);
		superblock_unlock(sb);
		brelse(buf);
		return -EINVAL;
	}

	sb->dev = dev;
	sb->fsop = &bfs_fsop;
	sb->s_blocksize_bits = BFS_BLOCK_SHIFT;
	sb->s_blocksize = BFS_BLOCK_SIZE;
	sb->u.bfs.block_size = bsb->block_size;
	sb->u.bfs.blocks_per_ag = bsb->blocks_per_ag;
	sb->u.bfs.ag_shift = bsb->ag_shift;
	sb->u.bfs.num_ags = bsb->num_ags;
	sb->u.bfs.num_blocks = bsb->num_blocks;
	sb->u.bfs.flags = bsb->flags;

	root = &bsb->root_dir;
	root_block = (root->allocation_group << bsb->ag_shift) + root->start;
	sb->u.bfs.root_inode = root_block;

	if(!(sb->root = iget(sb, root_block))) {
		printk("WARNING: %s(): unable to get root inode.\n",
		       __FUNCTION__);
		superblock_unlock(sb);
		brelse(buf);
		return -EINVAL;
	}

	superblock_unlock(sb);
	brelse(buf);
	return 0;
}

int bfs_init(void)
{
	return register_filesystem("bfs", &bfs_fsop);
}
