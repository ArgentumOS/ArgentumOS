/*
 * fnx/fs/devfs/nodes.c
 *
 * Copyright 2026, Kyle J Cardoza. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 *
 * The devfs node registry - the make_dev() analog. Drivers call
 * devfs_make_node() at probe time for each device node they own; the
 * registry is walked by devfs lookup/readdir to synthesize /dev. The
 * registry is independent of the mount, so nodes added after boot (e.g. a
 * hotplugged usb-storage disk) show up in /dev without any cache
 * invalidation: devfs inodes are synthesized on demand and never cached.
 */

#include <fnx/types.h>
#include <fnx/errno.h>
#include <fnx/fs.h>
#include <fnx/fs_devfs.h>
#include <fnx/devices.h>
#include <fnx/ata.h>
#include <fnx/floppy.h>
#include <fnx/ramdisk.h>
#include <fnx/stat.h>
#include <fnx/stdio.h>
#include <fnx/mm.h>
#include <fnx/string.h>

struct devfs_node *devfs_nodes = NULL;

/* dev-0 nodes (dirs/symlinks) get a unique virtual device number so their
 * inodes use the normal DEVFS_INO(dev) encoding (the shared DEVFS_INO(0)
 * made them all alias to one inode). 0x8000+ is above any real major. */
static __dev_t devfs_virtual_dev = 0x4000;

static unsigned int devfs_ino_extra = DEVFS_INO_BASE + 0x40000000;

int devfs_make_node(const char *name, __dev_t dev, __mode_t mode)
{
	struct devfs_node **n, *new;
	int name_len;

	/* idempotent: a node with this name already exists */
	if(devfs_find_node(name)) {
		return 0;
	}
	name_len = strlen(name);
	if(name_len > 31) {
		return -ENAMETOOLONG;
	}

	if(!(new = (struct devfs_node *)kmalloc(sizeof(struct devfs_node)))) {
		return -ENOMEM;
	}
	memset_b(new, 0, sizeof(struct devfs_node));
	strncpy(new->name, name, 31);
	new->name[31] = '\0';
	new->dev = dev ? dev : devfs_virtual_dev++;
	new->mode = mode;
	if(!dev) {
		new->ino = devfs_ino_extra++;
	}

	/* keep the registry sorted by device number for a stable /dev listing */
	n = &devfs_nodes;
	while(*n && (*n)->dev < dev) {
		n = &(*n)->next;
	}
	new->next = *n;
	*n = new;
	return 0;
}

int devfs_make_clone(const char *name, __dev_t dev, __mode_t mode, int (*clone_fn)(__dev_t))
{
	struct devfs_node *new;

	if(devfs_find_node(name)) {
		return 0;
	}
	if(strlen(name) > 31) {
		return -ENAMETOOLONG;
	}
	if(!(new = (struct devfs_node *)kmalloc(sizeof(struct devfs_node)))) {
		return -ENOMEM;
	}
	memset_b(new, 0, sizeof(struct devfs_node));
	strncpy(new->name, name, 31);
	new->name[31] = '\0';
	new->dev = dev ? dev : devfs_virtual_dev++;
	new->mode = mode;
	new->clone_fn = clone_fn;
	new->flags |= DEVFS_NODE_CLONE;

	/* keep the registry sorted by device number for a stable /dev listing */
	{
		struct devfs_node **n;
		n = &devfs_nodes;
		while(*n && (*n)->dev < dev) {
			n = &(*n)->next;
		}
		new->next = *n;
		*n = new;
	}
	return 0;
}

int devfs_make_symlink(const char *name, const char *target, __mode_t mode)
{
	struct devfs_node *n;

	if(devfs_find_node(name)) {
		return 0;
	}
	if(strlen(name) > 31) {
		return -ENAMETOOLONG;
	}
	if(!(n = (struct devfs_node *)kmalloc(sizeof(struct devfs_node)))) {
		return -ENOMEM;
	}
	memset_b(n, 0, sizeof(struct devfs_node));
	strncpy(n->name, name, 31);
	n->name[31] = '\0';
	n->mode = S_IFLNK | (mode & 0777);
	n->dev = devfs_virtual_dev++;
	if(!(n->target = (char *)kmalloc(strlen(target) + 1))) {
		kfree((addr_t)n);
		return -ENOMEM;
	}
	strcpy(n->target, target);
	n->flags |= DEVFS_NODE_SYMLINK;
	n->next = devfs_nodes;
	devfs_nodes = n;
	return 0;
}

void devfs_remove_node(__dev_t dev)
{
	struct devfs_node **n, *tmp;

	n = &devfs_nodes;
	while(*n) {
		if((*n)->dev == dev) {
			tmp = *n;
			*n = (*n)->next;
			if(tmp->target) {
				kfree((addr_t)tmp->target);
			}
			kfree((addr_t)tmp);
			return;
		}
		n = &(*n)->next;
	}
}

struct devfs_node *devfs_find_node_ino(unsigned int ino)
{
	struct devfs_node *n;

	for(n = devfs_nodes; n; n = n->next) {
		if(n->ino == ino) {
			return n;
		}
	}
	return NULL;
}

/* drop all devfs nodes owned by a device type/major (unregister path) */
void devfs_remove_device(int type, unsigned char major)
{
	struct devfs_node **n, *tmp;

	n = &devfs_nodes;
	while(*n) {
		if(MAJOR((*n)->dev) == major &&
		   (S_ISCHR((*n)->mode) == (type == CHR_DEV)) &&
		   (S_ISBLK((*n)->mode) == (type == BLK_DEV))) {
			tmp = *n;
			*n = (*n)->next;
			if(tmp->target) {
				kfree((addr_t)tmp->target);
			}
			kfree((addr_t)tmp);
			continue;
		}
		n = &(*n)->next;
	}
}

struct devfs_node *devfs_find_node(const char *name)
{
	struct devfs_node *n;

	for(n = devfs_nodes; n; n = n->next) {
		if(!strcmp(n->name, name)) {
			return n;
		}
	}
	return NULL;
}

struct devfs_node *devfs_find_node_dev(__dev_t dev)
{
	struct devfs_node *n;

	for(n = devfs_nodes; n; n = n->next) {
		if(n->dev == dev) {
			return n;
		}
	}
	return NULL;
}

/* the parent node of a nested node name ("disk/by-id" -> "disk"), or NULL
 * for a top-level name (whose parent is the devfs root). */
struct devfs_node *devfs_find_parent(const char *name)
{
	const char *slash;
	char parent[32];
	int plen;

	if(!(slash = strrchr(name, '/'))) {
		return NULL;
	}
	plen = (int)(slash - name);
	if(plen >= (int)sizeof(parent)) {
		return NULL;
	}
	memcpy_b(parent, name, plen);
	parent[plen] = '\0';
	return devfs_find_node(parent);
}

/* ---------------------------------------------------------------- */
/* M1: devfs <-> device-table integration (fs/devices.c hooks). A
 * registered device whose minors have no declared devfs nodes gets nodes
 * materialized here (the per-major fallback generators), so a driver that
 * has not been taught make_dev() still appears in /dev (e.g. the
 * usb-storage disk -> sda/sdb). unregister drops them again. */

/* per-major name generators (FreeBSD disk unit naming) */
static int devfs_sd_gen(int minor, char *name)
{
	char letter;

	/* "sd" + the first free letter: sda, sdb, ... (skip taken names) */
	for(letter = 'a'; letter <= 'z'; letter++) {
		sprintk(name, "sd%c", letter);
		if(!devfs_find_node(name)) {
			break;
		}
	}
	if(letter > 'z') {
		return -ENOSPC;
	}
	return 0;
}

static int devfs_nvme_gen(int minor, char *name)
{
	int n;

	for(n = 0; n < 64; n++) {
		sprintk(name, "nvme0n%d", n);
		if(!devfs_find_node(name)) {
			break;
		}
	}
	if(n >= 64) {
		return -ENOSPC;
	}
	return 0;
}

static int devfs_ide_gen(int major, int minor, char *name)
{
	char c;

	if(major == IDE0_MAJOR) {
		c = (minor & 0x40) ? 'b' : 'a';
	} else {
		c = (minor & 0x40) ? 'd' : 'c';
	}
	sprintk(name, "hd%c", c);
	return 0;
}

static int devfs_fd_gen(int minor, char *name)
{
	sprintk(name, "fd%d", minor);
	return 0;
}

static int devfs_ram_gen(int minor, char *name)
{
	sprintk(name, "ram%d", minor);
	return 0;
}

int devfs_device_registered(int type, struct device *d)
{
	char name[16];
	int minor;

	for(minor = 0; minor < 256; minor++) {
		if(!TEST_MINOR(d->minors, minor)) {
			continue;
		}
		/* already declared? */
		if(devfs_find_node_dev(MKDEV(d->major, minor))) {
			continue;
		}
		if(d->major == 8 && type == BLK_DEV) {
			if(!devfs_sd_gen(minor, name)) {
				devfs_make_node(name, MKDEV(d->major, minor), S_IFBLK | S_IRUSR | S_IWUSR);
			}
		} else if(d->major == 9 && type == BLK_DEV) {
			if(!devfs_nvme_gen(minor, name)) {
				devfs_make_node(name, MKDEV(d->major, minor), S_IFBLK | S_IRUSR | S_IWUSR);
			}
		} else if((d->major == IDE0_MAJOR || d->major == IDE1_MAJOR) && type == BLK_DEV) {
			if(!devfs_ide_gen(d->major, minor, name)) {
				devfs_make_node(name, MKDEV(d->major, minor), S_IFBLK | S_IRUSR | S_IWUSR);
			}
		} else if(d->major == FDC_MAJOR && type == BLK_DEV) {
			if(!devfs_fd_gen(minor, name)) {
				devfs_make_node(name, MKDEV(d->major, minor), S_IFBLK | S_IRUSR | S_IWUSR);
			}
		} else if(d->major == RAMDISK_MAJOR && type == BLK_DEV) {
			if(!devfs_ram_gen(minor, name)) {
				devfs_make_node(name, MKDEV(d->major, minor), S_IFBLK | S_IRUSR | S_IWUSR);
			}
		}
	}
	return 0;
}

void devfs_device_unregistered(int type, unsigned char major)
{
	devfs_remove_device(type, major);
}
