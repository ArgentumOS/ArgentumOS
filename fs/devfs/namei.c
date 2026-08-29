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
	char full[64];
	int plen, nlen;

	if(name[0] == '.' && name[1] == '\0') {
		*i_res = dir;
		return 0;
	} else if(name[0] == '.' && name[1] == '.') {
		/* ".." of a devfs directory node: the parent is the devfs root,
		 * or for a nested dir node the parent node (strip the last
		 * path component). (do_namei() only handles ".." at the mount
		 * boundary.) */
		if(dir->inode != DEVFS_ROOT_INO) {
			if(dir->u.devfs.node && (n = devfs_find_parent(dir->u.devfs.node->name))) {
				*i_res = iget(dir->sb, DEVFS_INO(n->dev, S_ISBLK(n->mode)));
				if(!*i_res) {
					*i_res = dir->sb->root;
					(*i_res)->count++;
				}
			} else {
				*i_res = dir->sb->root;
				(*i_res)->count++;
			}
		}
		return 0;
	}

	if(dir->inode != DEVFS_ROOT_INO && dir->u.devfs.node) {
		/* directory node (e.g. /dev/disk): resolve "by-id" against the
		 * node's own name prefix ("disk/by-id"). */
		plen = strlen(dir->u.devfs.node->name);
		nlen = strlen(name);
		if(plen + 1 + nlen >= (int)sizeof(full)) {
			iput(dir);
			return -ENAMETOOLONG;
		}
		memcpy_b(full, dir->u.devfs.node->name, plen);
		full[plen++] = '/';
		memcpy_b(full + plen, name, nlen + 1);
		n = devfs_find_node(full);
	} else {
		n = devfs_find_node(name);
	}
	if(!n) {
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
