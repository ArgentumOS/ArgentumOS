/*
 * fnx/kernel/syscalls/epoll.c
 *
 * epoll(7): epoll_create(213), epoll_create1(232), epoll_ctl(233),
 * epoll_wait(234), epoll_pwait(281). A minimal implementation over the
 * existing select()/poll() readiness machinery: each epoll instance is
 * an anonymous inode (allocated on the pipefs superblock) holding a
 * linked list of watched fds; epoll_wait reuses do_check() on every
 * item and sleeps on &do_select with the caller's timeout, exactly like
 * sys_poll().
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#include <fnx/syscalls.h>
#include <fnx/fs.h>
#include <fnx/filesystems.h>
#include <fnx/fs_epoll.h>
#include <fnx/fcntl.h>
#include <fnx/stat.h>
#include <fnx/fd.h>
#include <fnx/errno.h>
#include <fnx/string.h>
#include <fnx/sched.h>
#include <fnx/mm.h>
#include <fnx/timer.h>
#include <fnx/stdio.h>

/* the epoll inode's fsop: only close (free items) and select (allow an
 * epoll fd to be watched by another epoll/select) matter */
static int epoll_close(struct inode *i, struct fd *f);
static int epoll_select(struct inode *i, struct fd *f, int flag);

static struct fs_operations epoll_fsop = {
	0,
	0,

	NULL,			/* open */
	epoll_close,		/* close */
	NULL,			/* read */
	NULL,			/* write */
	NULL,			/* ioctl */
	NULL,			/* llseek */
	NULL,			/* readdir */
	NULL,			/* readdir64 */
	NULL,			/* mmap */
	epoll_select,		/* select */

	NULL,			/* readlink */
	NULL,			/* followlink */
	NULL,			/* bmap */
	NULL,			/* lookup */
	NULL,			/* rmdir */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* mknod */
	NULL,			/* truncate */
	NULL,			/* create */
	NULL,			/* rename */

	NULL,			/* read_block */
	NULL,			/* write_block */
	NULL,			/* read_inode */
	NULL,			/* write_inode */
	NULL,			/* ialloc */
	NULL,			/* ifree */
	NULL,			/* statfs */
	NULL,			/* read_superblock */
	NULL,			/* remount_fs */
	NULL,			/* write_superblock */
	NULL			/* release_superblock */
};

static int epoll_close(struct inode *i, struct fd *f)
{
	struct epoll_inode *ep;
	struct epoll_item *item, *next;

	ep = &i->u.epoll;
	for(item = ep->items; item; item = next) {
		next = item->next;
		kfree((addr_t)item);
	}
	ep->items = NULL;
	ep->count = 0;
	return 0;
}

static int epoll_select(struct inode *i, struct fd *f, int flag)
{
	/* report ready if any watched fd is currently ready */
	struct epoll_inode *ep;
	struct epoll_item *item;
	int n;

	ep = &i->u.epoll;
	for(item = ep->items; item; item = item->next) {
		if(item->fd < 0 || item->fd >= NR_OPENS || !current->fd[item->fd]) {
			continue;
		}
		if(!(n = do_check(fd_table[current->fd[item->fd]].inode, &fd_table[current->fd[item->fd]], flag))) {
			continue;
		}
		return 1;
	}
	return 0;
}

/* lookup an item by watched fd */
static struct epoll_item *epoll_find(struct epoll_inode *ep, int fd)
{
	struct epoll_item *item;

	for(item = ep->items; item; item = item->next) {
		if(item->fd == fd) {
			return item;
		}
	}
	return NULL;
}

/* validate the interest mask: only the bits we can report on */
static int epoll_valid_events(__u32 events)
{
	if(events & ~(EPOLLIN | EPOLLPRI | EPOLLOUT | EPOLLERR | EPOLLHUP |
		      EPOLLRDNORM | EPOLLWRNORM | EPOLLRDHUP)) {
		return 0;
	}
	return 1;
}

int sys_epoll_create(int size)
{
	return do_epoll_create(0);
}

int sys_epoll_create1(int flags)
{
	if(flags & ~EPOLL_CLOEXEC) {
		return -EINVAL;
	}
	return do_epoll_create(flags);
}

int do_epoll_create(int flags)
{
	int fd, ufd;
	struct filesystems *fs;
	struct inode *i;

	if(!(fs = get_filesystem("pipefs"))) {
		printk("WARNING: %s(): pipefs filesystem is not registered!\n", __FUNCTION__);
		return -EINVAL;
	}
	/* raw inode from the pool: pipefs's ialloc would allocate a FIFO
	 * buffer page in the u.pipefs union slot we reuse, and its ifree
	 * would then kfree() our epoll data. i_nlink = 1 keeps iput() from
	 * running any sb->fsop->ifree; rdev > FS_NODEV makes iput() drop
	 * the inode from the pool entirely. */
	if(!(i = get_free_inode())) {
		return -ENOMEM;
	}
	i->count = 1;
	i->i_nlink = 1;
	i->sb = &fs->mp->sb;
	i->dev = i->rdev = fs->mp->sb.dev;
	i->i_mode = S_IFIFO;
	i->fsop = &epoll_fsop;
	i->u.epoll.items = NULL;
	i->u.epoll.count = 0;

	if((fd = get_new_fd(i)) < 0) {
		iput(i);
		return -ENFILE;
	}
	if((ufd = get_new_user_fd(0)) < 0) {
		release_fd(fd);
		iput(i);
		return -EMFILE;
	}
	current->fd[ufd] = fd;
	fd_table[fd].flags = O_RDWR;
	if(flags & EPOLL_CLOEXEC) {
		current->fd_flags[ufd] |= FD_CLOEXEC;
	}
	return ufd;
}

int sys_epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
{
	struct inode *ei;
	struct epoll_inode *ep;
	struct epoll_item *item;
	struct epoll_event ev;
	int errno;

	if(epfd < 0 || epfd >= OPEN_MAX || !current->fd[epfd]) {
		return -EBADF;
	}
	ei = fd_table[current->fd[epfd]].inode;
	if(!ei->fsop || ei->fsop != &epoll_fsop) {
		return -EINVAL;	/* not an epoll fd */
	}
	ep = &ei->u.epoll;

	if(fd < 0 || fd >= OPEN_MAX || !current->fd[fd]) {
		return -EBADF;
	}

	switch(op) {
		case EPOLL_CTL_ADD:
			if(epoll_find(ep, fd)) {
				return -EEXIST;
			}
			if((errno = check_user_area(VERIFY_READ, event, sizeof(struct epoll_event)))) {
				return errno;
			}
			memcpy_b(&ev, event, sizeof(struct epoll_event));
			if(!epoll_valid_events(ev.events)) {
				return -EINVAL;
			}
			if(!(item = (struct epoll_item *)kmalloc(sizeof(struct epoll_item)))) {
				return -ENOMEM;
			}
			item->fd = fd;
			item->events = ev.events;
			item->data = ev.data;
			item->next = ep->items;
			ep->items = item;
			ep->count++;
			return 0;
		case EPOLL_CTL_DEL:
			if(!(item = epoll_find(ep, fd))) {
				return -ENOENT;
			}
			if(item == ep->items) {
				ep->items = item->next;
			} else {
				struct epoll_item *prev = ep->items;
				while(prev && prev->next != item) {
					prev = prev->next;
				}
				if(prev) {
					prev->next = item->next;
				}
			}
			kfree((addr_t)item);
			ep->count--;
			return 0;
		case EPOLL_CTL_MOD:
			if(!(item = epoll_find(ep, fd))) {
				return -ENOENT;
			}
			if((errno = check_user_area(VERIFY_READ, event, sizeof(struct epoll_event)))) {
				return errno;
			}
			memcpy_b(&ev, event, sizeof(struct epoll_event));
			if(!epoll_valid_events(ev.events)) {
				return -EINVAL;
			}
			item->events = ev.events;
			item->data = ev.data;
			return 0;
		default:
			return -EINVAL;
	}
}

static int epoll_do_wait(int epfd, struct epoll_event *events, int maxevents,
			 int timeout)
{
	struct inode *ei;
	struct epoll_inode *ep;
	struct epoll_item *item;
	struct epoll_event ev;
	struct fd *f;
	struct inode *i;
	int n, count;

	if(epfd < 0 || epfd >= OPEN_MAX || !current->fd[epfd]) {
		return -EBADF;
	}
	ei = fd_table[current->fd[epfd]].inode;
	if(!ei->fsop || ei->fsop != &epoll_fsop) {
		return -EINVAL;
	}
	ep = &ei->u.epoll;
	if(maxevents <= 0) {
		return -EINVAL;
	}
	if((n = check_user_area(VERIFY_WRITE, events, sizeof(struct epoll_event) * maxevents))) {
		return n;
	}

	if(timeout < 0) {
		current->timeout = INFINITE_WAIT;
	} else {
		struct timeval tv;

		tv.tv_sec = timeout / 1000;
		tv.tv_usec = (timeout % 1000) * 1000;
		current->timeout = tv2ticks(&tv);
	}

	for(;;) {
		count = 0;
		for(item = ep->items; item; item = item->next) {
			if(count >= maxevents) {
				break;
			}
			if(item->fd < 0 || item->fd >= NR_OPENS || !current->fd[item->fd]) {
				ev.events = EPOLLERR | EPOLLHUP;
				ev.data = item->data;
				memcpy_b(&events[count], &ev, sizeof(struct epoll_event));
				count++;
				continue;
			}
			f = &fd_table[current->fd[item->fd]];
			i = f->inode;
			ev.events = 0;
			ev.data = item->data;

			if(!i->fsop || !i->fsop->select) {
				/* no select method: always ready */
				if(item->events & (EPOLLIN | EPOLLRDNORM | EPOLLPRI)) {
					ev.events |= EPOLLIN | EPOLLRDNORM;
				}
				if(item->events & (EPOLLOUT | EPOLLWRNORM)) {
					ev.events |= EPOLLOUT | EPOLLWRNORM;
				}
			} else {
				if(item->events & (EPOLLIN | EPOLLRDNORM)) {
					if(do_check(i, f, SEL_R)) {
						ev.events |= EPOLLIN | EPOLLRDNORM;
					}
				}
				if(item->events & (EPOLLOUT | EPOLLWRNORM)) {
					if(do_check(i, f, SEL_W)) {
						ev.events |= EPOLLOUT | EPOLLWRNORM;
					}
				}
			}
			if(ev.events) {
				memcpy_b(&events[count], &ev, sizeof(struct epoll_event));
				count++;
			}
		}

		if(count || !current->timeout || current->sigpending & ~current->sigblocked) {
			break;
		}
		if(sleep(&do_select, PROC_INTERRUPTIBLE)) {
			current->timeout = 0;
			return -EINTR;
		}
	}
	current->timeout = 0;

	return count;
}

int sys_epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
	return epoll_do_wait(epfd, events, maxevents, timeout);
}

int sys_epoll_pwait(int epfd, struct epoll_event *events, int maxevents,
		    int timeout, const unsigned long *sigmask, int sigsetsize)
{
	int ret;

	if(sigmask) {
		/* FNX: no per-syscall sigmask support; accept and ignore */
	}
	ret = epoll_do_wait(epfd, events, maxevents, timeout);
	return ret;
}
