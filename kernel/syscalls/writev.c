/*
 * fnx/kernel/syscalls/writev.c
 *
 * Copyright 2023, Jordi Sanfeliu. All rights reserved.
 * Copyright 2023, Richard R. Masters.
 * Distributed under the terms of the Fiwix License.
 *
 * FNX (native port): writev over a stream must tolerate partial iovec
 * writes (O_NONBLOCK sockets fill and return short). Each iovec is
 * written to completion OR until the fd would block; a short write
 * resumes the remainder of the same iovec on the next iteration, and
 * when nothing more can be written the accumulated byte count is
 * reported (POSIX), only -EAGAIN when zero bytes were written at all.
 * The old loop moved to the next iovec after a short write and then
 * returned -EAGAIN discarding bytes already queued, so xcb (libX11
 * PutImage via writev on a nonblocking socket) believed nothing was
 * sent and re-sent the whole vector, duplicating data and corrupting
 * the request stream (S1.2 X11 flush stall).
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
	/* the iovec ARRAY itself is user memory dereferenced at CPL0 -
	 * verify it before the loop (an unmapped/short array panicked) */
	if((errno = check_user_area(VERIFY_READ, iov, iovcnt * sizeof(struct iovec)))) {
		return errno;
	}
	i = fd_table[current->fd[ufd]].inode;
	if(fd_table[current->fd[ufd]].flags & O_RDONLY) {
		return -EBADF;
	}
	if(!i->fsop || !i->fsop->write) {
		return -EINVAL;
	}
	for (vi = 0; vi < iovcnt; vi++) {
		struct iovec io;
		__ssize_t off;

		/* FNX (native port): full 64-bit struct iovec */
		io = ((struct iovec *)iov)[vi];
		if(!io.iov_len) {
			continue;
		}
		/* check_user_area's size is 32-bit: reject lengths it would
		 * truncate so the fsop write can never run unverified */
		if(io.iov_len > 0x7FFFFFFFULL) {
			return -EINVAL;
		}
		if((errno = check_user_area(VERIFY_READ, io.iov_base, io.iov_len))) {
			return errno;
		}
		if((__ssize_t)io.iov_len < 0) {
			return -EINVAL;
		}
		/* write this iovec to completion, or until the fd would
		 * block: a short write resumes the iovec remainder */
		off = 0;
		while(off < (__ssize_t)io.iov_len) {
			errno = i->fsop->write(i, &fd_table[current->fd[ufd]],
					       (char *)io.iov_base + off,
					       io.iov_len - off);
			if(errno > 0) {
				bytes_written += errno;
				off += errno;
				continue;
			}
			if(errno < 0) {
				/* -EAGAIN/-EINTR after a partial write:
				 * report the bytes queued so far (POSIX) */
				if(bytes_written) {
					return bytes_written;
				}
				return errno;
			}
			/* zero progress: nothing more can be written */
			return bytes_written;
		}
	}
#ifdef __DEBUG__
	printk("%d\n", bytes_written);
#endif /*__DEBUG__ */
	return bytes_written;
}
