/*
 * fnx/net/ipv4.c
 *
 * Copyright 2025, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 *
 * FNX: IPv4 over the loopback interface (127.0.0.1).
 *
 * SOCK_DGRAM sockets (UDP, and ICMP ping via SOCK_DGRAM|IPPROTO_ICMP -
 * what toybox ping opens) and SOCK_RAW sockets are delivered entirely
 * in the kernel on the loopback address: sendto() copies the datagram
 * into the destination socket's packet queue and wakes it; an ICMP
 * echo request (type 8) is answered with an echo reply (type 0), so
 * `ping 127.0.0.1` works without a NIC. SOCK_STREAM (TCP) is not
 * implemented and returns -EOPNOTSUPP.
 */

#include <fnx/config.h>
#include <fnx/fs.h>
#include <fnx/stat.h>
#include <fnx/errno.h>
#include <fnx/socket.h>
#include <fnx/net.h>
#include <fnx/net/packet.h>
#include <fnx/net/ipv4.h>
#include <fnx/fcntl.h>
#include <fnx/sched.h>
#include <fnx/sleep.h>
#include <fnx/mm.h>
#include <fnx/string.h>
#include <fnx/stdio.h>

#ifdef CONFIG_NET
struct ipv4_info *ipv4_socket_head;

static struct resource packet_resource = { 0, 0 };

static void add_ipv4_socket(struct ipv4_info *ip4)
{
	struct ipv4_info *h;

	if((h = ipv4_socket_head)) {
		while(h->next) {
			h = h->next;
		}
		h->next = ip4;
	} else {
		ipv4_socket_head = ip4;
	}
}

static void remove_ipv4_socket(struct ipv4_info *ip4)
{
	struct ipv4_info *h;

	if(ipv4_socket_head == ip4) {
		ipv4_socket_head = ip4->next;
		return;
	}

	h = ipv4_socket_head;
	while(h && h->next != ip4) {
		h = h->next;
	}
	if(h && h->next == ip4) {
		h->next = ip4->next;
	}
}

/* find a loopback socket bound to the given port with the same protocol */
static struct ipv4_info *find_loopback_socket(__u16 port, int protocol)
{
	struct ipv4_info *ip4;

	ip4 = ipv4_socket_head;
	while(ip4) {
		if(ip4->local_port == port && ip4->protocol == protocol) {
			return ip4;
		}
		ip4 = ip4->next;
	}
	return NULL;
}

/* deliver a datagram to a socket's packet queue */
static int loopback_deliver(struct socket *s, const char *buffer, __size_t count)
{
	struct ipv4_info *ip4;
	struct packet *p;

	ip4 = &s->u.ipv4_info;
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
	append_packet_to_queue(p, &ip4->packet_queue);
	unlock_resource(&packet_resource);
	wakeup(ip4);
	return count;
}

/* internet checksum (ones-complement sum of 16-bit words) */
static __u16 ip_checksum(const unsigned char *data, int len)
{
	__u32 sum = 0;
	int i;

	for(i = 0; i + 1 < len; i += 2) {
		sum += (data[i] << 8) | data[i + 1];
	}
	if(i < len) {
		sum += data[i] << 8;
	}
	while(sum >> 16) {
		sum = (sum & 0xffff) + (sum >> 16);
	}
	return (__u16)~sum;
}

/* answer an ICMP echo request (type 8) with an echo reply (type 0);
 * returns 0 if the packet was an echo request, -1 otherwise */
static int icmp_echo_reply(struct socket *s, const char *buffer, __size_t count)
{
	unsigned char *reply;
	__size_t len;
	int type;

	if(count < 8) {
		return -1;
	}
	type = buffer[0];
	if(type != 8) {		/* only answer echo requests */
		return -1;
	}
	len = count;
	if(!(reply = (unsigned char *)kmalloc(len))) {
		return -ENOMEM;
	}
	/* echo reply: type=0, code=0, checksum recomputed, id+seq+data
	 * copied from the request */
	memcpy_b(reply, buffer, len);
	reply[0] = 0;			/* ICMP echo reply */
	reply[2] = reply[3] = 0;	/* checksum field */
	((__u16 *)(reply + 2))[0] = htons(ip_checksum(reply, len));
	loopback_deliver(s, (char *)reply, len);
	kfree((addr_t)reply);
	return 0;
}

static int ipv4_wait_connected(struct socket *s);

int ipv4_create(struct socket *s, int domain, int type, int protocol)
{
	struct ipv4_info *ip4;

	if(type != SOCK_DGRAM && type != SOCK_RAW && type != SOCK_STREAM) {
		return -EOPNOTSUPP;
	}
	/* loopback only: no NIC fd */
	s->fd_ext = -1;
	ip4 = &s->u.ipv4_info;
	memset_b(ip4, 0, sizeof(struct ipv4_info));
	ip4->count = 1;
	ip4->socket = s;
	ip4->type = type;
	/* protocol 0 means "default for the type" (Linux semantics) */
	if(!protocol && type == SOCK_STREAM) {
		protocol = IPPROTO_TCP;
	}
	if(!protocol && type == SOCK_DGRAM) {
		protocol = IPPROTO_UDP;
	}
	ip4->protocol = protocol;
	add_ipv4_socket(ip4);
	return 0;
}

void ipv4_free(struct socket *s)
{
	struct ipv4_info *ip4, *peer4;
	struct packet *p;

	ip4 = &s->u.ipv4_info;
	if(ip4->type == SOCK_STREAM && ip4->peer) {
		peer4 = &ip4->peer->u.ipv4_info;
		peer4->peer = NULL;
		ip4->peer->state = SS_DISCONNECTING;
		wakeup(peer4);
		wakeup(&do_select);
	}
	remove_ipv4_socket(ip4);
	while((p = remove_packet_from_queue(&ip4->packet_queue))) {
		kfree((addr_t)p->data);
		kfree((addr_t)p);
	}
	s->fd_ext = 0;
}

int ipv4_bind(struct socket *s, const struct sockaddr *addr, int addrlen)
{
	struct sockaddr_in *sin;
	struct ipv4_info *ip4;


	if(addrlen < (int)sizeof(struct sockaddr_in)) {
		return -EINVAL;
	}
	sin = (struct sockaddr_in *)addr;
	if(sin->sin_family != AF_INET) {
		return -EINVAL;
	}
	/* only loopback (or INADDR_ANY) is bindable */
	if(ntohl(sin->sin_addr) != INADDR_LOOPBACK && ntohl(sin->sin_addr) != INADDR_ANY) {
		return -EADDRNOTAVAIL;
	}
	ip4 = &s->u.ipv4_info;
	ip4->local_port = ntohs(sin->sin_port);
	ip4->local_addr = ntohl(sin->sin_addr);
	return 0;
}

/* ephemeral port allocator for loopback clients */
static __u16 ipv4_ephemeral_port(void)
{
	static __u16 next = 49152;

	if(++next == 65535) {
		next = 49152;
	}
	return next;
}

int ipv4_listen(struct socket *s, int backlog)
{
	struct ipv4_info *ip4;

	if(s->type != SOCK_STREAM) {
		return -EOPNOTSUPP;
	}
	ip4 = &s->u.ipv4_info;
	if(!ip4->local_port) {
		return -EADDRNOTAVAIL;	/* must bind() first */
	}
	s->flags |= SO_ACCEPTCONN;
	s->queue_limit = backlog > 0 ? backlog : 1;
	s->state = SS_UNCONNECTED;
	return 0;
}

int ipv4_connect(struct socket *s, const struct sockaddr *addr, int addrlen)
{
	struct sockaddr_in *sin;
	struct ipv4_info *ip4, *dest;
	__u16 dport;
	int errno;

	if(s->type != SOCK_STREAM) {
		return -EOPNOTSUPP;
	}
	if(addrlen < (int)sizeof(struct sockaddr_in)) {
		return -EINVAL;
	}
	sin = (struct sockaddr_in *)addr;
	if(sin->sin_family != AF_INET) {
		return -EINVAL;
	}
	if(ntohl(sin->sin_addr) != INADDR_LOOPBACK && ntohl(sin->sin_addr) != INADDR_ANY) {
		return -ENETUNREACH;	/* no NIC: only loopback is reachable */
	}
	ip4 = &s->u.ipv4_info;

	dport = ntohs(sin->sin_port);
	if(!(dest = find_loopback_socket(dport, IPPROTO_TCP)) || !(dest->socket->flags & SO_ACCEPTCONN)) {
		return -ECONNREFUSED;
	}

	/* assign an ephemeral local port */
	ip4->local_port = ipv4_ephemeral_port();
	ip4->local_addr = INADDR_LOOPBACK;

	s->state = SS_CONNECTING;
	if((errno = insert_socket_to_queue(dest->socket, s))) {
		s->state = SS_UNCONNECTED;
		return errno;
	}
	/* loopback connect completes when the listener accepts; the
	 * connection is already in the listener's backlog, so return
	 * immediately (a blocking connect would deadlock a single
	 * threaded client that accepts from the same process). */
	wakeup(dest->socket);
	wakeup(&do_select);
	return 0;
}

int ipv4_accept(struct socket *s, struct sockaddr *addr, unsigned int *addrlen)
{
	int ufd;
	struct socket *sc, *nss;
	struct ipv4_info *ip4, *sc4, *ns4;
	int errno;

	while(!(sc = get_socket_from_queue(s))) {
		if(s->fd->flags & O_NONBLOCK) {
			return -EAGAIN;
		}
		if(sleep(s, PROC_INTERRUPTIBLE)) {
			return -EINTR;
		}
	}

	nss = NULL;
	if((ufd = sock_alloc(&nss)) < 0) {
		return ufd;
	}
	nss->type = s->type;
	nss->ops = s->ops;
	if((errno = nss->ops->create(nss, AF_INET, s->type, 0)) < 0) {
		sock_free(nss);
		return errno;
	}

	ip4 = &s->u.ipv4_info;
	sc4 = &sc->u.ipv4_info;
	ns4 = &nss->u.ipv4_info;

	ns4->local_port = ip4->local_port;	/* server side */
	ns4->local_addr = INADDR_LOOPBACK;
	sc4->peer = nss;			/* client <-> server link */
	ns4->peer = sc;
	sc->state = SS_CONNECTED;
	nss->state = SS_CONNECTED;
	wakeup(sc);
	wakeup(&do_select);
	if(addr) {
		nss->ops->getname(nss, addr, addrlen, SYS_GETPEERNAME);
	}
	return ufd;
}

int ipv4_getname(struct socket *s, struct sockaddr *addr, unsigned int *addrlen, int call)
{
	struct sockaddr_in *sin;
	struct ipv4_info *ip4;

	ip4 = &s->u.ipv4_info;
	sin = (struct sockaddr_in *)addr;
	memset_b(sin, 0, sizeof(struct sockaddr_in));
	sin->sin_family = AF_INET;
	sin->sin_port = htons(ip4->local_port);
	sin->sin_addr = htonl(ip4->local_addr);
	*addrlen = sizeof(struct sockaddr_in);
	return 0;
}

int ipv4_socketpair(struct socket *s1, struct socket *s2)
{
	return -EOPNOTSUPP;
}

int ipv4_send(struct socket *s, struct fd *f, const char *buffer, __size_t count, int flags)
{
	if(flags & ~(MSG_DONTWAIT | MSG_NOSIGNAL)) {
		return -EINVAL;
	}
	return ipv4_write(s, f, buffer, count);
}

int ipv4_recv(struct socket *s, struct fd *f, char *buffer, __size_t count, int flags)
{
	if(flags & ~MSG_DONTWAIT) {
		return -EINVAL;
	}
	return ipv4_read(s, f, buffer, count);
}

int ipv4_sendto(struct socket *s, struct fd *f, const char *buffer, __size_t count, int flags, const struct sockaddr *addr, int addrlen)
{
	struct sockaddr_in *sin;
	struct ipv4_info *ip4, *dest;
	__u16 dport;
	int errno;

	if(flags & ~(MSG_DONTWAIT | MSG_NOSIGNAL)) {
		return -EINVAL;
	}
	if(addrlen < (int)sizeof(struct sockaddr_in)) {
		return -EINVAL;
	}
	sin = (struct sockaddr_in *)addr;
	if(sin->sin_family != AF_INET) {
		return -EINVAL;
	}
	if(ntohl(sin->sin_addr) != INADDR_LOOPBACK && ntohl(sin->sin_addr) != INADDR_ANY) {
		return -ENETUNREACH;	/* no NIC: only loopback is reachable */
	}
	ip4 = &s->u.ipv4_info;

	/* SOCK_STREAM (loopback TCP): deliver to the connected peer */
	if(ip4->type == SOCK_STREAM) {
		if((errno = ipv4_wait_connected(s)) < 0) {
			return errno;
		}
		return loopback_deliver(ip4->peer, buffer, count);
	}

	/* ICMP ping sockets (SOCK_DGRAM|IPPROTO_ICMP, SOCK_RAW): the
	 * kernel answers echo requests itself on loopback */
	if(ip4->protocol == IPPROTO_ICMP || ip4->type == SOCK_RAW) {
		if(icmp_echo_reply(s, buffer, count) == 0) {
			return count;
		}
		return -EINVAL;
	}

	/* UDP: deliver to the socket bound to the destination port */
	dport = ntohs(sin->sin_port);
	if(!(dest = find_loopback_socket(dport, ip4->protocol))) {
		return -ECONNREFUSED;
	}
	return loopback_deliver(dest->socket, buffer, count);
}

int ipv4_recvfrom(struct socket *s, struct fd *f, char *buffer, __size_t count, int flags, struct sockaddr *addr, int *addrlen)
{
	struct ipv4_info *ip4;
	struct packet *p;
	int size;

	ip4 = &s->u.ipv4_info;

	lock_resource(&packet_resource);
	while(!(p = peek_packet(ip4->packet_queue))) {
		unlock_resource(&packet_resource);
		if(!(f->flags & O_NONBLOCK)) {
			if(sleep(ip4, PROC_INTERRUPTIBLE)) {
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
	if(!(flags & MSG_PEEK)) {
		p = remove_packet_from_queue(&ip4->packet_queue);
		kfree((addr_t)p->data);
		kfree((addr_t)p);
	}
	unlock_resource(&packet_resource);

	if(addr && addrlen) {
		struct sockaddr_in *sin = (struct sockaddr_in *)addr;

		memset_b(sin, 0, sizeof(struct sockaddr_in));
		sin->sin_family = AF_INET;
		sin->sin_port = htons(ip4->local_port);
		sin->sin_addr = htonl(INADDR_LOOPBACK);
		*addrlen = sizeof(struct sockaddr_in);
	}
	return size;
}

int ipv4_read(struct socket *s, struct fd *f, char *buffer, __size_t count)
{
	return ipv4_recvfrom(s, f, buffer, count, 0, NULL, NULL);
}

static int ipv4_wait_connected(struct socket *s)
{
	struct ipv4_info *ip4;

	ip4 = &s->u.ipv4_info;
	/* loopback connect() returns once the connection is queued in the
	 * listener's backlog; the peer is linked when the listener calls
	 * accept(). Until then the client is SS_CONNECTING - wait (data
	 * sent before the server accepts must not be dropped). */
	while(s->state == SS_CONNECTING && !ip4->peer) {
		if(s->fd->flags & O_NONBLOCK) {
			return -EAGAIN;
		}
		if(sleep(s, PROC_INTERRUPTIBLE)) {
			return -EINTR;
		}
	}
	if(!ip4->peer || s->state != SS_CONNECTED) {
		return -ENOTCONN;
	}
	return 0;
}

int ipv4_write(struct socket *s, struct fd *f, const char *buffer, __size_t count)
{
	struct ipv4_info *ip4;
	int errno;

	ip4 = &s->u.ipv4_info;
	if(ip4->type == SOCK_STREAM) {
		if((errno = ipv4_wait_connected(s)) < 0) {
			return errno;
		}
		return loopback_deliver(ip4->peer, buffer, count);
	}
	/* unconnected datagram socket: nothing to send without a dest */
	return -ENOTCONN;
}

int ipv4_ioctl(struct socket *s, struct fd *f, int cmd, unsigned int arg)
{
	return dev_ioctl(cmd, (void *)arg);
}

int ipv4_select(struct socket *s, int flag)
{
	struct ipv4_info *ip4;

	ip4 = &s->u.ipv4_info;
	if(flag == SEL_R) {
		return (ip4->packet_queue != NULL) ? 1 : 0;
	}
	return 0;
}

int ipv4_shutdown(struct socket *s, int how)
{
	wakeup(&s->u.ipv4_info);
	return 0;
}

int ipv4_setsockopt(struct socket *s, int level, int optname, const void *optval, socklen_t optlen)
{
	/* accept-and-ignore: loopback has no tunable IP options */
	return 0;
}

int ipv4_getsockopt(struct socket *s, int level, int optname, void *optval, socklen_t *optlen)
{
	return -EOPNOTSUPP;
}

int ipv4_init(void)
{
	ipv4_socket_head = NULL;
	return 0;
}
#endif /* CONFIG_NET */
