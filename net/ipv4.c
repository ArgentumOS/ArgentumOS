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
	/* A SELECT/POLL WAITER SLEEPS ON ITS OWN CHANNEL, SO DATA ARRIVAL HAS TO WAKE IT TOO.
	 *
	 * `wakeup(ip4)` above wakes the BLOCKING READER, which sleeps on this socket; a select(2)/poll(2) waiter
	 * sleeps on &do_select, and every OTHER readiness producer in this tree wakes both - every STATE CHANGE
	 * (ipv4_free, ipv4_connect, ipv4_accept, and their AF_UNIX twins) and every DRAIN (a reader taking a packet
	 * out of the queue). The data path had only the first, so it was the one producer that could leave a waiter
	 * asleep. The rule is uniform now.
	 *
	 * WHAT THIS WAKE IS *NOT*: the cure for §58.1's parked TLS handshake. Measured, this is not what held that
	 * handshake - a peer in ANOTHER PROCESS already supplies this wake, because &do_select is a global channel
	 * (`kernel/sleep.c`'s wakeup() walks one sleep_hash_table, so a wake crosses processes, threads and address
	 * spaces alike) - and the window that did is described in foundation-plan.md §58.1: sys_poll CHECKS
	 * readiness and THEN sleeps, so a wake landing between the two finds nobody and is lost. This line is right
	 * on its own terms and stays; it is simply not the answer to that stall. */
	wakeup(ip4);
	wakeup(&do_select);
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
	/* external-capable when a NIC is present (loopback sockets are
	 * switched back to -1 by ipv4_bind/ipv4_connect) */
	extern int ext_net_present(void);
	if(ext_net_present()) {
		s->fd_ext = 0;
	} else {
		s->fd_ext = -1;	/* loopback only */
	}
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
	struct socket *sc;

	ip4 = &s->u.ipv4_info;

	/* FNX: LEAVE EVERY QUEUE YOU ARE IN, AND EMPTY THE ONE YOU OWN. A socket's storage IS its sockfs
	 * inode, so a stale queue pointer is a pointer into FREED MEMORY — and BOTH directions of that
	 * were missing here:
	 *
	 *   * a client that CONNECTS and CLOSES BEFORE THE SERVER ACCEPTS stayed in the listener's
	 *     `queue_head` chain, and the next `connect()`'s walk of that chain dereferenced the freed
	 *     inode: a #GP inside insert_socket_to_queue. Measured, and the reproducer is
	 *     tests/cases/libressl_l2.py — curl aborts the untrusted-cert fetch and s_server never gets to
	 *     accept the connection it opened.
	 *   * a LISTENER that closed left its queued sockets holding `pending_in` back at it, with their
	 *     `connect()` parked in ipv4_wait_connected() waiting for an accept that could not happen.
	 *
	 * The drain DISCONNECTS what it releases and wakes it: a connector parked in
	 * ipv4_wait_connected() re-checks `s->state` when it wakes, so it now answers ENOTCONN instead of
	 * waiting for a listener that is gone. (ipv4_accept() wakes its client the same way.) */
	if(s->pending_in) {
		remove_socket_from_queue(s->pending_in, s);
		s->pending_in = NULL;
	}
	while((sc = get_socket_from_queue(s))) {
		sc->pending_in = NULL;
		sc->state = SS_DISCONNECTING;
		wakeup(sc);
	}
	wakeup(&do_select);

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
	if(ntohl(sin->sin_addr) == INADDR_LOOPBACK) {
		s->fd_ext = -1;	/* loopback-bound: internal delivery */
	}
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
	s->fd_ext = -1;	/* loopback connection: internal delivery */

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
	ip4 = &s->u.ipv4_info;
	if(ntohl(sin->sin_addr) != INADDR_LOOPBACK && ntohl(sin->sin_addr) != INADDR_ANY) {
		/* external destination: send via the real NIC */
		extern int ext_net_send_ip(unsigned int, int, const void *, __size_t);
		extern int ext_net_present(void);
		if(!ext_net_present()) {
			return -ENETUNREACH;	/* no NIC */
		}
		/* Linux computes the ICMP checksum in-kernel for ping sockets
		 * (SOCK_DGRAM|IPPROTO_ICMP): the socket layer hands the user
		 * buffer straight through here (no kernel copy), so recompute
		 * into a scratch buffer. toybox 0.8.11's pingchksum() is broken
		 * (no one's complement + a spurious end-around carry), and
		 * without this its echo requests leave the NIC with a bad
		 * checksum that every peer drops. SOCK_RAW senders own their
		 * checksum and are left untouched, as on Linux. */
		if(ip4->type == SOCK_DGRAM && ip4->protocol == IPPROTO_ICMP && count >= 8) {
			unsigned char *tmp;

			if(!(tmp = (unsigned char *)kmalloc(count))) {
				return -ENOMEM;
			}
			memcpy_b(tmp, buffer, count);
			tmp[2] = tmp[3] = 0;	/* checksum field */
			((__u16 *)(tmp + 2))[0] = htons(ip_checksum(tmp, count));
			errno = ext_net_send_ip(sin->sin_addr, ip4->protocol, tmp, count);
			kfree((addr_t)tmp);
			return (errno < 0) ? errno : (int)count;
		}
		return ext_net_send_ip(sin->sin_addr, ip4->protocol, buffer, count);
	}

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

	if(s->fd_ext != -1 && ip4->local_addr != INADDR_LOOPBACK &&
	   !peek_packet(ip4->packet_queue)) {
		/* external socket with no loopback data waiting: receive an IP
		 * datagram from the NIC. The packet_queue check first lets a
		 * socket that can address both (unbound ping) drain loopback
		 * replies before polling the NIC. */
		extern int ext_net_recv_ip(unsigned int, int, void *, __size_t, unsigned int *);
		extern int ext_net_present(void);
		struct sockaddr_in *rsin = (struct sockaddr_in *)addr;
		unsigned int from = 0;
		int n;

		if(!ext_net_present()) {
			return -ENODEV;
		}
		n = ext_net_recv_ip(0, ip4->protocol, buffer, count, &from);
		if(n < 0) {
			return n;
		}
		if(rsin && addrlen) {
			rsin->sin_family = AF_INET;
			rsin->sin_port = 0;
			rsin->sin_addr = from;
			*addrlen = sizeof(struct sockaddr_in);
		}
		return n;
	}

	lock_resource(&packet_resource);
	while(!(p = peek_packet(ip4->packet_queue))) {
		/* EOF IS NOT AN ABSENCE OF DATA, IT IS A CONDITION, AND THIS LOOP HAD ONLY THE FORMER.
		 * A peer that closes stamps this socket SS_DISCONNECTING and wakes it, and the wake-up
		 * lands HERE — where the sole question was whether a packet had arrived, so a read after
		 * the peer's close slept for ever instead of returning 0. A FIN that is never observed is
		 * indistinguishable from a slow server, which is how it stayed hidden. Checked BEFORE the
		 * O_NONBLOCK branch, because EOF is not EAGAIN: a non-blocking reader must see it too. */
		if(s->state == SS_DISCONNECTING) {
			unlock_resource(&packet_resource);
			return 0;
		}
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
	/*
	 * FNX: THE PACKET IS ONLY REMOVED ONCE IT HAS BEEN READ TO ITS END. `p->offset` above is the
	 * partial-read bookkeeping — it was already being advanced — but the packet was then dequeued and
	 * freed REGARDLESS, so a caller whose buffer was smaller than the queued packet had the remainder
	 * of that packet DESTROYED. `read(5)` on a 307-byte write returned 5 bytes and threw away 302.
	 *
	 * THAT IS A STREAM-SOCKET BUG WITH A LONG REACH, and it is what stalled L1's TLS handshake: TLS
	 * reads its 5-byte record header first, so the ClientHello arrived as 5 bytes and the rest was
	 * gone — the peer waited for bytes that no longer existed and the client waited for a reply that
	 * was never coming. Measured by userland/tests/libressl_tls_pair.c, whose byte-flow log shows it
	 * exactly (`client: WRITE took 307`, `peer: READ got 5`, `peer: READ wants 302` … and nothing).
	 *
	 * A partially-read packet now STAYS at the head of the queue with its offset advanced, so the next
	 * read continues where this one stopped — which is what a stream socket owes its reader. A packet
	 * read to its end is removed here, in the same call, so the next read never sees a zero-length
	 * remainder and mistakes it for EOF.
	 */
	if(!(flags & MSG_PEEK) && p->offset >= p->len) {
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

int ipv4_ioctl(struct socket *s, struct fd *f, int cmd, addr_t arg)
{
	return dev_ioctl(cmd, (void *)arg);
}

int ipv4_select(struct socket *s, int flag)
{
	struct ipv4_info *ip4;

	ip4 = &s->u.ipv4_info;
	if(flag == SEL_R) {
		if(ip4->packet_queue) {
			return 1;	/* loopback data waiting */
		}
		/* AN EOF IS A READABLE CONDITION, and leaving it out is the SAME DEFECT as leaving out
		 * writability below: a program that multiplexes before it reads waits for ever. A peer's
		 * close stamps its partner SS_DISCONNECTING and wakes it (ipv4_free), and the woken poll
		 * asked only whether a packet had arrived — so it went back to sleep. MEASURED: an https
		 * fetch read the response, then hung until it was killed, because `HTTP/1.0` told it to
		 * expect a close and the close was never reported (libressl_l2). */
		if(s->state == SS_DISCONNECTING) {
			return 1;
		}
		if(s->fd_ext != -1 && ip4->local_addr != INADDR_LOOPBACK) {
			/* external socket: the data comes from the NIC's receive
			 * queue, not the loopback packet_queue */
			extern int ext_poll(int, int);
			return ext_poll(s->fd_ext, SEL_R);
		}
		return 0;
	}
	/*
	 * WRITABILITY, WHICH THIS FUNCTION NEVER REPORTED — and its absence was a real defect with a
	 * long reach, not an omission in a corner.
	 *
	 * select(2)/poll(2) answered "not writable" for EVERY IPv4 socket, forever, so any program that
	 * multiplexes and waits for writability before sending waited for ever. That is exactly what
	 * LibreSSL's s_client and s_server do: their TLS handshake stalled with an empty -state trace
	 * and no bytes moved, while the same exchange over BLOCKING sockets worked. Measured, with a
	 * reproducer that has no SSL in it at all: userland/tests/kernel_loopback_tcp.c.
	 *
	 * THE CONDITION IS THE WRITE PATH'S OWN TEST, which is the contract select must keep: a write is
	 * possible exactly when it would not block. For a loopback stream socket that is what
	 * ipv4_wait_connected() already decides — the peer is linked and the connection is up. There is
	 * NO send buffer to fill (loopback_deliver() appends to the peer's queue synchronously), so those
	 * two facts are the whole condition.
	 *
	 * AND WHILE THE CLIENT IS STILL SS_CONNECTING IT IS CORRECTLY NOT WRITABLE: loopback connect()
	 * returns once the connection is queued in the listener's backlog and the peer is linked only
	 * when the listener ACCEPTS — which is why a non-blocking write there answers EAGAIN. ipv4_accept
	 * sets SS_CONNECTED and wakes the waiters, so select is told the moment that becomes false.
	 */
	if(flag == SEL_W) {
		if(s->fd_ext != -1 && ip4->local_addr != INADDR_LOOPBACK) {
			extern int ext_poll(int, int);
			return ext_poll(s->fd_ext, SEL_W);
		}
		return (ip4->peer != NULL && s->state == SS_CONNECTED) ? 1 : 0;
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
	int errno, val = 0, size = sizeof(int);

	switch(level) {
		case SOL_SOCKET:
			switch(optname) {
				case SO_ERROR:
					/* NO PENDING ASYNC ERROR, and returning that is the whole point of
					 * implementing this. Every error this stack produces is returned
					 * SYNCHRONOUSLY by the call that caused it (a refused connect() hands back
					 * ECONNREFUSED and does not queue anything), so there is never a deferred
					 * error for SO_ERROR to report — 0 here is the truth, not a placeholder.
					 *
					 * AND IT IS NOT AN OBSCURE OPTION: it is how curl decides whether a
					 * non-blocking connect() SUCCEEDED. lib/cf-socket.c:921 does
					 *     if(getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &sockerr, &errSize))
					 *             sockerr = SOCKERRNO;
					 * and then treats anything that is not 0/EISCONN as "This was not a
					 * successful connect" — so the -EOPNOTSUPP this function used to return for
					 * EVERY option became sockerr = 95, a connect curl believed had failed, and
					 * an https fetch that retried until it timed out. Measured: an https fetch
					 * against a local s_server never connected, while `openssl s_client` against
					 * the same server connected, handshook and verified the certificate — the
					 * only thing curl asked for that s_client does not is this.
					 * (unix_getsockopt() has answered SO_ERROR all along; the IPv4 one was the
					 * stub, and AF_INET is the socket a TLS fetch uses.) */
					val = 0;
					break;
				case SO_TYPE:
					val = s->type;
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

int ipv4_init(void)
{
	ipv4_socket_head = NULL;
	return 0;
}
#endif /* CONFIG_NET */
