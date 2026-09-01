/*
 * fnx/kernel/syscalls/dup2.c
 *
 * Copyright 2018-2022, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/syscalls.h>
#include <fnx/process.h>
#include <fnx/fcntl.h>
#include <fnx/errno.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#endif /*__DEBUG__ */

int sys_dup2(unsigned int old_ufd, unsigned int new_ufd)
{
	int errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_dup2(%d, %d)", current->pid, old_ufd, new_ufd);
#endif /*__DEBUG__ */

	CHECK_UFD(old_ufd);
	if(new_ufd >= OPEN_MAX) {
		return -EINVAL;
	}
	if(old_ufd == new_ufd) {
		return new_ufd;
	}
	if(current->fd[new_ufd]) {
		sys_close(new_ufd);
	}
	if((errno = get_new_user_fd(new_ufd)) < 0) {
		return errno;
	}
	new_ufd = errno;
	current->fd[new_ufd] = current->fd[old_ufd];
	fd_table[current->fd[new_ufd]].count++;
#ifdef __DEBUG__
	printk(" --> returning %d\n", new_ufd);
#endif /*__DEBUG__ */
	return new_ufd;
}

/* dup3(292): dup2 with flags (only O_CLOEXEC is supported) */
int sys_dup3(unsigned int old_ufd, unsigned int new_ufd, int flags)
{
	int errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_dup3(%d, %d, %d)", current->pid, old_ufd, new_ufd, flags);
#endif /*__DEBUG__ */

	if(flags & ~O_CLOEXEC) {
		return -EINVAL;
	}
	if(old_ufd == new_ufd) {
		return -EINVAL;
	}
	if((errno = sys_dup2(old_ufd, new_ufd)) < 0) {
		return errno;
	}
	if(flags & O_CLOEXEC) {
		current->fd_flags[new_ufd] |= FD_CLOEXEC;
	} else {
		current->fd_flags[new_ufd] &= ~FD_CLOEXEC;
	}
	return new_ufd;
}
