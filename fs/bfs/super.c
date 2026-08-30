/*
 * fnx/fs/bfs/super.c
 *
 * BFS (BeOS) filesystem: superblock, bitmap cache, registration, mount.
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
extern int bfs_file_write(struct inode *, struct fd *, const char *, __size_t);
extern __loff_t bfs_file_llseek(struct inode *, __loff_t);
extern int bfs_dir_read(struct inode *, struct fd *, char *, __size_t);
extern int bfs_readdir(struct inode *, struct fd *, struct dirent *, __size_t);
extern int bfs_readdir64(struct inode *, struct fd *, struct dirent64 *, __size_t);

/* inode operations */
extern int bfs_read_inode(struct inode *);
extern int bfs_write_inode(struct inode *);
extern int bfs_bmap(struct inode *, __off_t, int);
extern int bfs_lookup(const char *, struct inode *, struct inode **);
extern int bfs_truncate(struct inode *, __off_t);
extern int bfs_create(struct inode *, char *, int, __mode_t, struct inode **);
extern int bfs_mkdir(struct inode *, char *, __mode_t);
extern int bfs_link(struct inode *, struct inode *, char *);
extern int bfs_unlink(struct inode *, struct inode *, char *);
extern int bfs_rmdir(struct inode *, struct inode *);
extern int bfs_rename(struct inode *, struct inode *, struct inode *, struct inode *, char *, char *);
extern int bfs_symlink(struct inode *, char *, char *);
extern int bfs_readlink(struct inode *, char *, __size_t);
extern int bfs_followlink(struct inode *, struct inode *, struct inode **);
extern int bfs_ialloc(struct inode *, int);
extern void bfs_ifree(struct inode *);

/* superblock operations */
static void bfs_statfs(struct superblock *, struct statfs *);
static void bfs_release_superblock(struct superblock *);
static int bfs_write_superblock(struct superblock *);
static int bfs_read_superblock(__dev_t, struct superblock *);

struct fs_operations bfs_fsop = {
	FSOP_REQUIRES_DEV,
	0,

	bfs_open,		/* open */
	bfs_close,		/* close */
	file_read,		/* read */
	bfs_file_write,		/* write */
	NULL,			/* ioctl */
	bfs_file_llseek,	/* llseek */
	bfs_readdir,		/* readdir */
	bfs_readdir64,		/* readdir64 */
	NULL,			/* mmap */
	NULL,			/* select */

	bfs_readlink,		/* readlink */
	bfs_followlink,		/* followlink */
	bfs_bmap,		/* bmap */
	bfs_lookup,		/* lookup */
	bfs_rmdir,		/* rmdir */
	bfs_link,		/* link */
	bfs_unlink,		/* unlink */
	bfs_symlink,		/* symlink */
	bfs_mkdir,		/* mkdir */
	NULL,			/* mknod */
	bfs_truncate,		/* truncate */
	bfs_create,		/* create */
	bfs_rename,		/* rename */

	NULL,			/* read_block */
	NULL,			/* write_block */

	bfs_read_inode,		/* read_inode */
	bfs_write_inode,	/* write_inode */
	bfs_ialloc,		/* ialloc */
	bfs_ifree,		/* ifree */
	bfs_statfs,		/* statfs */
	bfs_read_superblock,	/* read_superblock */
	NULL,			/* remount_fs */
	bfs_write_superblock,	/* write_superblock */
	bfs_release_superblock,	/* release_superblock */
};

static void bfs_statfs(struct superblock *sb, struct statfs *buf)
{
	buf->f_type = BFS_SUPER_MAGIC1;
	buf->f_bsize = sb->s_blocksize;
	buf->f_blocks = sb->u.bfs.num_blocks;
	buf->f_bfree = sb->u.bfs.num_blocks - sb->u.bfs.used_blocks;
	buf->f_bavail = buf->f_bfree;
	buf->f_files = 0;
	buf->f_ffree = 0;
	buf->f_namelen = BFS_NAME_LEN;
}

static void bfs_release_superblock(struct superblock *sb)
{
	if(sb->flags & MS_RDONLY) {
		return;
	}

	superblock_lock(sb);
	sb->u.bfs.flags = BFS_SUPER_CLEAN;
	sb->state = SUPERBLOCK_DIRTY;
	superblock_unlock(sb);
}

/*
 * Write the superblock + the cached bitmap back to disk.
 */
static int bfs_write_superblock(struct superblock *sb)
{
	struct buffer *buf;
	struct bfs_superblock *bsb;
	__u32 i;

	superblock_lock(sb);

	/* flush the bitmap blocks */
	for(i = 0; i < sb->u.bfs.bitmap_blocks; i++) {
		struct buffer *bb;
		if(!(bb = bread(sb->dev, 1 + i, BFS_BLOCK_SIZE))) {
			superblock_unlock(sb);
			return -EIO;
		}
		memcpy_b(bb->data, sb->u.bfs.bitmap + (i * BFS_BLOCK_SIZE),
			BFS_BLOCK_SIZE);
		bwrite(bb);
	}

	/* rebuild the superblock */
	if(!(buf = bread(sb->dev, 0, BFS_BLOCK_SIZE))) {
		superblock_unlock(sb);
		return -EIO;
	}
	bsb = (struct bfs_superblock *)(buf->data + 512);
	memset_b(bsb, 0, 512);
	bsb->name[0] = 'B';
	bsb->name[1] = 'F';
	bsb->name[2] = 'S';
	bsb->name[3] = '1';
	bsb->magic1 = BFS_SUPER_MAGIC1;
	bsb->fs_byte_order = BFS_SUPER_BYTEORDER;
	bsb->block_size = sb->u.bfs.block_size;
	bsb->block_shift = BFS_BLOCK_SHIFT;
	bsb->num_blocks = sb->u.bfs.num_blocks;
	bsb->used_blocks = sb->u.bfs.used_blocks;
	bsb->inode_size = BFS_INODE_SIZE;
	bsb->magic2 = BFS_SUPER_MAGIC2;
	bsb->blocks_per_ag = sb->u.bfs.blocks_per_ag;
	bsb->ag_shift = sb->u.bfs.ag_shift;
	bsb->num_ags = sb->u.bfs.num_ags;
	bsb->flags = sb->u.bfs.flags;
	bsb->log_blocks.allocation_group = 0;
	bsb->log_blocks.start = 2;
	bsb->log_blocks.len = 4;
	bsb->log_start = 0;
	bsb->log_end = 0;
	bsb->magic3 = BFS_SUPER_MAGIC3;
	bsb->root_dir.allocation_group = 0;
	bsb->root_dir.start = sb->u.bfs.root_inode;
	bsb->root_dir.len = 1;
	bwrite(buf);

	sb->state &= ~SUPERBLOCK_DIRTY;
	superblock_unlock(sb);
	return 0;
}

static int bfs_read_superblock(__dev_t dev, struct superblock *sb)
{
	struct buffer *buf;
	struct bfs_superblock *bsb;
	struct bfs_block_run *root;
	__u32 root_block;
	__u32 i;

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
	sb->u.bfs.used_blocks = bsb->used_blocks;
	sb->u.bfs.flags = bsb->flags;
	sb->u.bfs.next_free = 0;

	root = &bsb->root_dir;
	root_block = (root->allocation_group << bsb->ag_shift) + root->start;
	sb->u.bfs.root_inode = root_block;

	/* load the bitmap into memory */
	sb->u.bfs.bitmap_blocks = sb->u.bfs.num_ags * sb->u.bfs.blocks_per_ag;
	if(!(sb->u.bfs.bitmap = (unsigned char *)kmalloc(
			sb->u.bfs.bitmap_blocks * BFS_BLOCK_SIZE))) {
		printk("WARNING: %s(): unable to allocate the bitmap.\n",
		       __FUNCTION__);
		superblock_unlock(sb);
		brelse(buf);
		return -ENOMEM;
	}
	for(i = 0; i < sb->u.bfs.bitmap_blocks; i++) {
		struct buffer *bb;
		if(!(bb = bread(dev, 1 + i, BFS_BLOCK_SIZE))) {
			kfree((addr_t)sb->u.bfs.bitmap);
			sb->u.bfs.bitmap = NULL;
			superblock_unlock(sb);
			brelse(buf);
			return -EIO;
		}
		memcpy_b(sb->u.bfs.bitmap + (i * BFS_BLOCK_SIZE), bb->data,
			BFS_BLOCK_SIZE);
		brelse(bb);
	}

	if(!(sb->root = iget(sb, root_block))) {
		printk("WARNING: %s(): unable to get root inode.\n",
		       __FUNCTION__);
		superblock_unlock(sb);
		brelse(buf);
		return -EINVAL;
	}

	if(!(sb->flags & MS_RDONLY)) {
		sb->u.bfs.flags = BFS_SUPER_DIRTY;
		sb->state |= SUPERBLOCK_DIRTY;
	}
	brelse(buf);
	superblock_unlock(sb);
	return 0;
}

int bfs_init(void)
{
	return register_filesystem("bfs", &bfs_fsop);
}
