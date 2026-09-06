/*
 * fnx/kernel/syscalls/newlstat.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/fs.h>
#include <fnx/statbuf.h>
#include <fnx/string.h>

/* shared stat filler defined below (see statbuf.h): clang rejects the
 * c89 implicit declaration gcc tolerated (conflicting types) */
void fill_new_stat(struct inode *, struct new_stat *);

#ifdef __DEBUG__
#include <fnx/stdio.h>
#include <fnx/process.h>

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
