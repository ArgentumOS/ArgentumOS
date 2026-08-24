/*
 * fiwix/kernel/syscalls/socketcall64.c
 *
 * Copyright 2026, Kyle J Cardoza. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 *
 * Fiwix64: direct x86-64 socket syscalls (41-55). musl x86-64 uses these
 * instead of the i386 sys_socketcall() multiplexer (which this port
 * deleted). The underlying implementations live in net/socket.c and take
 * the direct arguments; these wrappers match the Linux x86-64 ABI arg
 * order (which for accept/recvfrom differs from the C function order).
 */

#include <fiwix/config.h>
#include <fiwix/net.h>
#include <fiwix/errno.h>
#include <fiwix/process.h>

#ifdef __DEBUG__
#include <fiwix/stdio.h>
#endif /*__DEBUG__ */

#ifdef CONFIG_NET

int sys_socket(int domain, int type, int protocol)
{
	return socket(domain, type, protocol);
}

int sys_connect(int sd, struct sockaddr *addr, int addrlen)
{
	return connect(sd, addr, addrlen);
}

int sys_accept(int sd, struct sockaddr *addr, unsigned int *addrlen)
{
	return accept(sd, addr, addrlen);
}

int sys_sendto(int sd, const void *buf, __size_t len, int flags,
	const struct sockaddr *addr, struct sigcontext *sc)
{
	/* x86-64: 6th arg (addrlen) arrives in r9, stashed in sc->r9 */
	return sendto(sd, buf, len, flags, addr, (int)sc->r9);
}

int sys_recvfrom(int sd, void *buf, __size_t len, int flags,
	struct sockaddr *addr, struct sigcontext *sc)
{
	/* x86-64: 6th arg (addrlen ptr) arrives in r9, stashed in sc->r9 */
	int *addrlen = (int *)sc->r9;

	return recvfrom(sd, buf, len, flags, addr, addrlen);
}

int sys_shutdown(int sd, int how)
{
	return shutdown(sd, how);
}

int sys_bind(int sd, struct sockaddr *addr, int addrlen)
{
	return bind(sd, addr, addrlen);
}

int sys_listen(int sd, int backlog)
{
	return listen(sd, backlog);
}

int sys_getsockname(int sd, struct sockaddr *addr, unsigned int *addrlen)
{
	return getname(sd, addr, addrlen, SYS_GETSOCKNAME);
}

int sys_getpeername(int sd, struct sockaddr *addr, unsigned int *addrlen)
{
	return getname(sd, addr, addrlen, SYS_GETPEERNAME);
}

int sys_socketpair(int domain, int type, int protocol, int sockfd[2])
{
	return socketpair(domain, type, protocol, sockfd);
}

int sys_setsockopt(int sd, int level, int optname, const void *optval, socklen_t optlen)
{
	return setsockopt(sd, level, optname, optval, optlen);
}

int sys_getsockopt(int sd, int level, int optname, void *optval, socklen_t *optlen)
{
	return getsockopt(sd, level, optname, optval, optlen);
}

#endif /* CONFIG_NET */
