/*
 * fnx/net/ext_net.c
 *
 * FNX external (real NIC) IP path: ARP resolution + Ethernet framing
 * over the ext_* API (drivers/net/virtio_net.c). ipv4.c calls
 * ext_net_send_ip()/ext_net_recv_ip() for destinations that are not
 * loopback; everything here is minimal but functional: a one-entry ARP
 * cache, ARP request/reply, and IPv4 frames to the configured gateway.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#include <fnx/config.h>
#include <fnx/types.h>
#include <fnx/errno.h>
#include <fnx/string.h>
#include <fnx/stdio.h>
#include <fnx/mm.h>
#include <fnx/net.h>
#include <fnx/socket.h>
#include <fnx/sched.h>

#ifdef CONFIG_NET

/* Ethernet frame header (14 bytes) */
struct eth_hdr {
	unsigned char dst[6];
	unsigned char src[6];
	unsigned short proto;		/* network order */
};

#define ETH_P_IP	0x0800
#define ETH_P_ARP	0x0806

/* ARP packet (28 bytes, Ethernet/IPv4) - must be tightly packed: the
 * u32 spa/tpa fields would otherwise get alignment padding after the
 * u8 sha[6], shifting every following field and corrupting the frame
 * (SLIRP saw a garbage sender IP and never answered). */
struct arp_pkt {
	unsigned short htype;		/* 1 = Ethernet */
	unsigned short ptype;		/* 0x0800 */
	unsigned char hlen;		/* 6 */
	unsigned char plen;		/* 4 */
	unsigned short op;		/* 1 = request, 2 = reply */
	unsigned char sha[6];
	unsigned int spa;		/* sender IP (network order) */
	unsigned char tha[6];
	unsigned int tpa;		/* target IP (network order) */
} __attribute__((packed));

/* IPv4 header (20 bytes) */
struct ip_hdr {
	unsigned char ver_ihl;
	unsigned char tos;
	unsigned short tot_len;
	unsigned short id;
	unsigned short frag_off;
	unsigned char ttl;
	unsigned char proto;
	unsigned short csum;
	unsigned int saddr;
	unsigned int daddr;
};

#define ARPOP_REQUEST	1
#define ARPOP_REPLY	2

extern int ext_open(int, int, int);
extern int ext_sendto(int, const void *, __size_t, const struct sockaddr *, int);
extern int ext_recvfrom(int, void *, __size_t, struct sockaddr *, int *);
extern int ext_poll(int, int);

static int ext_fd = -1;
static unsigned char ext_mac[6];
static unsigned int ext_ip;			/* our IP (network order) */
static unsigned int gateway_ip;			/* gateway (network order) */

/* ARP cache (single entry) */
static unsigned char arp_mac[6];
static unsigned int arp_ip;
static int arp_valid;

static unsigned int ip_csum(const void *buf, int len)
{
	const unsigned short *p = (const unsigned short *)buf;
	unsigned int sum = 0;
	int n = len / 2;

	while(n--) {
		sum += *p++;
	}
	if(len & 1) {
		sum += *(const unsigned char *)p;
	}
	while(sum >> 16) {
		sum = (sum & 0xFFFF) + (sum >> 16);
	}
	return ~sum & 0xFFFF;
}

/* send an ARP request for ip (network order) */
static int ext_net_send_arp(unsigned int ip)
{
	struct eth_hdr *eth;
	struct arp_pkt *arp;
	unsigned char *frame;
	int len = 14 + 28;

	if(!(frame = (unsigned char *)kmalloc(len))) {
		return -ENOMEM;
	}
	eth = (struct eth_hdr *)frame;
	arp = (struct arp_pkt *)(frame + 14);

	memset_b(eth->dst, 0xFF, 6);		/* broadcast */
	memcpy_b(eth->src, ext_mac, 6);
	eth->proto = htons(ETH_P_ARP);

	arp->htype = htons(1);
	arp->ptype = htons(ETH_P_IP);
	arp->hlen = 6;
	arp->plen = 4;
	arp->op = htons(ARPOP_REQUEST);
	memcpy_b(arp->sha, ext_mac, 6);
	arp->spa = ext_ip;
	memset_b(arp->tha, 0, 6);
	arp->tpa = ip;

	ext_sendto(ext_fd, frame, len, NULL, 0);
	kfree((addr_t)frame);
	return 0;
}

/* process an incoming ARP frame; returns 1 if it answered our request */
static int ext_net_handle_arp(const unsigned char *frame, int len)
{
	const struct eth_hdr *eth = (const struct eth_hdr *)frame;
	const struct arp_pkt *arp;
	int r = 0;

	if(len < 14 + 28) {
		return 0;
	}
	arp = (const struct arp_pkt *)(frame + 14);
	if(ntohs(arp->op) == ARPOP_REQUEST && arp->tpa == ext_ip) {
		/* reply to an ARP request for us */
		unsigned char *out;
		int outlen = 14 + 28;

		if(!(out = (unsigned char *)kmalloc(outlen))) {
			return 0;
		}
		{
			struct eth_hdr *oe = (struct eth_hdr *)out;
			struct arp_pkt *oa = (struct arp_pkt *)(out + 14);
			memcpy_b(oe->dst, arp->sha, 6);
			memcpy_b(oe->src, ext_mac, 6);
			oe->proto = htons(ETH_P_ARP);
			oa->htype = htons(1);
			oa->ptype = htons(ETH_P_IP);
			oa->hlen = 6;
			oa->plen = 4;
			oa->op = htons(ARPOP_REPLY);
			memcpy_b(oa->sha, ext_mac, 6);
			oa->spa = ext_ip;
			memcpy_b(oa->tha, arp->sha, 6);
			oa->tpa = arp->spa;
			ext_sendto(ext_fd, out, outlen, NULL, 0);
		}
		kfree((addr_t)out);
	}
	if(ntohs(arp->op) == ARPOP_REPLY && arp->spa == gateway_ip) {
		/* our ARP request was answered by the gateway */
		memcpy_b(arp_mac, arp->sha, 6);
		arp_ip = gateway_ip;
		arp_valid = 1;
		r = 1;
	}
	return r;
}

/* resolve the gateway MAC, sending an ARP request if needed */
static int ext_net_arp_resolve(void)
{
	int i;

	if(arp_valid && arp_ip == gateway_ip) {
		return 0;
	}
	arp_valid = 0;
	ext_net_send_arp(gateway_ip);
	/* wait for the reply (with a modest retry budget) */
	for(i = 0; i < 200 && !arp_valid; i++) {
		unsigned char *frame;
		int n;

		if(!(frame = (unsigned char *)kmalloc(2048))) {
			return -ENOMEM;
		}
		n = ext_recvfrom(ext_fd, frame, 2048, NULL, NULL);
		if(n > 0 && ext_net_handle_arp(frame, n)) {
			kfree((addr_t)frame);
			return 0;
		}
		kfree((addr_t)frame);
	}
	return arp_valid ? 0 : -EHOSTUNREACH;
}

/* send an IP datagram to ip (network order) via the NIC */
int ext_net_send_ip(unsigned int ip, int proto, const void *payload, __size_t len)
{
	struct eth_hdr *eth;
	struct ip_hdr *ip4;
	unsigned char *frame;
	int flen = 14 + 20 + len;
	int errno;

	if(ext_fd < 0) {
		return -ENODEV;
	}
	if((errno = ext_net_arp_resolve()) < 0) {
		return errno;
	}
	if(!(frame = (unsigned char *)kmalloc(flen))) {
		return -ENOMEM;
	}
	memcpy_b(frame + 14 + 20, payload, len);

	eth = (struct eth_hdr *)frame;
	ip4 = (struct ip_hdr *)(frame + 14);
	memcpy_b(eth->dst, arp_mac, 6);
	memcpy_b(eth->src, ext_mac, 6);
	eth->proto = htons(ETH_P_IP);

	ip4->ver_ihl = 0x45;
	ip4->tos = 0;
	ip4->tot_len = htons(20 + len);
	ip4->id = htons(0);
	ip4->frag_off = htons(0x4000);		/* DF */
	ip4->ttl = 64;
	ip4->proto = proto;
	ip4->csum = 0;
	ip4->saddr = ext_ip;
	ip4->daddr = ip;
	ip4->csum = htons(ip_csum(ip4, 20));

	errno = ext_sendto(ext_fd, frame, flen, NULL, 0);
	kfree((addr_t)frame);
	return (errno < 0) ? errno : (int)len;
}

/* receive one IP datagram from the NIC (handling ARP internally) */
int ext_net_recv_ip(unsigned int want_ip, int want_proto,
		    void *payload, __size_t count, unsigned int *from)
{
	unsigned char *frame;
	int n;

	for(;;) {
		if(!(frame = (unsigned char *)kmalloc(2048))) {
			return -ENOMEM;
		}
		n = ext_recvfrom(ext_fd, frame, 2048, NULL, NULL);
		if(n < 0) {
			kfree((addr_t)frame);
			return n;
		}
		if(n < 14) {
			kfree((addr_t)frame);
			continue;
		}
		if(ntohs(((struct eth_hdr *)frame)->proto) == ETH_P_ARP) {
			ext_net_handle_arp(frame, n);
			kfree((addr_t)frame);
			continue;
		}
		if(ntohs(((struct eth_hdr *)frame)->proto) == ETH_P_IP && n >= 14 + 20) {
			struct ip_hdr *ip4 = (struct ip_hdr *)(frame + 14);
			int iplen = ntohs(ip4->tot_len);
			int hlen = (ip4->ver_ihl & 0x0F) * 4;

			if(ip4->proto == want_proto && (want_ip == 0 || ip4->saddr == want_ip)) {
				int datalen = (iplen > n - 14) ? (n - 14 - hlen) : (iplen - hlen);
				int c = (count < (unsigned int)datalen) ? count : datalen;

				if(c > 0) {
					memcpy_b(payload, frame + 14 + hlen, c);
				}
				if(from) {
					*from = ip4->saddr;
				}
				kfree((addr_t)frame);
				return c;
			}
		}
		kfree((addr_t)frame);
	}
}

/* configure the NIC (IP/gateway) - called by the driver on init */
int ext_net_configure(const unsigned char *mac, unsigned int ip, unsigned int gw)
{
	memcpy_b(ext_mac, mac, 6);
	ext_ip = ip;
	gateway_ip = gw;
	arp_valid = 0;
	if(ext_fd < 0) {
		ext_fd = ext_open(0, 0, 0);
	}
	return 0;
}

int ext_net_present(void)
{
	return (ext_fd >= 0) ? 1 : 0;
}

#endif /* CONFIG_NET */
