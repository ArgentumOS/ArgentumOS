/*
 * fnx/fs/inotifyfs/super.c
 *
 * FNX: inotifyfs - a tiny internal filesystem giving inotify instances a
 * real inode/fd (read/poll/ioctl/close via the fsop), modeled on sockfs.
 */

#include <fnx/types.h>
#include <fnx/errno.h>
#include <fnx/fs.h>
#include <fnx/filesystems.h>
#include <fnx/fs_inotify.h>
#include <fnx/stat.h>
#include <fnx/mm.h>
#include <fnx/sched.h>
#include <fnx/stdio.h>
#include <fnx/string.h>

static unsigned int i_counter;

extern int inotifyfs_read(struct inode *, struct fd *, char *, __size_t);
extern int inotifyfs_select(struct inode *, struct fd *, int);
extern int inotifyfs_ioctl(struct inode *, struct fd *, int, addr_t);
extern int inotifyfs_close(struct inode *, struct fd *);

struct fs_operations inotifyfs_fsop = {
	FSOP_KERN_MOUNT,
	0,			/* fsdev */

	NULL,			/* open */
	inotifyfs_close,
	inotifyfs_read,
	NULL,			/* write */
	inotifyfs_ioctl,
	NULL,			/* llseek */
	NULL,			/* readdir */
	NULL,			/* readdir64 */
	NULL,			/* mmap */
	inotifyfs_select,

	NULL,			/* readlink */
	NULL,			/* followlink */
	NULL,			/* bmap */
	NULL,			/* lookup */
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

	NULL,			/* read_inode */
	NULL,			/* write_inode */
	inotifyfs_ialloc,
	inotifyfs_ifree,
	NULL,			/* statfs */
	inotifyfs_read_superblock,
	NULL,			/* remount_fs */
	NULL,			/* write_superblock */
	NULL			/* release_superblock */
};

int inotifyfs_ialloc(struct inode *i, int mode)
{
	struct superblock *sb = i->sb;

	superblock_lock(sb);
	i_counter++;
	superblock_unlock(sb);

	i->i_mode = mode;
	i->dev = i->rdev = sb->dev;
	i->fsop = &inotifyfs_fsop;
	i->inode = i_counter;
	i->count = 1;
	memset_b(&i->u.inotify, 0, sizeof(struct inotifyfs_inode));
	return 0;
}

void inotifyfs_ifree(struct inode *i)
{
	/* the instance (watches + event queue) is torn down in
	 * inotifyfs_close() when the last fd is closed; ifree is a
	 * no-op here */
}

int inotifyfs_read_superblock(__dev_t dev, struct superblock *sb)
{
	superblock_lock(sb);
	sb->dev = dev;
	sb->fsop = &inotifyfs_fsop;
	sb->s_blocksize = BLKSIZE_1K;
	i_counter = 0;
	superblock_unlock(sb);
	return 0;
}

int inotifyfs_init(void)
{
	return register_filesystem("inotifyfs", &inotifyfs_fsop);
}
