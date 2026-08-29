/*
 * fnx/fs/devfs/namei.c
 *
 * Copyright 2026, Kyle J Cardoza. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/types.h>
#include <fnx/fs.h>
#include <fnx/filesystems.h>
#include <fnx/fs_devfs.h>
#include <fnx/stat.h>
#include <fnx/errno.h>
#include <fnx/stdio.h>
#include <fnx/string.h>

int devfs_lookup(const char *name, struct inode *dir, struct inode **i_res)
{
	struct devfs_node *n;

	if(name[0] == '.' && name[1] == '\0') {
		*i_res = dir;
		return 0;
	} else if(name[0] == '.' && name[1] == '.') {
		/* ".." of a devfs directory node: the parent is the devfs root.
		 * (do_namei() only handles ".." at the mount boundary.) */
		if(dir->inode != DEVFS_ROOT_INO) {
			*i_res = dir->sb->root;
			(*i_res)->count++;
		}
		return 0;
	}

	if(!(n = devfs_find_node(name))) {
		iput(dir);
		return -ENOENT;
	}
	if(!(*i_res = iget(dir->sb, DEVFS_INO(n->dev, S_ISBLK(n->mode))))) {
		iput(dir);
		return -EACCES;
	}
	iput(dir);
	return 0;
}
