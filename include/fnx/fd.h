/*
 * fnx/include/fnx/fd.h
 *
 * Copyright 2023, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#ifndef _FNX_FD_H
#define _FNX_FD_H

#include <fnx/config.h>
#include <fnx/types.h>

/*
 * A NEGATIVE DESCRIPTOR IS TESTED FIRST, AND IT USED TO BE TESTED NOWHERE: `-1 > OPEN_MAX-1` is false, so
 * control reached `current->fd[-1]` - AN OUT-OF-BOUNDS READ PAST THE ARRAY - and returned whatever the
 * neighbouring kernel memory held. A printk showed a garbage "slot" of 60725 PASSING the check, after which
 * get_socket() read a wild inode and !S_ISSOCK answered ENOTSOCK. Linux answers EBADF without touching
 * memory, and what a caller saw here was NONDETERMINISTIC - "Bad file descriptor" in one run and
 * "Not a socket" in the next - which is exactly what an out-of-bounds read looks like from the outside.
 */
#define CHECK_UFD(ufd)							\
{									\
	if((ufd) < 0 || (ufd) > (OPEN_MAX - 1) || current->fd[(ufd)] == 0) {	\
		return -EBADF;						\
	}								\
}

extern unsigned int fd_table_size;	/* size in bytes */
extern struct fd *fd_table;

struct fd {
	struct inode *inode;		/* file inode */
	unsigned short int flags;	/* flags */
	unsigned short int count;	/* number of opened instances */
#ifdef CONFIG_OFFSET64
	__loff_t offset;		/* r/w pointer position */
#else
	__off_t offset;			/* r/w pointer position */
#endif /* CONFIG_OFFSET64 */
	void *private_data;		/* needed for tty driver */
};

#endif /* _FNX_FS_H */
