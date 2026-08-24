/*
 * fnx/kernel/syscalls/socketcall64.c
 *
 * Copyright 2026, Kyle J Cardoza. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 *
 * FNX: direct x86-64 socket syscalls (41-55). musl x86-64 uses these
 * instead of the i386 sys_socketcall() multiplexer (which this port
 * deleted). The underlying implementations live in net/socket.c and take
 * the direct arguments; these wrappers match the Linux x86-64 ABI arg
 * order (which for accept/recvfrom differs from the C function order).
 */

#include <fnx/config.h>
#include <fnx/net.h>
#include <fnx/errno.h>
#include <fnx/process.h>
#include <fnx/limits.h>
#include <fnx/mm.h>
#include <fnx/string.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
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

/*
 * x86-64 struct msghdr ABI (LP64, 56 bytes):
 *   void *msg_name;         @0
 *   socklen_t msg_namelen;  @8
 *   struct iovec *msg_iov;  @16
 *   int msg_iovlen;         @24
 *   int __pad1;             @28
 *   void *msg_control;      @32
 *   socklen_t msg_controllen; @40
 *   int __pad2;             @44
 *   int msg_flags;          @48
 */
struct msghdr_abi {
	void *msg_name;
	int msg_namelen;
	void *msg_iov;
	int msg_iovlen;
	int __pad1;
	void *msg_control;
	int msg_controllen;
	int __pad2;
	int msg_flags;
};

/* user iovec -> contiguous kernel buffer (gather). Returns bytes gathered
 * or a negative errno; *user_len_out gets the total user-requested bytes. */
static int msg_gather(struct msghdr_abi *mh, char **kbuf, __size_t *total_out)
{
	struct iovec io;
	__size_t total;
	char *buf, *dst;
	int vi, errno;

	if(mh->msg_iovlen < 0 || mh->msg_iovlen > UIO_MAXIOV) {
		return -EINVAL;
	}
	total = 0;
	for(vi = 0; vi < mh->msg_iovlen; vi++) {
		if((errno = check_user_area(VERIFY_READ, (struct iovec *)mh->msg_iov + vi, sizeof(struct iovec)))) {
			return errno;
		}
		io = ((struct iovec *)mh->msg_iov)[vi];
		total += io.iov_len;
	}
	if(!(buf = (char *)kmalloc(total ? total : 1))) {
		return -ENOMEM;
	}
	dst = buf;
	for(vi = 0; vi < mh->msg_iovlen; vi++) {
		io = ((struct iovec *)mh->msg_iov)[vi];
		if(io.iov_len) {
			if((errno = check_user_area(VERIFY_READ, io.iov_base, io.iov_len))) {
				kfree((addr_t)buf);
				return errno;
			}
			memcpy_b(dst, io.iov_base, io.iov_len);
			dst += io.iov_len;
		}
	}
	*kbuf = buf;
	*total_out = total;
	return 0;
}

/* contiguous kernel buffer -> user iovecs (scatter). Returns bytes
 * scattered (may be less than the buffer if the iovs are short). */
static int msg_scatter(struct msghdr_abi *mh, char *src, __size_t len)
{
	struct iovec io;
	__size_t off;
	int vi, errno;

	if(mh->msg_iovlen < 0 || mh->msg_iovlen > UIO_MAXIOV) {
		return -EINVAL;
	}
	off = 0;
	for(vi = 0; vi < mh->msg_iovlen && off < len; vi++) {
		if((errno = check_user_area(VERIFY_READ, (struct iovec *)mh->msg_iov + vi, sizeof(struct iovec)))) {
			return errno;
		}
		io = ((struct iovec *)mh->msg_iov)[vi];
		if(!io.iov_len) {
			continue;
		}
		if((errno = check_user_area(VERIFY_WRITE, io.iov_base, io.iov_len))) {
			return errno;
		}
		if(io.iov_len > len - off) {
			io.iov_len = len - off;
		}
		memcpy_b(io.iov_base, src + off, io.iov_len);
		off += io.iov_len;
	}
	return off;
}

int sys_sendmsg(int sd, const struct msghdr_abi *msg, int flags)
{
	struct msghdr_abi mh;
	char *kbuf;
	__size_t total;
	int errno, ret;

	if((errno = check_user_area(VERIFY_READ, msg, sizeof(struct msghdr_abi)))) {
		return errno;
	}
	memcpy_b(&mh, msg, sizeof(struct msghdr_abi));
	if((errno = msg_gather(&mh, &kbuf, &total))) {
		return errno;
	}
	/* ancillary data (SCM_RIGHTS etc.) is not implemented */
	if(mh.msg_controllen) {
		kfree((addr_t)kbuf);
		return -EINVAL;
	}
	/* user sockaddr was validated by msg_gather's iov walk? No - validate
	 * the destination address like sendto() does before the kernel derefs it */
	if(mh.msg_name) {
		if((errno = check_user_area(VERIFY_READ, mh.msg_name, mh.msg_namelen)) < 0) {
			kfree((addr_t)kbuf);
			return errno;
		}
		ret = msg_send(sd, kbuf, total, flags, (struct sockaddr *)mh.msg_name, mh.msg_namelen);
	} else {
		ret = msg_send(sd, kbuf, total, flags, NULL, 0);
	}
	kfree((addr_t)kbuf);
	return ret;
}

int sys_recvmsg(int sd, struct msghdr_abi *msg, int flags)
{
	struct msghdr_abi mh;
	char *kbuf;
	struct sockaddr ret_addr;
	int addrlen, errno, ret;
	__size_t total;

	if((errno = check_user_area(VERIFY_READ, msg, sizeof(struct msghdr_abi)))) {
		return errno;
	}
	memcpy_b(&mh, msg, sizeof(struct msghdr_abi));

	total = 0;
	{
		struct iovec io;
		int vi;
		if(mh.msg_iovlen < 0 || mh.msg_iovlen > UIO_MAXIOV) {
			return -EINVAL;
		}
		for(vi = 0; vi < mh.msg_iovlen; vi++) {
			if((errno = check_user_area(VERIFY_READ, (struct iovec *)mh.msg_iov + vi, sizeof(struct iovec)))) {
				return errno;
			}
			io = ((struct iovec *)mh.msg_iov)[vi];
			total += io.iov_len;
		}
	}
	if(!(kbuf = (char *)kmalloc(total ? total : 1))) {
		return -ENOMEM;
	}

	/* source address: msg_recv fills a kernel-side sockaddr; copy it back
	 * to the user msg_name below */
	addrlen = sizeof(struct sockaddr);
	if(mh.msg_name) {
		ret = msg_recv(sd, kbuf, total, flags, &ret_addr, &addrlen);
	} else {
		ret = msg_recv(sd, kbuf, total, flags, NULL, NULL);
	}
	if(ret < 0) {
		kfree((addr_t)kbuf);
		return ret;
	}
	errno = ret;	/* bytes read */

	/* scatter into the user iovs */
	{
		struct msghdr_abi mh2;
		memcpy_b(&mh2, &mh, sizeof(struct msghdr_abi));
		msg_scatter(&mh2, kbuf, errno);
	}
	/* report the source address back to the caller */
	if(mh.msg_name && addrlen > 0) {
		if((ret = check_user_area(VERIFY_WRITE, mh.msg_name, addrlen)) < 0) {
			kfree((addr_t)kbuf);
			return ret;
		}
		memcpy_b(mh.msg_name, &ret_addr, addrlen);
		if((ret = check_user_area(VERIFY_WRITE, &msg->msg_namelen, sizeof(int))) < 0) {
			kfree((addr_t)kbuf);
			return ret;
		}
		((struct msghdr_abi *)msg)->msg_namelen = addrlen;
	}
	kfree((addr_t)kbuf);
	return errno;
}

#endif /* CONFIG_NET */
