/*
 * fnx/fs/bfs/symlink.c
 *
 * BFS symlink operations. Targets are stored in the inode's symlink
 * area (fast symlinks; the BFS inode holds 144 bytes of symlink data).
 *
 * Copyright 2024, the FNX project.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/kernel.h>
#include <fnx/types.h>
#include <fnx/errno.h>
#include <fnx/fs.h>
#include <fnx/bfs.h>
#include <fnx/sched.h>
#include <fnx/stat.h>
#include <fnx/string.h>

int bfs_readlink(struct inode *i, char *buffer, __size_t count)
{
	int n;

	if(!S_ISLNK(i->i_mode)) {
		return 0;
	}

	inode_lock(i);
	count = MIN(count, i->i_size);
	count = MIN(count, 143);
	for(n = 0; n < count; n++) {
		buffer[n] = i->u.bfs.raw.u.symlink[n];
	}
	buffer[count] = 0;
	inode_unlock(i);
	return count;
}

int bfs_followlink(struct inode *dir, struct inode *i, struct inode **i_res)
{
	char name[144];
	int n, errno;

	if(!i) {
		return -ENOENT;
	}
	if(!S_ISLNK(i->i_mode)) {
		return 0;
	}
	if(current->loopcnt > MAX_SYMLINKS) {
		iput(i);
		return -ELOOP;
	}

	for(n = 0; n < 143; n++) {
		if((name[n] = i->u.bfs.raw.u.symlink[n])) {
			continue;
		}
		break;
	}
	name[n] = 0;

	current->loopcnt++;
	iput(i);
	errno = parse_namei(name, dir, i_res, NULL, FOLLOW_LINKS);
	current->loopcnt--;
	return errno;
}
