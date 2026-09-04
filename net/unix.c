/*
 * fnx/net/unix.c
 *
 * Copyright 2023, Jordi Sanfeliu. All rights reserved.
 * Portions Copyright 2024, Greg Haerr.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/config.h>
#include <fnx/fs.h>
#include <fnx/stat.h>
#include <fnx/errno.h>
#include <fnx/socket.h>
#include <fnx/net.h>
#include <fnx/netdevice.h>
#include <fnx/net/unix.h>
#include <fnx/fcntl.h>
#include <fnx/sched.h>
#include <fnx/sleep.h>
#include <fnx/mm.h>
#include <fnx/string.h>
#include <fnx/stdio.h>

#ifdef CONFIG_NET
struct unix_info *unix_socket_head;

static struct resource packet_resource = { 0, 0 };

static void add_unix_socket(struct unix_info *u)
{
	struct unix_info *h;

	if((h = unix_socket_head)) {
		while(h->next) {
			h = h->next;
		}
		h->next = u;
	} else {
		unix_socket_head = u;
	}
}

static void remove_unix_socket(struct unix_info *u)
{
	struct unix_info *h;

	if(unix_socket_head == u) {
		unix_socket_head = u->next;
		return;
	}

	h = unix_socket_head;
	while(h && h->next != u) {
		h = h->next;
	}
	if(h && h->next == u) {
		h->next = u->next;
	}
}

/* FNX: abstract-socket names are sun_path starting with '\\0'; the name is
 * the bytes up to the second NUL (or the stored length). Compare two
 * bound/connecting abstract names for equality. */
/* FNX: abstract-socket names are sun_path[0] == '\\0'; the meaningful name
 * runs from sun_path[0] to the second NUL (or the end of the stored/passed
 * address). u is a BOUND abstract socket; su/addrlen is the connect() or
 * bind() side. Returns 1 when both name the same abstract socket. */
static int unix_abstract_match(struct unix_info *u, struct sockaddr_un *su, int addrlen)
{
	struct sockaddr_un *su2 = u->sun;
	int len, len2, i;

	if(!su2 || su2->sun_path[0] != '\0' || su->sun_path[0] != '\0') {
		return 0;
	}
	len = addrlen - 2;
	len2 = u->sun_len - 2;
	if(len > 0 && len < (int)sizeof(su->sun_path) &&
	   len2 > 0 && len2 < (int)sizeof(su2->sun_path)) {
		/* cut both at their first NUL beyond the leading one so a
		 * full-length sockaddr_un (trailing NUL padding) matches a
		 * length-exact connect address */
		for(i = 1; i < len; i++) {
			if(su->sun_path[i] == '\0') {
				len = i;
				break;
			}
		}
		for(i = 1; i < len2; i++) {
			if(su2->sun_path[i] == '\0') {
				len2 = i;
				break;
			}
		}
		if(len == len2 && !memcmp(su2->sun_path, su->sun_path, len)) {
			return 1;
		}
	}
	return 0;
}

static struct unix_info *lookup_unix_socket(char *path, struct inode *i)
{
	struct unix_info *u;

	u = unix_socket_head;
	while(u) {
		if(u->sun) {
			if(!strcmp(u->sun->sun_path, path) && u->inode == i) {
				return u;
			}
		}
		u = u->next;
	}

	return NULL;
}

/* FNX: find a socket bound in the abstract namespace with the same name */
static struct unix_info *lookup_unix_abstract(struct sockaddr_un *su, int addrlen)
{
	struct unix_info *u;

	if(su->sun_path[0] != '\0') {
		return NULL;
	}
	u = unix_socket_head;
	while(u) {
		if(u->socket && unix_abstract_match(u, su, addrlen)) {
			return u;
		}
		u = u->next;
	}
	return NULL;
}

int unix_create(struct socket *s, int domain, int type, int protocol)
{
	struct unix_info *u;

	u = &s->u.unix_info;
	memset_b(u, 0, sizeof(struct unix_info));
	u->count = 1;
	u->socket = s;
	add_unix_socket(u);
	return 0;
}

void unix_free(struct socket *s)
{
	struct unix_info *u;

	u = &s->u.unix_info;
	if(!(--u->count)) {
		if(u->data) {
			kfree((addr_t)u->data);
		}
		if(u->sun) {
			kfree((addr_t)u->sun);
		}
		if(u->inode) {
			iput(u->inode);
		}
		u->peer = NULL;
		remove_unix_socket(u);
		return;
	}

	if(u->peer) {
		if(!--u->peer->count) {
			remove_unix_socket(u->peer);
		}
		if(u->peer->socket) {
			u->peer->socket->state = SS_DISCONNECTING;
		}
		wakeup(u->peer);
		wakeup(&do_select);
	}
	remove_unix_socket(u);
	return;
}

int unix_bind(struct socket *s, const struct sockaddr *addr, int addrlen)
{
	struct inode *i;
	struct sockaddr_un *su;
	struct unix_info *u;
	int errno;

	su = (struct sockaddr_un *)addr;
	if(su->sun_family != AF_UNIX) {
                return -EINVAL;
	}
	if(addrlen < 0 || addrlen > sizeof(struct sockaddr_un)) {
                return -EINVAL;
	}

	if(s->u.unix_info.sun) {
		return -EINVAL;
	}
	if(!(s->u.unix_info.sun = (struct sockaddr_un *)kmalloc(sizeof(struct sockaddr_un)))) {
		return -ENOMEM;
	}
	memset_b(s->u.unix_info.sun, 0, sizeof(struct sockaddr_un));
	memcpy_b(s->u.unix_info.sun, su, addrlen);
	s->u.unix_info.sun_len = addrlen;

	/* FNX: abstract sockets (sun_path[0] == '\\0') live in the abstract
	 * namespace: no filesystem node, matched by name at connect(). This
	 * is what libxcb/X clients use by default (they connect to the
	 * abstract form of /tmp/.X11-unix/X0). */
	if(su->sun_path[0] == '\0') {
		u = unix_socket_head;
		while(u) {
			if(u != &s->u.unix_info && u->socket &&
			   unix_abstract_match(u, su, addrlen)) {
				kfree((addr_t)s->u.unix_info.sun);
				s->u.unix_info.sun = NULL;
				return -EADDRINUSE;
			}
			u = u->next;
		}
		return 0;
	}

	errno = do_mknod((char *)su->sun_path, S_IFSOCK | (S_IRWXU | S_IRWXG | S_IRWXO), 0);
	if(errno < 0) {
		kfree((addr_t)s->u.unix_info.sun);
		s->u.unix_info.sun = NULL;
		if(errno == -EEXIST) {
			errno = -EADDRINUSE;
		}
		return errno;
	}
	if((errno = namei(su->sun_path, &i, NULL, FOLLOW_LINKS))) {
		kfree((addr_t)s->u.unix_info.sun);
		s->u.unix_info.sun = NULL;
		return errno;
	}
	s->u.unix_info.inode = i;
	return errno;
}

int unix_listen(struct socket *s, int backlog)
{
	return 0;
}

int unix_connect(struct socket *sc, const struct sockaddr *addr, int addrlen)
{
	struct inode *i;
	struct sockaddr_un *su;
	struct unix_info *up;
	char *tmp_name;
	int errno;

	su = (struct sockaddr_un *)addr;
	if(su->sun_family != AF_UNIX) {
                return -EINVAL;
	}
	if(addrlen < 0 || addrlen > sizeof(struct sockaddr_un)) {
                return -EINVAL;
	}

	/* FNX: abstract connect - match a bound abstract socket by name */
	if(su->sun_path[0] == '\0') {
		struct unix_info *u;

		if(!(u = lookup_unix_abstract(su, addrlen))) {
			return -ENOENT;
		}
		up = u;
		goto do_queue;
	}

	if((errno = malloc_name(su->sun_path, &tmp_name)) < 0) {
		return errno;
	}
	if((errno = namei(tmp_name, &i, NULL, FOLLOW_LINKS))) {
		free_name(tmp_name);
		return errno;
	}
	if(!(up = lookup_unix_socket(tmp_name, i))) {
		iput(i);
		free_name(tmp_name);
		return -ECONNREFUSED;
	}
	iput(i);
	free_name(tmp_name);
do_queue:
	if((errno = insert_socket_to_queue(up->socket, sc))) {
		return errno;
	}
	wakeup(up->socket);
	wakeup(&do_select);	/* a select()-ing listener must re-check */
	sleep(sc, PROC_INTERRUPTIBLE);
	return 0;
}

int unix_accept(struct socket *ss, struct sockaddr *addr, unsigned int *addrlen)
{
	int ufd;
	struct socket *sc, *nss;
	struct unix_info *uc, *us;
	int errno;

	while(!(sc = get_socket_from_queue(ss))) {
		if(ss->fd->flags & O_NONBLOCK) {
			return -EAGAIN;
		}
		if(sleep(ss, PROC_INTERRUPTIBLE)) {
			return -EINTR;
		}
	}

	nss = NULL;
	if((ufd = sock_alloc(&nss)) < 0) {
		return ufd;
	}
	nss->type = ss->type;
	nss->ops = ss->ops;
	if((errno = nss->ops->create(nss, 0, 0, 0)) < 0) {
		sock_free(nss);
		return errno;
	}


	uc = &sc->u.unix_info;
	us = &nss->u.unix_info;

	if(!(uc->data = (char *)kmalloc(PIPE_BUF))) {
		sock_free(nss);
		return -ENOMEM;
	}
	us->data = uc->data;
	/* FNX: the accepted socket carries the LISTENER's bound name, so
	 * getsockname()/getpeername() on the connection return the address
	 * the client connected to (Linux semantics; libxcb checks the peer
	 * name via getpeername after connect). The old code copied the
	 * client's (unbound) sun, yielding an empty peer address. */
	if(ss->u.unix_info.sun) {
		if(!(us->sun = (struct sockaddr_un *)kmalloc(sizeof(struct sockaddr_un)))) {
			sock_free(nss);
			return -ENOMEM;
		}
		memcpy_b(us->sun, ss->u.unix_info.sun, sizeof(struct sockaddr_un));
		us->sun_len = ss->u.unix_info.sun_len;
	}
	us->peer = uc;
	us->count++;
	uc->peer = us;	/* server socket */
	uc->count++;
	sc->state = SS_CONNECTED;
	nss->state = SS_CONNECTED;
	wakeup(sc);
	wakeup(&do_select);
	if(addr) {
		nss->ops->getname(nss, addr, addrlen, SYS_GETPEERNAME);
	}
	return ufd;
}

int unix_getname(struct socket *s, struct sockaddr *addr, unsigned int *addrlen, int call)
{
	struct unix_info *u;
	int len, errno;

	if((errno = check_user_area(VERIFY_WRITE, addrlen, sizeof(int)))) {
		return errno;
	}
	len = *addrlen;

	if(call == SYS_GETSOCKNAME) {
		u = &s->u.unix_info;
	} else {
		/* SYS_GETPEERNAME */
		u = s->u.unix_info.peer;
	}
	if(len > u->sun_len) {
		len = u->sun_len;
	}
	if(len) {
		if((errno = check_user_area(VERIFY_WRITE, addr, len))) {
			return errno;
		}
		memcpy_b(addr, u->sun, len);
	}
	return 0;
}

int unix_socketpair(struct socket *s1, struct socket *s2)
{
	struct unix_info *u1, *u2;

	u1 = &s1->u.unix_info;
	u2 = &s2->u.unix_info;

	if(!(u1->data = (char *)kmalloc(PIPE_BUF))) {
		return -ENOMEM;
	}
	u2->data = u1->data;
	u1->count++;
	u2->count++;
	u1->peer = u2;
	u2->peer = u1;
	s1->state = SS_CONNECTED;
	s2->state = SS_CONNECTED;
	return 0;
}

int unix_send(struct socket *s, struct fd *f, const char *buffer, __size_t count, int flags)
{
	if(flags & ~MSG_DONTWAIT) {
		return -EINVAL;
	}
	return unix_write(s, f, buffer, count);
}

int unix_recv(struct socket *s, struct fd *f, char *buffer, __size_t count, int flags)
{
	if(flags & ~MSG_DONTWAIT) {
		return -EINVAL;
	}
	return unix_read(s, f, buffer, count);
}

int unix_sendto(struct socket *s, struct fd *f, const char *buffer, __size_t count, int flags, const struct sockaddr *addr, int addrlen)
{
	struct inode *i;
	struct unix_info *u;
	struct sockaddr_un *su;
	struct packet *p;
	char *tmp_name;
	int errno;

	su = (struct sockaddr_un *)addr;
	if(su->sun_family != AF_UNIX) {
                return -EINVAL;
	}
	if(addrlen < 0 || addrlen > sizeof(struct sockaddr_un)) {
                return -EINVAL;
	}

	if((errno = malloc_name(su->sun_path, &tmp_name)) < 0) {
		return errno;
	}
	if((errno = namei(tmp_name, &i, NULL, FOLLOW_LINKS))) {
		free_name(tmp_name);
		return errno;
	}
	if(!(u = lookup_unix_socket(tmp_name, i))) {
		iput(i);
		free_name(tmp_name);
		return -ECONNREFUSED;
	}
	iput(i);
	free_name(tmp_name);

	if(!(p = (struct packet *)kmalloc(sizeof(struct packet)))) {
		return -ENOMEM;
	}
	memset_b(p, 0, sizeof(struct packet));
	if(!(p->data = (char *)kmalloc(count + 1))) {
		kfree((addr_t)p);
		return -ENOMEM;
	}
	memset_b(p->data, 0, count + 1);
	memcpy_b(p->data, buffer, count);
	p->len = count;
	p->socket = s;
	lock_resource(&packet_resource);
	append_packet_to_queue(p, &u->packet_queue);
	unlock_resource(&packet_resource);
	wakeup(u);
	return count;
}

int unix_recvfrom(struct socket *s, struct fd *f, char *buffer, __size_t count, int flags, struct sockaddr *addr, int *addrlen)
{
	struct unix_info *u, *up;
	struct sockaddr_un *sun;
	struct packet *p;
	int size;

	u = &s->u.unix_info;

	lock_resource(&packet_resource);
	while(!(p = peek_packet(u->packet_queue))) {
		unlock_resource(&packet_resource);
		if(!(f->flags & O_NONBLOCK)) {
			if(sleep(u, PROC_INTERRUPTIBLE)) {
				return -EINTR;
			}
			lock_resource(&packet_resource);
		} else {
			unlock_resource(&packet_resource);
			return -EAGAIN;
		}
	}

	size = MIN(p->len - p->offset, count);
	memcpy_b(buffer, p->data + p->offset, size);
	p->offset += size;
	/* capture before the packet is freed (p->socket is read below) */
	up = &p->socket->u.unix_info;
	if(!(flags & MSG_PEEK)) {
		p = remove_packet_from_queue(&u->packet_queue);
		kfree((addr_t)p->data);
		kfree((addr_t)p);
	}
	unlock_resource(&packet_resource);

	sun = (struct sockaddr_un *)addr;
	sun->sun_family = AF_UNIX;
	/* the path field is 108 bytes but a malicious peer can bind with a
	 * sun_len up to sizeof(sockaddr_un)=110: copy at most the field
	 * size so we never write/read past sun_path (recvfrom()'s ret_addr
	 * is exactly sizeof(struct sockaddr_un) bytes) */
	memcpy_b(sun->sun_path, up->sun->sun_path, MIN(up->sun_len, sizeof(sun->sun_path)));
	*addrlen = up->sun_len;
	return size;
}

int unix_read(struct socket *s, struct fd *f, char *buffer, __size_t count)
{
	struct unix_info *u;
	int bytes_read;
	int n, limit;

	u = &s->u.unix_info;
	bytes_read = 0;

	while(count) {
		if(u->writeoff) {
			if(u->readoff >= u->writeoff) {
				limit = PIPE_BUF - u->readoff;
			} else {
				limit = u->writeoff - u->readoff;
			}
		} else {
			limit = PIPE_BUF - u->readoff;
		}
		n = MIN(limit, count);
		if(u->size && n) {
			memcpy_b(buffer + bytes_read, u->data + u->readoff, n);
			bytes_read += n;
			u->readoff += n;
			u->size -= n;
			count -= n;
			if(u->writeoff == PIPE_BUF) {
				u->writeoff = 0;
			}
			wakeup(u->peer);
			wakeup(&do_select);
		} else {
			if(s->state != SS_CONNECTED) {
				if(s->state == SS_DISCONNECTING) {
					if(u->size) {
						if(u->readoff == PIPE_BUF) {
							u->readoff = 0;
						}
						continue;
					}
					return bytes_read;
				}
				return -EINVAL;
			}
			if(u->writeoff) {
				break;
			}
			if(f->flags & O_NONBLOCK) {
				return -EAGAIN;
			}
			if(sleep(u, PROC_INTERRUPTIBLE)) {
				return -EINTR;
			}
		}
	}
	if(!u->size) {
		u->readoff = u->writeoff = 0;
	}
	return bytes_read;
}

int unix_write(struct socket *s, struct fd *f, const char *buffer, __size_t count)
{
	struct unix_info *u, *up;
	int bytes_written;
	int n, limit;

	u = &s->u.unix_info;
	up = s->u.unix_info.peer;
	bytes_written = 0;

	while(bytes_written < count) {
		if(s->state != SS_CONNECTED) {
			if(s->state == SS_DISCONNECTING) {
				send_sig(current, SIGPIPE);
				return -EPIPE;
			}
			return -EINVAL;
		}
		if(up->readoff) {
			if(up->writeoff <= up->readoff) {
				limit = up->readoff;
			} else {
				limit = PIPE_BUF;
			}
		} else {
			limit = PIPE_BUF;
		}

		n = MIN((count - bytes_written), (limit - up->writeoff));

		if(n && n <= PIPE_BUF) {
			memcpy_b(up->data + up->writeoff, buffer + bytes_written, n);
			bytes_written += n;
			up->writeoff += n;
			up->size += n;
			if(up->readoff == PIPE_BUF) {
				up->readoff = 0;
			}
			wakeup(u->peer);
			wakeup(&do_select);
			continue;
		}
		wakeup(u->peer);
		wakeup(&do_select);
		if(!(f->flags & O_NONBLOCK)) {
			if(sleep(u, PROC_INTERRUPTIBLE)) {
				return -EINTR;
			}
		} else {
			/* nonblocking: report the partial bytes actually copied
			 * (POSIX); only EAGAIN when nothing fit */
			if(bytes_written) {
				return bytes_written;
			}
			return -EAGAIN;
		}
	}
	return bytes_written;
}

int unix_ioctl(struct socket *s, struct fd *f, int cmd, unsigned int arg)
{
	int errno;

	switch(cmd) {
		default:
			errno = dev_ioctl(cmd, (void *)arg);
			break;
	}
	return errno;
}

int unix_select(struct socket *s, int flag)
{
	struct unix_info *u, *up;

	if(s->flags & SO_ACCEPTCONN) {
		if (flag == SEL_R && s->queue_len) {
			return 1;
		}
		return 0;
	}

	u = &s->u.unix_info;
	up = s->u.unix_info.peer;

	switch(flag) {
		case SEL_R:
			if(u->size) {
				return 1;
			}
			if(s->state != SS_CONNECTED) {
				printk("UNIX: select: socket not connected (read EOF)\n");
				return 1;
			}
			break;
		case SEL_W:
			if(s->state != SS_CONNECTED) {
				printk("UNIX: select: socket not connected (write EOF)\n");
				return 1;
			}
			if(up->size < PIPE_BUF) {
				return 1;
			}
			break;
	}
	return 0;
}

int unix_shutdown(struct socket *s, int how)
{
	return -EOPNOTSUPP;
}

int unix_setsockopt(struct socket *s, int level, int optname, const void *optval, socklen_t optlen)
{
	/* FNX: accept the usual stream-socket options as no-ops so common
	 * clients (libxcb, etc.) don't fail their setup. All AF_UNIX options
	 * are advisory on Linux anyway. */
	switch(level) {
		case SOL_SOCKET:
			switch(optname) {
				case SO_SNDBUF:
				case SO_RCVBUF:
				case SO_SNDLOWAT:
				case SO_RCVLOWAT:
				case SO_SNDTIMEO:
				case SO_RCVTIMEO:
				case SO_REUSEADDR:
				case SO_KEEPALIVE:
				case SO_BROADCAST:
				case SO_OOBINLINE:
				case SO_PRIORITY:
					return 0;
			}
			break;
	}
	return -EOPNOTSUPP;
}

int unix_getsockopt(struct socket *s, int level, int optname, void *optval, socklen_t *optlen)
{
	int errno, val = 0, size = sizeof(int);

	switch(level) {
		case SOL_SOCKET:
			switch(optname) {
				case SO_ERROR:
					val = 0;	/* no pending async errors */
					break;
				case SO_TYPE:
					val = s->type;
					break;
				case SO_SNDBUF:
				case SO_RCVBUF:
					val = PIPE_BUF;
					break;
				default:
					return -EOPNOTSUPP;
			}
			break;
		default:
			return -EOPNOTSUPP;
	}
	if((errno = check_user_area(VERIFY_READ, optlen, sizeof(int)))) {
		return errno;
	}
	if((errno = check_user_area(VERIFY_WRITE, optval, size))) {
		return errno;
	}
	memcpy_b(optval, &val, size);
	memcpy_b(optlen, &size, sizeof(int));
	return 0;
}

int unix_init(void)
{
	unix_socket_head = NULL;
	return 0;
}
#endif /* CONFIG_NET */
