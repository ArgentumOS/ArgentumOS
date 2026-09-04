/*
 * fnx/fs/super.c
 *
 * Copyright 2018-2022, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/asm.h>
#include <fnx/kernel.h>
#include <fnx/kparms.h>
#include <fnx/types.h>
#include <fnx/errno.h>
#include <fnx/fs.h>
#include <fnx/stat.h>
#include <fnx/filesystems.h>
#include <fnx/sleep.h>
#include <fnx/sched.h>
#include <fnx/stdio.h>
#include <fnx/string.h>
#include <fnx/mm.h>

struct mount *mount_table = NULL;
static struct resource sync_resource = { 0, 0 };

void superblock_lock(struct superblock *sb)
{
	unsigned int flags;

	for(;;) {
		SAVE_FLAGS(flags); CLI();
		if(sb->state & SUPERBLOCK_LOCKED) {
			RESTORE_FLAGS(flags);
			sleep(sb, PROC_UNINTERRUPTIBLE);
		} else {
			break;
		}
	}
	sb->state |= SUPERBLOCK_LOCKED;
	RESTORE_FLAGS(flags);
}
 
void superblock_unlock(struct superblock *sb)
{
	unsigned int flags;

	SAVE_FLAGS(flags); CLI();
	sb->state &= ~SUPERBLOCK_LOCKED;
	wakeup(sb);
	RESTORE_FLAGS(flags);
}

struct mount *add_mount_point(__dev_t dev, const char *devname, const char *dirname)
{
	unsigned int flags;
	struct mount *mp;

	if(kstat.mount_points + 1 > NR_MOUNT_POINTS) {
		printk("WARNING: tried to exceed NR_MOUNT_POINTS (%d).\n", NR_MOUNT_POINTS);
		return NULL;
	}

	/* check if this device is already mounted */
	if(get_superblock(dev)) {
		return NULL;
	}

	if(!(mp = (struct mount *)kmalloc(sizeof(struct mount)))) {
		return NULL;
	}
	memset_b(mp, 0, sizeof(struct mount));

	if(!(mp->devname = (char *)kmalloc(strlen(devname) + 1))) {
		kfree((addr_t)mp);
		return NULL;
	}
	if(!(mp->dirname = (char *)kmalloc(strlen(dirname) + 1))) {
		kfree((addr_t)mp->devname);
		kfree((addr_t)mp);
		return NULL;
	}

	SAVE_FLAGS(flags); CLI();
	if(!mount_table) {
		mount_table = mp;
	} else {
		mp->prev = mount_table->prev;
		mount_table->prev->next = mp;
	}
	mount_table->prev = mp;
	RESTORE_FLAGS(flags);

	mp->dev = dev;
	strcpy(mp->devname, devname);
	strcpy(mp->dirname, dirname);
	kstat.mount_points++;
	return mp;
}

void del_mount_point(struct mount *mp)
{
	unsigned int flags;
	struct mount *tmp;

	tmp = mp;

	if(!mp->next && !mp->prev) {
		printk("WARNING: %s(): trying to umount an unexistent mount point (%x, '%s', '%s').\n", __FUNCTION__, mp->dev, mp->devname, mp->dirname);
		return;
	}

	SAVE_FLAGS(flags); CLI();
	if(mp->next) {
		mp->next->prev = mp->prev;
	}
	if(mp->prev) {
		if(mp != mount_table) {
			mp->prev->next = mp->next;
		}
	}
	if(!mp->next) {
		mount_table->prev = mp->prev;
	}
	if(mp == mount_table) {
		mount_table = mp->next;
	}
	RESTORE_FLAGS(flags);

	kfree((addr_t)tmp->devname);
	kfree((addr_t)tmp->dirname);
	kfree((addr_t)tmp);
	kstat.mount_points--;
}

struct mount *get_mount_point(struct inode *i)
{
	struct mount *mp;

	mp = mount_table;

	while(mp) {
		if(S_ISDIR(i->i_mode)) {
			if(mp->sb.root == i) {
				return mp;
			}
		}
		if(S_ISBLK(i->i_mode)) {
			if(mp->dev == i->rdev) {
				return mp;
			}
		}
		mp = mp->next;
	}

	return NULL;
}

struct superblock *get_superblock(__dev_t dev)
{
	struct mount *mp;

	mp = mount_table;

	while(mp) {
		if(mp->dev == dev) {
			return &mp->sb;
		}
		mp = mp->next;
	}
	return NULL;
}

void sync_superblocks(__dev_t dev)
{
	struct superblock *sb;
	struct mount *mp;

	mp = mount_table;

	lock_resource(&sync_resource);
	while(mp) {
		if(!dev || mp->dev == dev) {
			sb = &mp->sb;
			if((sb->state & SUPERBLOCK_DIRTY) && !(sb->flags & MS_RDONLY)) {
				if(sb->fsop->write_superblock(sb)) {
					printk("WARNING: %s(): I/O error on device %d,%d while syncing superblock.\n", __FUNCTION__, MAJOR(sb->dev), MINOR(sb->dev));
				}
			}
		}
		mp = mp->next;
	}
	unlock_resource(&sync_resource);
}

/* pseudo-filesystems are only mountable by the kernel */
int kern_mount(__dev_t dev, struct filesystems *fs)
{
	struct mount *mp;

	if(!(mp = add_mount_point(dev, "none", "none"))) {
		return -EBUSY;
	}

	if(fs->fsop->read_superblock(dev, &mp->sb)) {
		del_mount_point(mp);
		return -EINVAL;
	}

	mp->sb.dir = NULL;
	mp->fs = fs;
	fs->mp = mp;
	return 0;
}

int mount_root(void)
{
	struct filesystems *fs = NULL;
	struct mount *mp;
	int n;

	/*
	 * FIXME: before trying to mount the filesystem, we should first
	 * check if '_rootdev' is a device successfully registered.
	 */

	if(!kparms.rootdev) {
		PANIC("root device not defined.\n");
	}

	if(!(mp = add_mount_point(kparms.rootdev, "/dev/root", "/"))) {
		PANIC("unable to get a free mount point.\n");
	}
	if(kparms.ro) {
		mp->sb.flags = MS_RDONLY;
	}

	if(kparms.rootfstype[0]) {
		/* explicit rootfstype= on the command line */
		if(!(fs = get_filesystem(kparms.rootfstype))) {
			printk("WARNING: %s(): '%s' is not a registered filesystem. Defaulting to 'ext2'.\n", __FUNCTION__, kparms.rootfstype);
			fs = get_filesystem("ext2");
		}
		if(!fs) {
			PANIC("ext2 filesystem is not registered!\n");
		}
		if(fs->fsop->read_superblock(kparms.rootdev, &mp->sb)) {
			PANIC("unable to mount root filesystem on %s.\n", kparms.rootdevname);
		}
	} else {
		/* no rootfstype= on the command line: probe the disk
		 * filesystems (in a fixed order; the pseudo filesystems like
		 * procfs/devfs would claim any device) and mount the first
		 * one whose read_superblock recognizes the device */
		static const char *probe[] = { "minix", "ext2", "iso9660", "bfs" };

		for(n = 0; n < (int)(sizeof(probe) / sizeof(probe[0])); n++) {
			struct filesystems *cand = get_filesystem(probe[n]);

			if(!cand) {
				continue;
			}
			/* a failed probe may leave stale generic sb fields;
			 * the successful one fills in everything it uses */
			memset_b(&mp->sb, 0, sizeof(struct superblock));
			if(kparms.ro) {
				mp->sb.flags = MS_RDONLY;
			}
			if(!cand->fsop->read_superblock(kparms.rootdev, &mp->sb)) {
				fs = cand;
				break;
			}
		}
		if(!fs) {
			PANIC("unable to mount root filesystem on %s.\n", kparms.rootdevname);
		}
	}

	mp->sb.root->mount_point = mp->sb.root;
	mp->sb.root->count++;
	mp->sb.dir = mp->sb.root;
	mp->sb.dir->count++;
	mp->fs = fs;

	current->root = mp->sb.root;
	current->root->count++;
	current->pwd = mp->sb.root;
	current->pwd->count++;
	iput(mp->sb.root);

	printk("mounted root device (%s filesystem)", fs->name);
	if(mp->sb.flags & MS_RDONLY) {
		printk(" in readonly mode");
	}
	printk(".\n");
	return 0;
}

/*
 * Make sure /tmp is a real, empty-able directory on the freshly mounted
 * root filesystem. Called right after mount_root(), i.e. after the root
 * fs's journal has been replayed.
 *
 * Why: the BFS journal replays a killed session's uncommitted writes at
 * mount time, and a session killed mid-write can leave /tmp replayed in
 * a state no userland cleanup can fix (observed: /tmp itself restored
 * as a regular file - every access inside it then fails ENOTDIR, so the
 * init scripts' rm -rf of the stale X locks cannot work and Xfb refuses
 * to start). The kernel repairs /tmp here (drop + recreate when it is
 * not a directory); the init scripts then clear whatever stale content
 * a healthy-but-dirty /tmp was replayed with.
 */
void fs_repair_tmpdir(void)
{
	struct inode *tmp, *dir;
	int errno;

	tmp = NULL;
	dir = NULL;
	errno = parse_namei("/System/Temporary Files", NULL, &tmp, &dir, !FOLLOW_LINKS);

	if(!errno && tmp && S_ISDIR(tmp->i_mode)) {
		/* healthy tmp dir: the init scripts clear the stale contents */
		iput(tmp);
		iput(dir);
		return;
	}
	printk("WARNING: %s(): /System/Temporary Files not a directory at mount (errno %d, mode %o); recreating.\n",
	       __FUNCTION__, -errno, tmp ? tmp->i_mode : 0);

	/* the tmp dir exists but is not a directory (journal replay
	 * residue): remove it so a real directory can be created in its
	 * place */
	if(!errno && tmp) {
		if(dir && !IS_RDONLY_FS(dir) && dir->fsop && dir->fsop->unlink) {
			dir->fsop->unlink(dir, tmp, "Temporary Files");
		}
		iput(tmp);
	}

	/* recreate it. dir is the parent (/System, ref'd) both when the
	 * entry was a non-directory and when the final component was
	 * missing (parse_namei returns the parent on ENOENT) */
	if(dir && !IS_RDONLY_FS(dir) && dir->fsop && dir->fsop->mkdir) {
		if(dir->fsop->mkdir(dir, "Temporary Files", 01777)) {
			printk("WARNING: %s(): unable to create /System/Temporary Files.\n",
			       __FUNCTION__);
		}
	}
	if(dir) {
		iput(dir);
	}
}
