/*
 * fnx/kernel/syscalls/statfs.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/fs.h>
#include <fnx/statfs.h>
#include <fnx/errno.h>
#include <fnx/string.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#include <fnx/process.h>
#endif /*__DEBUG__ */

int sys_statfs(const char *filename, struct statfs *statfsbuf)
{
	struct inode *i;
	char *tmp_name;
	int errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_statfs('%s', 0x%08x)\n", current->pid, filename, (unsigned int)statfsbuf);
#endif /*__DEBUG__ */

	if((errno = check_user_area(VERIFY_WRITE, statfsbuf, sizeof(struct statfs)))) {
		return errno;
	}
	if((errno = malloc_name(filename, &tmp_name)) < 0) {
		return errno;
	}
	if((errno = namei(tmp_name, &i, NULL, FOLLOW_LINKS))) {
		free_name(tmp_name);
		return errno;
	}
	free_name(tmp_name);

	if(i->sb && i->sb->fsop && i->sb->fsop->statfs) {
		i->sb->fsop->statfs(i->sb, statfsbuf);
		iput(i);
		return 0;
	}
	iput(i);
	return -ENOSYS;
}

/* x86-64 statfs ABI: the user struct has 64-bit fields (see
 * include/fnx/statfs.h). The fsop->statfs() callbacks fill the 32-bit-era
 * kernel struct, so expand into a kernel-side 64-bit struct and copy it
 * out (FNX has no 32-bit compatibility; this IS sys_statfs on x86_64). */
static void statfs64_from32(struct fnx_statfs64 *s64, struct statfs *s32)
{
	memset_b(s64, 0, sizeof(struct fnx_statfs64));
	s64->f_type = s32->f_type;
	s64->f_bsize = s32->f_bsize;
	s64->f_blocks = s32->f_blocks;
	s64->f_bfree = s32->f_bfree;
	s64->f_bavail = s32->f_bavail;
	s64->f_files = s32->f_files;
	s64->f_ffree = s32->f_ffree;
	s64->f_fsid[0] = s32->f_fsid.val[0];
	s64->f_fsid[1] = s32->f_fsid.val[1];
	s64->f_namelen = s32->f_namelen;
}

int sys_statfs64(const char *filename, struct fnx_statfs64 *statfsbuf)
{
	struct fnx_statfs64 s64;
	struct statfs s32;
	struct inode *i;
	char *tmp_name;
	int errno;

	if((errno = check_user_area(VERIFY_WRITE, statfsbuf,
			sizeof(struct fnx_statfs64)))) {
		return errno;
	}
	if((errno = malloc_name(filename, &tmp_name)) < 0) {
		return errno;
	}
	if((errno = namei(tmp_name, &i, NULL, FOLLOW_LINKS))) {
		free_name(tmp_name);
		return errno;
	}
	free_name(tmp_name);

	if(!i->sb || !i->sb->fsop || !i->sb->fsop->statfs) {
		iput(i);
		return -ENOSYS;
	}
	memset_b(&s32, 0, sizeof(s32));
	i->sb->fsop->statfs(i->sb, &s32);
	iput(i);
	statfs64_from32(&s64, &s32);
	return copy_to_user(statfsbuf, &s64, sizeof(s64));
}
