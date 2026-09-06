/*
 * fnx/net/af_packet.c
 *
 * AF_PACKET (packet socket) domain: a userland-facing mirror of the
 * real NIC (the virtio-net driver behind net/ext_net.c). SOCK_DGRAM
 * sockets exchange raw Ethernet frames: the kernel prepends/strips the
 * 14-byte Ethernet header, with the destination/source MAC and protocol
 * carried by struct sockaddr_ll. This is what toybox's dhcp client uses
 * for its DISCOVER/OFFER/REQUEST/ACK exchange.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#include <fnx/config.h>
#include <fnx/errno.h>
#include <fnx/net.h>
#include <fnx/net/af_packet.h>
#include <fnx/netdev.h>
#include <fnx/socket.h>
#include <fnx/string.h>
#include <fnx/mm.h>
#include <fnx/sleep.h>
#include <fnx/sched.h>

#ifdef CONFIG_NET

#define ETH_ALEN	6
#define ETH_P_IP	0x0800

struct eth_hdr {
	unsigned char dst[ETH_ALEN];
	unsigned char src[ETH_ALEN];
	unsigned short proto;	/* network byte order */
};

static int packet_create(struct socket *s, int domain, int type, int protocol)
{
	if(domain != AF_PACKET || type != SOCK_DGRAM) {
		return -EPROTONOSUPPORT;
	}
	memset_b(&s->u.packet_info, 0, sizeof(s->u.packet_info));
	s->u.packet_info.protocol = protocol;
	s->fd_ext = 0;	/* external: data comes from the NIC */
	return 0;
}

static void packet_free(struct socket *s)
{
	wakeup(&s->u.packet_info);
}

static int packet_bind(struct socket *s, const struct sockaddr *addr, int addrlen)
{
	struct sockaddr_ll *sll;

	if(addrlen < (int)sizeof(struct sockaddr_ll)) {
		return -EINVAL;
	}
	sll = (struct sockaddr_ll *)addr;
	if(sll->sll_family != AF_PACKET) {
		return -EINVAL;
	}
	s->u.packet_info.protocol = sll->sll_protocol;
	s->u.packet_info.ifindex = sll->sll_ifindex;
	s->u.packet_info.halen = sll->sll_halen;
	memcpy_b(s->u.packet_info.addr, sll->sll_addr, sizeof(sll->sll_addr));
	return 0;
}

static int packet_getname(struct socket *s, struct sockaddr *addr, unsigned int *addrlen, int peer)
{
	struct sockaddr_ll *sll;
	extern int check_user_area(int, const void *, unsigned int);

	if(!addr || !addrlen) {
		return -EINVAL;
	}
	if(check_user_area(VERIFY_WRITE, addr, sizeof(struct sockaddr_ll))) {
		return -EFAULT;
	}
	sll = (struct sockaddr_ll *)addr;
	memset_b(sll, 0, sizeof(*sll));
	sll->sll_family = AF_PACKET;
	sll->sll_protocol = s->u.packet_info.protocol;
	sll->sll_ifindex = s->u.packet_info.ifindex;
	sll->sll_halen = s->u.packet_info.halen;
	memcpy_b(sll->sll_addr, s->u.packet_info.addr, sizeof(sll->sll_addr));
	*addrlen = sizeof(*sll);
	return 0;
}

/* wrap the user's payload in an Ethernet frame (SOCK_DGRAM semantics:
 * the sockaddr_ll carries the destination MAC + protocol) and send it
 * on the NIC */
static int packet_sendto(struct socket *s, struct fd *f, const char *buffer, __size_t count, int flags, const struct sockaddr *addr, int addrlen)
{
	extern int ext_sendto(int, const void *, __size_t, const struct sockaddr *, int);
	extern void ext_net_get_mac(unsigned char *);
	struct sockaddr_ll *sll;
	struct eth_hdr *eth;
	unsigned char *frame;
	int errno;

	sll = (struct sockaddr_ll *)addr;
	if(addrlen < (int)sizeof(struct sockaddr_ll)) {
		return -EINVAL;
	}
	if(sll->sll_family != AF_PACKET || sll->sll_halen != ETH_ALEN) {
		return -EINVAL;
	}
	if(!(frame = (unsigned char *)kmalloc(14 + count))) {
		return -ENOMEM;
	}
	eth = (struct eth_hdr *)frame;
	memcpy_b(eth->dst, sll->sll_addr, ETH_ALEN);
	ext_net_get_mac(eth->src);
	eth->proto = sll->sll_protocol ? sll->sll_protocol : (unsigned short)s->u.packet_info.protocol;
	memcpy_b(frame + 14, buffer, count);
	errno = ext_sendto(0, frame, 14 + count, NULL, 0);
	kfree((addr_t)frame);
	return (errno < 0) ? errno : (int)count;
}

/* receive one Ethernet frame from the NIC, strip its header and return
 * the payload; the source MAC + protocol come back in sockaddr_ll */
static int packet_recvfrom(struct socket *s, struct fd *f, char *buffer, __size_t count, int flags, struct sockaddr *addr, int *addrlen)
{
	extern int ext_recvfrom(int, void *, __size_t, struct sockaddr *, int *);
	struct eth_hdr *eth;
	struct sockaddr_ll *sll;
	unsigned char *frame;
	int n;

	for(;;) {
	if(!(frame = (unsigned char *)kmalloc(14 + count))) {
		return -ENOMEM;
	}
	n = ext_recvfrom(0, frame, 14 + count, NULL, NULL);
	if(n < 0) {
		kfree((addr_t)frame);
		return n;
	}
	if(n < 14) {
		kfree((addr_t)frame);
		continue;
	}
	eth = (struct eth_hdr *)frame;
	/* Linux semantics: the socket's protocol filters what it receives;
	 * frames for other protocols (e.g. ARP) are consumed and skipped,
	 * matching ext_net_recv_ip. protocol 0 = receive everything. */
	if(s->u.packet_info.protocol && s->u.packet_info.protocol != eth->proto) {
		kfree((addr_t)frame);
		continue;
	}
	if(addr && addrlen) {
		sll = (struct sockaddr_ll *)addr;
		memset_b(sll, 0, sizeof(*sll));
		sll->sll_family = AF_PACKET;
		sll->sll_protocol = eth->proto;
		sll->sll_ifindex = s->u.packet_info.ifindex ? s->u.packet_info.ifindex : 1;
		sll->sll_halen = ETH_ALEN;
		memcpy_b(sll->sll_addr, eth->src, ETH_ALEN);
		*addrlen = sizeof(*sll);
	}
	memcpy_b(buffer, frame + 14, n - 14);
	kfree((addr_t)frame);
	return n - 14;
	}
}

static int packet_read(struct socket *s, struct fd *f, char *buffer, __size_t count)
{
	return packet_recvfrom(s, f, buffer, count, 0, NULL, NULL);
}

static int packet_write(struct socket *s, struct fd *f, const char *buffer, __size_t count)
{
	struct sockaddr_ll sll;

	/* a write() needs a prior bind() to know the destination */
	if(!s->u.packet_info.halen) {
		return -EINVAL;
	}
	memset_b(&sll, 0, sizeof(sll));
	sll.sll_family = AF_PACKET;
	sll.sll_protocol = s->u.packet_info.protocol;
	sll.sll_ifindex = s->u.packet_info.ifindex;
	sll.sll_halen = s->u.packet_info.halen;
	memcpy_b(sll.sll_addr, s->u.packet_info.addr, sizeof(sll.sll_addr));
	return packet_sendto(s, f, buffer, count, 0, (struct sockaddr *)&sll, sizeof(sll));
}

static int packet_ioctl(struct socket *s, struct fd *f, int cmd, addr_t arg)
{
	extern int dev_ioctl(int, void *);
	return dev_ioctl(cmd, (void *)arg);
}

static int packet_select(struct socket *s, int flag)
{
	extern int ext_poll(int, int);

	if(flag == SEL_R) {
		return ext_poll(s->fd_ext, SEL_R);
	}
	return 1;
}

static int packet_shutdown(struct socket *s, int how)
{
	wakeup(&s->u.packet_info);
	return 0;
}

static int packet_setsockopt(struct socket *s, int level, int optname, const void *optval, socklen_t optlen)
{
	/* SO_ATTACH_FILTER and friends: accept-and-ignore (the client only
	 * needs the call to succeed) */
	return 0;
}

static int packet_getsockopt(struct socket *s, int level, int optname, void *optval, socklen_t *optlen)
{
	return 0;
}

int packet_init(void)
{
	return 0;
}

/* proto_ops.send/recv carry a flags argument (5 args) while this socket
 * type's plain read/write do not (4 args); clang rejects aliasing one
 * function to both shapes (gcc only warned), so send/recv are thin
 * adapters that drop the flags. */
static int packet_send(struct socket *s, struct fd *f, const char *buf, __size_t len, int flags)
{
	return packet_write(s, f, buf, len);
}
static int packet_recv(struct socket *s, struct fd *f, char *buf, __size_t len, int flags)
{
	return packet_read(s, f, buf, len);
}

struct proto_ops packet_ops = {
	packet_create,
	packet_free,
	packet_bind,
	NULL,			/* listen */
	NULL,			/* connect */
	NULL,			/* accept */
	packet_getname,
	NULL,			/* socketpair */
	packet_send,		/* send */
	packet_recv,		/* recv */
	packet_sendto,
	packet_recvfrom,
	packet_read,
	packet_write,
	packet_ioctl,
	packet_select,
	packet_shutdown,
	packet_setsockopt,
	packet_getsockopt,
	packet_init,
};

#endif /* CONFIG_NET */
