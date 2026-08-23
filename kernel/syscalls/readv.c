/*
 * fiwix/kernel/syscalls/readv.c
 *
 * Copyright 2023, Jordi Sanfeliu. All rights reserved.
 * Copyright 2023, Richard R. Masters.
 * Distributed under the terms of the Fiwix License.
 */

#include <fiwix/fs.h>
#include <fiwix/fcntl.h>
#include <fiwix/errno.h>

#ifdef __DEBUG__
#include <fiwix/stdio.h>
#include <fiwix/process.h>
#endif /*__DEBUG__ */

int sys_readv(unsigned int ufd, const struct iovec *iov, int iovcnt)
{
	struct inode *i;
	int errno;
	int bytes_read = 0;
	int vi;	/* vector index */

#ifdef __DEBUG__
	printk("(pid %d) sys_readv(%d, 0x%08x, %d) -> ", current->pid, ufd, iov, iovcnt);
#endif /*__DEBUG__ */

	CHECK_UFD(ufd);
	if(iovcnt < 0 || iovcnt > UIO_MAXIOV) {
		return -EINVAL;
	}
	for (vi = 0; vi < iovcnt; vi++) {
		struct iovec io;
#ifdef __x86_64__
		if(current->flags & PF_ELF64) {
			/* native x86_64 userland: full 64-bit struct iovec */
			io = ((struct iovec *)iov)[vi];
		} else {
			const struct iovec32 *io32 = (const struct iovec32 *)iov + vi;
			io.iov_base = (void *)(unsigned long)io32->iov_base;
			io.iov_len = io32->iov_len;
		}
#else
		io = iov[vi];
#endif /* __x86_64__ */
		if((errno = check_user_area(VERIFY_WRITE, io.iov_base, io.iov_len))) {
			return errno;
		}
		if(fd_table[current->fd[ufd]].flags & O_WRONLY) {
			return -EBADF;
		}
		if(!io.iov_len) {
			continue;
		}
		if((__ssize_t)io.iov_len < 0) {
			return -EINVAL;
		}

		i = fd_table[current->fd[ufd]].inode;
		if(i->fsop && i->fsop->read) {
			errno = i->fsop->read(i, &fd_table[current->fd[ufd]], io.iov_base, io.iov_len);
			if (errno < 0) {
			    return errno;
			}
			bytes_read += errno;
			if (errno < io.iov_len) {
				break;
			}
		} else {
			return -EINVAL;
		}
	}
#ifdef __DEBUG__
	printk("%d\n", bytes_read);
#endif /*__DEBUG__ */
	return bytes_read;
}
