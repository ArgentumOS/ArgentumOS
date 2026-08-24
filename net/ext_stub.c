/*
 * fnx/net/ext_stub.c
 *
 * Copyright 2026, Kyle J Cardoza. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 *
 * FNX: stub for the external (NIC driver) interface that net/ipv4.c
 * calls. Fiwix mainline never shipped a NIC driver either - these hooks
 * were always expected to be provided by a future driver. Without a NIC,
 * every IP operation returns -ENODEV; UNIX-domain sockets (net/unix.c)
 * do not use them and work fully.
 */

#include <fnx/config.h>
#include <fnx/errno.h>
#include <fnx/net.h>

#ifdef CONFIG_NET

void ext_init(void)
{
}

int ext_open(int domain, int type, int protocol)
{
	(void)domain; (void)type; (void)protocol;
	return -ENODEV;
}

int ext_close(int fd_ext)
{
	(void)fd_ext;
	return -ENODEV;
}

int ext_bind(int fd_ext, const struct sockaddr *addr, int addrlen)
{
	(void)fd_ext; (void)addr; (void)addrlen;
	return -ENODEV;
}

int ext_listen(int fd_ext, int backlog)
{
	(void)fd_ext; (void)backlog;
	return -ENODEV;
}

int ext_connect(int fd_ext, const struct sockaddr *addr, int addrlen)
{
	(void)fd_ext; (void)addr; (void)addrlen;
	return -ENODEV;
}

int ext_accept(int fd_ext, struct sockaddr *addr, unsigned int *addrlen)
{
	(void)fd_ext; (void)addr; (void)addrlen;
	return -ENODEV;
}

int ext_ioctl(int fd_ext, int cmd, void *arg)
{
	(void)fd_ext; (void)cmd; (void)arg;
	return -ENODEV;
}

int ext_sendto(int fd_ext, const void *buffer, __size_t count, const struct sockaddr *addr, int addrlen)
{
	(void)fd_ext; (void)buffer; (void)count; (void)addr; (void)addrlen;
	return -ENODEV;
}

int ext_recvfrom(int fd_ext, void *buffer, __size_t count, struct sockaddr *addr, int *addrlen)
{
	(void)fd_ext; (void)buffer; (void)count; (void)addr; (void)addrlen;
	return -ENODEV;
}

int ext_read(int fd_ext, void *buffer, __size_t count)
{
	(void)fd_ext; (void)buffer; (void)count;
	return -ENODEV;
}

int ext_write(int fd_ext, const void *buffer, __size_t count)
{
	(void)fd_ext; (void)buffer; (void)count;
	return -ENODEV;
}

#endif /* CONFIG_NET */
