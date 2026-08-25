/*
 * fnx/kernel/syscalls/sendfile.c
 *
 * FNX: sendfile(40) - copy data between fds entirely in the kernel.
 *
 * sendfile(out_fd, in_fd, offset, count):
 *   - reads up to 'count' bytes from in_fd (optionally starting at
 *     *offset, which is advanced) and writes them to out_fd.
 *   - a kernel bounce buffer is used (no user-space copy); the read
 *     side must be a regular file (or anything with a ->read method),
 *     the write side any writable fd.
 *
 * Implemented over the existing fsop read/write so it works for files,
 * pipes and sockets alike; not a zero-copy splice, but it removes the
 * user-space round trip that would otherwise copy the data twice.
 */

#include <fnx/fs.h>
#include <fnx/process.h>
#include <fnx/fcntl.h>
#include <fnx/errno.h>
#include <fnx/mm.h>
#include <fnx/string.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#endif /*__DEBUG__ */

int sys_sendfile(int out_fd, int in_fd, __off_t *offset, __size_t count)
{
	struct inode *i_in, *i_out;
	unsigned int fd_in, fd_out;
	char *buf;
	__size_t total, n, chunk;
	int errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_sendfile(%d, %d, 0x%x, %d)\n", current->pid, out_fd, in_fd, (unsigned int)offset, count);
#endif /*__DEBUG__ */

	CHECK_UFD(out_fd);
	CHECK_UFD(in_fd);
	fd_out = current->fd[out_fd];
	fd_in = current->fd[in_fd];

	if(!(fd_table[fd_out].flags & (O_WRONLY | O_RDWR))) {
		return -EBADF;
	}
	if(fd_table[fd_in].flags & O_WRONLY) {
		return -EBADF;
	}
	i_out = fd_table[fd_out].inode;
	i_in = fd_table[fd_in].inode;
	if(!i_in->fsop || !i_in->fsop->read) {
		return -EINVAL;
	}
	if(!i_out->fsop || !i_out->fsop->write) {
		return -EINVAL;
	}

	if(offset) {
		if((errno = check_user_area(VERIFY_READ, offset, sizeof(__off_t)))) {
			return errno;
		}
		if(*offset < 0) {
			return -EINVAL;
		}
	}

	if(!(buf = (char *)kmalloc(PAGE_SIZE))) {
		return -ENOMEM;
	}

	total = 0;
	while(total < count) {
		chunk = count - total;
		if(chunk > PAGE_SIZE) {
			chunk = PAGE_SIZE;
		}

		if(offset) {
			/* positional read: temporarily move the file offset,
			 * read, then restore it */
			__loff_t save;

			save = fd_table[fd_in].offset;
			fd_table[fd_in].offset = *offset;
			errno = i_in->fsop->read(i_in, &fd_table[fd_in], buf, chunk);
			fd_table[fd_in].offset = save;
			if(errno <= 0) {
				break;
			}
			*offset += errno;
			n = errno;
		} else {
			if((errno = i_in->fsop->read(i_in, &fd_table[fd_in], buf, chunk)) <= 0) {
				break;
			}
			n = errno;
		}

		errno = i_out->fsop->write(i_out, &fd_table[fd_out], buf, n);
		if(errno < 0) {
			total = 0;
			break;
		}
		total += n;
	}

	kfree((addr_t)buf);
	return (int)total;
}
