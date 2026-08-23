/*
 * fiwix/kernel/syscalls/newstat.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fiwix/fs.h>
#include <fiwix/statbuf.h>
#include <fiwix/string.h>

#ifdef __DEBUG__
#include <fiwix/stdio.h>
#include <fiwix/process.h>
#endif /*__DEBUG__ */

int sys_newstat(const char *filename, struct new_stat *statbuf)
{
	struct inode *i;
	char *tmp_name;
	int errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_newstat('%s', 0x%08x) -> returning structure\n", current->pid, filename, (unsigned int )statbuf);
#endif /*__DEBUG__ */

	if((errno = check_user_area(VERIFY_WRITE, statbuf, sizeof(struct new_stat)))) {
		return errno;
	}
	if((errno = malloc_name(filename, &tmp_name)) < 0) {
		return errno;
	}
	if((errno = namei(tmp_name, &i, NULL, FOLLOW_LINKS))) {
		free_name(tmp_name);
		return errno;
	}
	fill_new_stat(i, statbuf);
	iput(i);
	free_name(tmp_name);
	return 0;
}

/* shared filler for the x86-64 ABI 'struct stat' (see statbuf.h) */
void fill_new_stat(struct inode *i, struct new_stat *statbuf)
{
	statbuf->st_dev = i->dev;
	statbuf->st_ino = i->inode;
	statbuf->st_mode = i->i_mode;
	statbuf->st_nlink = i->i_nlink;
	statbuf->st_uid = i->i_uid;
	statbuf->st_gid = i->i_gid;
	statbuf->__pad0 = 0;
	statbuf->st_rdev = i->rdev;
	statbuf->st_size = i->i_size;
	statbuf->st_blksize = i->sb->s_blocksize;
	statbuf->st_blocks = i->i_blocks;
	if(!i->i_blocks) {
		statbuf->st_blocks = (i->i_size / i->sb->s_blocksize) * 2;
		statbuf->st_blocks++;
	}
	statbuf->st_atime = i->i_atime;
	statbuf->st_atime_nsec = 0;
	statbuf->st_mtime = i->i_mtime;
	statbuf->st_mtime_nsec = 0;
	statbuf->st_ctime = i->i_ctime;
	statbuf->st_ctime_nsec = 0;
	statbuf->__unused[0] = statbuf->__unused[1] = statbuf->__unused[2] = 0;
}
