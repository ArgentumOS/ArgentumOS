/*
 * fnx/kernel/syscalls/fstatfs.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/fs.h>
#include <fnx/statfs.h>
#include <fnx/errno.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#include <fnx/process.h>
#endif /*__DEBUG__ */

int sys_fstatfs(unsigned int ufd, struct statfs *statfsbuf)
{
	struct inode *i;
	int errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_fstatfs(%d, 0x%08x)\n", current->pid, ufd, (unsigned int)statfsbuf);
#endif /*__DEBUG__ */

	CHECK_UFD(ufd);
	if((errno = check_user_area(VERIFY_WRITE, statfsbuf, sizeof(struct statfs)))) {
		return errno;
	}
	i = fd_table[current->fd[ufd]].inode;
	if(i->sb && i->sb->fsop && i->sb->fsop->statfs) {
		i->sb->fsop->statfs(i->sb, statfsbuf);
		return 0;
	}
	return -ENOSYS;
}

/* x86-64 ABI: same struct as sys_statfs64 (include/fnx/statfs.h) */
int sys_fstatfs64(unsigned int ufd, struct fnx_statfs64 *statfsbuf)
{
	struct fnx_statfs64 s64;
	struct statfs s32;
	struct inode *i;
	int errno;

	CHECK_UFD(ufd);
	if((errno = check_user_area(VERIFY_WRITE, statfsbuf,
			sizeof(struct fnx_statfs64)))) {
		return errno;
	}
	i = fd_table[current->fd[ufd]].inode;
	if(!i->sb || !i->sb->fsop || !i->sb->fsop->statfs) {
		return -ENOSYS;
	}
	memset_b(&s32, 0, sizeof(s32));
	i->sb->fsop->statfs(i->sb, &s32);
	memset_b(&s64, 0, sizeof(s64));
	s64.f_type = s32.f_type;
	s64.f_bsize = s32.f_bsize;
	s64.f_blocks = s32.f_blocks;
	s64.f_bfree = s32.f_bfree;
	s64.f_bavail = s32.f_bavail;
	s64.f_files = s32.f_files;
	s64.f_ffree = s32.f_ffree;
	s64.f_fsid[0] = s32.f_fsid.val[0];
	s64.f_fsid[1] = s32.f_fsid.val[1];
	s64.f_namelen = s32.f_namelen;
	return copy_to_user(statfsbuf, &s64, sizeof(s64));
}
