/*
 * fiwix/kernel/syscalls/newlstat.c
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

int sys_newlstat(const char *filename, struct new_stat *statbuf)
{
	struct inode *i;
	char *tmp_name;
	int errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_newlstat('%s', 0x%08x) -> returning structure\n", current->pid, filename, (unsigned int )statbuf);
#endif /*__DEBUG__ */

	if((errno = check_user_area(VERIFY_WRITE, statbuf, sizeof(struct new_stat)))) {
		return errno;
	}
	if((errno = malloc_name(filename, &tmp_name)) < 0) {
		return errno;
	}
	if((errno = namei(tmp_name, &i, NULL, !FOLLOW_LINKS))) {
		free_name(tmp_name);
		return errno;
	}
		fill_new_stat(i, statbuf);
	iput(i);
	free_name(tmp_name);
	return 0;
}
