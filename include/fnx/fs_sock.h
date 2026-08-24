/*
 * fnx/include/fnx/fs_sock.h
 *
 * Copyright 2023, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#ifdef CONFIG_NET

#ifndef _FNX_FS_SOCK_H
#define _FNX_FS_SOCK_H

#include <fnx/net.h>

extern struct fs_operations sockfs_fsop;

struct sockfs_inode {
	struct socket sock;
};

#endif /* _FNX_FS_SOCK_H */

#endif /* CONFIG_NET */
