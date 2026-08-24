/*
 * fnx/include/fnx/netdevice.h
 *
 * Copyright 2026, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#ifdef CONFIG_NET

#ifndef _FNX_NETDEVICE_H
#define _FNX_NETDEVICE_H

int dev_ioctl(int, void *);

#endif /* _FNX_NETDEVICE_H */

#endif /* CONFIG_NET */
