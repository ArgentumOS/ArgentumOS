/*
 * fnx/include/fnx/fs_epoll.h
 *
 * FNX epoll(7) support: a minimal epoll instance stored in the inode
 * union. The x86-64 struct epoll_event is the LP64 layout (events u32 +
 * data u64 = 16 bytes), which is what musl's <sys/epoll.h> expects.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#ifndef _FNX_FS_EPOLL_H
#define _FNX_FS_EPOLL_H

#include <fnx/types.h>

/* EPOLL* event bits (x86-64 values, same as Linux) */
#define EPOLLIN		0x001
#define EPOLLPRI	0x002
#define EPOLLOUT	0x004
#define EPOLLERR	0x008
#define EPOLLHUP	0x010
#define EPOLLRDNORM	0x040
#define EPOLLWRNORM	0x100
#define EPOLLRDHUP	0x2000

/* epoll_ctl ops */
#define EPOLL_CTL_ADD	1
#define EPOLL_CTL_DEL	2
#define EPOLL_CTL_MOD	3

/* epoll_create1 flags */
#define EPOLL_CLOEXEC	02000000	/* == O_CLOEXEC (x86-64) */

struct epoll_event {
	__u32 events;		/* EPOLL* bitmask */
	__u64 data;		/* user data */
};

/* one watched fd inside an epoll instance */
struct epoll_item {
	int fd;			/* user fd number being watched */
	__u32 events;		/* interest mask (EPOLL*) */
	__u64 data;		/* user data returned on readiness */
	struct epoll_item *next;
};

struct epoll_inode {
	struct epoll_item *items;	/* watched fd list */
	int count;			/* number of items */
};

#endif /* _FNX_FS_EPOLL_H */
