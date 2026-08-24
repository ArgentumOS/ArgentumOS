/*
 * fnx/net/core.c
 *
 * Copyright 2025, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/config.h>
#include <fnx/errno.h>
#include <fnx/ioctl.h>
#include <fnx/fs.h>
#include <fnx/socket.h>
#include <fnx/string.h>

#ifdef CONFIG_NET
int dev_ioctl(int cmd, void *arg)
{
	int n;

	switch(cmd) {
		default:
			return -EINVAL;
	}
}
#endif /* CONFIG_NET */
