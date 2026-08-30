/*
 * fnx/kernel/syscalls/chown.c
 *
 * Copyright 2018-2021, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/types.h>
#include <fnx/kernel.h>
#include <fnx/fs.h>
#include <fnx/stat.h>
#include <fnx/errno.h>
#include <fnx/fs_inotify.h>
#include <fnx/string.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#include <fnx/process.h>
#endif /*__DEBUG__ */

int sys_chown(const char *filename, __uid_t owner, __gid_t group)
{
	struct inode *i;
	char *tmp_name;
	int errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_chown('%s', %d, %d)\n", current->pid, filename, owner, group);
#endif /*__DEBUG__ */

	if((errno = malloc_name(filename, &tmp_name)) < 0) {
		return errno;
	}
	if((errno = namei(tmp_name, &i, NULL, FOLLOW_LINKS))) {
		free_name(tmp_name);
		return errno;
	}

	if(IS_RDONLY_FS(i)) {
		iput(i);
		free_name(tmp_name);
		return -EROFS;
	}
	if((errno = check_chown_permission(i, owner, group))) {
		iput(i);
		free_name(tmp_name);
		return errno;
	}

	if(owner == (__uid_t)-1) {
		owner = i->i_uid;
	}
	if(group == (__gid_t)-1) {
		group = i->i_gid;
	}

	i->i_uid = owner;
	i->i_gid = group;
	i->i_ctime = CURRENT_TIME;	/* chown always updates ctime */
	i->state |= INODE_DIRTY;
	inotify_queue(i, IN_ATTRIB, 0, NULL);
	iput(i);
	free_name(tmp_name);
	return 0;
}
