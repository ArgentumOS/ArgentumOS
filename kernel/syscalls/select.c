/*
 * fnx/kernel/syscalls/select.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/types.h>
#include <fnx/fs.h>
#include <fnx/process.h>
#include <fnx/timer.h>
#include <fnx/sched.h>
#include <fnx/sleep.h>
#include <fnx/errno.h>
#include <fnx/stdio.h>
#include <fnx/string.h>

/* poll(2) event/revents bits (x86-64 ABI) */
#define POLLIN		0x001
#define POLLPRI		0x002
#define POLLOUT		0x004
#define POLLERR		0x008
#define POLLHUP		0x010
#define POLLNVAL	0x020
#define POLLRDNORM	0x040
#define POLLRDBAND	0x080
#define POLLWRNORM	0x100
#define POLLWRBAND	0x200
#define POLLMSG		0x400
#define POLLRDHUP	0x2000

static int check_fds(int nfds, fd_set *rfds, fd_set *wfds, fd_set *efds)
{
	int n, bit;
	unsigned int set;

	n = 0;
	for(;;) {
		bit = n * __NFDBITS;
		if(bit >= nfds) {
			break;
		}
		set = rfds->fds_bits[n] | wfds->fds_bits[n] | efds->fds_bits[n];
		while(set) {
			if(__FD_ISSET(bit, rfds) || __FD_ISSET(bit, wfds) || __FD_ISSET(bit, efds)) {
				CHECK_UFD(bit);
			}
			set >>= 1;
			bit++;
		}
		n++;
	}

	return 0;
}

int do_check(struct inode *i, struct fd *f, int flag)
{
	if(i->fsop && i->fsop->select) {
		if(i->fsop->select(i, f, flag)) {
			return 1;
		}
	}

	return 0;
}

/*
 * poll(2) ABI (x86-64): poll(struct pollfd *fds, nfds_t nfds, int timeout_ms).
 * struct pollfd is { int fd; short events; short revents; }.
 */
struct pollfd_abi {
	int fd;
	short events;
	short revents;
};

int sys_poll(struct pollfd_abi *fds, unsigned long nfds, int timeout)
{
	struct pollfd_abi pfd;
	struct inode *i;
	int n, count;

	if(nfds > NR_OPENS) {
		return -EINVAL;
	}
	if((n = check_user_area(VERIFY_WRITE, fds, nfds * sizeof(struct pollfd_abi)))) {
		return n;
	}

	/* timeout is in milliseconds; -1 = infinite, 0 = poll once */
	if(timeout < 0) {
		current->timeout = INFINITE_WAIT;
	} else {
		struct timeval tv;
		tv.tv_sec = timeout / 1000;
		tv.tv_usec = (timeout % 1000) * 1000;
		current->timeout = tv2ticks(&tv);
	}

	count = 0;
	for(;;) {
		count = 0;
		for(n = 0; n < (int)nfds; n++) {
			int err;
			if((err = check_user_area(VERIFY_WRITE, &fds[n], sizeof(struct pollfd_abi)))) {
				return err;
			}
			memcpy_b(&pfd, &fds[n], sizeof(struct pollfd_abi));
			pfd.revents = 0;
			if(pfd.fd < 0) {
				/* negative fds are ignored */
			} else if(pfd.fd >= NR_OPENS || !current->fd[pfd.fd]) {
				pfd.revents |= POLLNVAL;
				count++;
			} else {
				i = fd_table[current->fd[pfd.fd]].inode;
				if(!i->fsop || !i->fsop->select) {
					/* no select method: treat as always ready
					 * (regular files, /dev/null, etc.) */
					if(pfd.events & (POLLIN | POLLRDNORM | POLLPRI)) {
						pfd.revents |= POLLIN | POLLRDNORM;
					}
					if(pfd.events & (POLLOUT | POLLWRNORM)) {
						pfd.revents |= POLLOUT | POLLWRNORM;
					}
					if(pfd.revents) {
						count++;
					}
				} else {
					if(pfd.events & (POLLIN | POLLRDNORM)) {
						if(do_check(i, &fd_table[current->fd[pfd.fd]], SEL_R)) {
							pfd.revents |= POLLIN | POLLRDNORM;
							count++;
						}
					}
					if(pfd.events & (POLLOUT | POLLWRNORM)) {
						if(do_check(i, &fd_table[current->fd[pfd.fd]], SEL_W)) {
							pfd.revents |= POLLOUT | POLLWRNORM;
							count++;
						}
					}
				}
			}
			memcpy_b(&fds[n], &pfd, sizeof(struct pollfd_abi));
		}

		if(count || !current->timeout || current->sigpending & ~current->sigblocked) {
			break;
		}
		if(sleep(&do_select, PROC_INTERRUPTIBLE)) {
			return -EINTR;
		}
	}
	current->timeout = 0;

	return count;
}

int do_select(int nfds, fd_set *rfds, fd_set *wfds, fd_set *efds, fd_set *res_rfds, fd_set *res_wfds, fd_set *res_efds)
{
	int n, count;
	struct inode *i;

	count = 0;
	for(;;) {
		for(n = 0; n < nfds; n++) {
			if(!current->fd[n]) {
				continue;
			}
			i = fd_table[current->fd[n]].inode;
			if(__FD_ISSET(n, rfds)) {
				if(do_check(i, &fd_table[current->fd[n]], SEL_R)) {
					__FD_SET(n, res_rfds);
					count++;
				}
			}
			if(__FD_ISSET(n, wfds)) {
				if(do_check(i, &fd_table[current->fd[n]], SEL_W)) {
					__FD_SET(n, res_wfds);
					count++;
				}
			}
			if(__FD_ISSET(n, efds)) {
				if(do_check(i, &fd_table[current->fd[n]], SEL_E)) {
					__FD_SET(n, res_efds);
					count++;
				}
			}
		}

		if(count || !current->timeout || current->sigpending & ~current->sigblocked) {
			break;
		}
		if(sleep(&do_select, PROC_INTERRUPTIBLE)) {
			return -EINTR;
		}
	}

	return count;
}

int sys_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)
{
	unsigned int t;
	fd_set rfds, wfds, efds;
	fd_set res_rfds, res_wfds, res_efds;
	int errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_select(%d, 0x%08x, 0x%08x, 0x%08x, 0x%08x [%d])\n", current->pid, nfds, (int)readfds, (int)writefds, (int)exceptfds, (int)timeout, (int)timeout ? tv2ticks(timeout): 0);
#endif /*__DEBUG__ */

	if(nfds < 0) {
		return -EINVAL;
	}
	if(nfds > MIN(__FD_SETSIZE, NR_OPENS)) {
		nfds = MIN(__FD_SETSIZE, NR_OPENS);
	}

	if(readfds) {
		if((errno = check_user_area(VERIFY_WRITE, readfds, sizeof(fd_set)))) {
			return errno;
		}
		memcpy_b(&rfds, readfds, sizeof(fd_set));
	} else {
		__FD_ZERO(&rfds);
	}
	if(writefds) {
		if((errno = check_user_area(VERIFY_WRITE, writefds, sizeof(fd_set)))) {
			return errno;
		}
		memcpy_b(&wfds, writefds, sizeof(fd_set));
	} else {
		__FD_ZERO(&wfds);
	}
	if(exceptfds) {
		if((errno = check_user_area(VERIFY_WRITE, exceptfds, sizeof(fd_set)))) {
			return errno;
		}
		memcpy_b(&efds, exceptfds, sizeof(fd_set));
	} else {
		__FD_ZERO(&efds);
	}

	/* check the validity of all fds */
	if((errno = check_fds(nfds, &rfds, &wfds, &efds)) < 0) {
		return errno;
	}

	if(timeout) {
		struct timeval tv;

		/* 'timeout' is read AND written (tv2ticks/ticks2tv): the
		 * fault-recovering copies return -EFAULT instead of an
		 * unchecked kernel read/write or a raced-munmap panic */
		if((errno = copy_from_user(&tv, timeout, sizeof(struct timeval)))) {
			return errno;
		}
		t = tv2ticks(&tv);
	} else {
		t = INFINITE_WAIT;
	}

	__FD_ZERO(&res_rfds);
	__FD_ZERO(&res_wfds);
	__FD_ZERO(&res_efds);

	current->timeout = t;
	if((errno = do_select(nfds, &rfds, &wfds, &efds, &res_rfds, &res_wfds, &res_efds)) < 0) {
		return errno;
	}
	t = current->timeout;
	current->timeout = 0;

	if(readfds) {
		memcpy_b(readfds, &res_rfds, sizeof(fd_set));
	}
	if(writefds) {
		memcpy_b(writefds, &res_wfds, sizeof(fd_set));
	}
	if(exceptfds) {
		memcpy_b(exceptfds, &res_efds, sizeof(fd_set));
	}
	if(timeout) {
		struct timeval tv;

		tv.tv_sec = t / HZ;
		tv.tv_usec = (t % HZ) * (1000000 / HZ);
		if(copy_to_user(timeout, &tv, sizeof(struct timeval))) {
			return -EFAULT;
		}
	}
	return errno;
}
