/*
 * fnx/kernel/syscalls/writev.c
 *
 * Copyright 2023, Jordi Sanfeliu. All rights reserved.
 * Copyright 2023, Richard R. Masters.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/fs.h>
#include <fnx/fcntl.h>
#include <fnx/errno.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#include <fnx/process.h>
#endif /*__DEBUG__ */

int sys_writev(int ufd, const struct iovec *iov, int iovcnt)
{
	struct inode *i;
	int errno;
	int bytes_written = 0;
	int vi;	/* vector index */

#ifdef __DEBUG__
	printk("(pid %d) sys_writev(%d, 0x%08x, %d) -> ", current->pid, ufd, iov, iovcnt);
#endif /*__DEBUG__ */

	CHECK_UFD(ufd);
	if(iovcnt < 0 || iovcnt > UIO_MAXIOV) {
		return -EINVAL;
	}
	for (vi = 0; vi < iovcnt; vi++) {
		struct iovec io;
		/* FNX (native port): full 64-bit struct iovec */
		io = ((struct iovec *)iov)[vi];
		if(!io.iov_len) {
			continue;
		}
		if((errno = check_user_area(VERIFY_READ, io.iov_base, io.iov_len))) {
			return errno;
		}
		if(fd_table[current->fd[ufd]].flags & O_RDONLY) {
			return -EBADF;
		}
		if((__ssize_t)io.iov_len < 0) {
			return -EINVAL;
		}
		i = fd_table[current->fd[ufd]].inode;
		if(i->fsop && i->fsop->write) {
			errno = i->fsop->write(i, &fd_table[current->fd[ufd]], io.iov_base, io.iov_len);
			if (errno < 0) {
				return errno;
			}
			bytes_written += errno;
		} else {
			return -EINVAL;
		}
	}
#ifdef __DEBUG__
	printk("%d\n", bytes_written);
#endif /*__DEBUG__ */
	return bytes_written;
}
