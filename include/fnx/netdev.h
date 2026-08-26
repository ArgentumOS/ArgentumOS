/*
 * fnx/include/fnx/netdev.h
 *
 * Network interface ioctls and the user-space ifreq ABI (matches the
 * Linux sockios.h / netdevice.h subset that ifconfig and toybox's dhcp
 * client use). The ext NIC is exposed as a fixed pseudo-interface
 * "eth0" with ifindex 1.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#ifndef _FNX_NETDEV_H
#define _FNX_NETDEV_H

#include <fnx/socket.h>

#define IFNAMSIZ	16

/* interface flags (ifr_flags) */
#define IFF_UP		0x0001		/* interface is up */
#define IFF_BROADCAST	0x0002		/* broadcast address valid */
#define IFF_RUNNING	0x0040		/* resources allocated */
#define IFF_MULTICAST	0x1000		/* supports multicast */

/* ARP hardware type for ifr_hwaddr.sa_family */
#define ARPHRD_ETHER	1

/* struct ifmap: keeps the ifreq union at the Linux ABI size (24 bytes) */
struct ifmap {
	unsigned long mem_start;
	unsigned long mem_end;
	unsigned short base_addr;
	unsigned char irq;
	unsigned char dma;
	unsigned char port;
	unsigned char port_type;
	unsigned char rev;
};

/* struct ifreq: 16-byte name + 24-byte union (40 bytes total, x86-64,
 * matching the musl userland ABI) */
struct ifreq {
	char ifr_name[IFNAMSIZ];
	union {
		struct sockaddr ifr_addr;
		struct sockaddr ifr_dstaddr;
		struct sockaddr ifr_broadaddr;
		struct sockaddr ifr_netmask;
		struct sockaddr ifr_hwaddr;
		short ifr_flags;
		int ifr_ifindex;
		int ifr_metric;
		int ifr_mtu;
		struct ifmap ifr_map;
		char ifr_slave[IFNAMSIZ];
		char ifr_newname[IFNAMSIZ];
		void *ifr_data;
	} ifr_ifru;
};

#define ifr_addr	ifr_ifru.ifr_addr
#define ifr_dstaddr	ifr_ifru.ifr_dstaddr
#define ifr_broadaddr	ifr_ifru.ifr_broadaddr
#define ifr_netmask	ifr_ifru.ifr_netmask
#define ifr_hwaddr	ifr_ifru.ifr_hwaddr
#define ifr_flags	ifr_ifru.ifr_flags
#define ifr_ifindex	ifr_ifru.ifr_ifindex
#define ifr_metric	ifr_ifru.ifr_metric
#define ifr_mtu		ifr_ifru.ifr_mtu
#define ifr_data	ifr_ifru.ifr_data

/* interface ioctls (linux/sockios.h) */
#define SIOCGIFNAME	0x8910		/* get iface name */
#define SIOCGIFFLAGS	0x8913		/* get iface flags */
#define SIOCSIFFLAGS	0x8914		/* set iface flags */
#define SIOCGIFADDR	0x8915		/* get iface address */
#define SIOCSIFADDR	0x8916		/* set iface address */
#define SIOCGIFBRDADDR	0x8919		/* get broadcast address */
#define SIOCSIFBRDADDR	0x891a		/* set broadcast address */
#define SIOCGIFNETMASK	0x891b		/* get netmask */
#define SIOCSIFNETMASK	0x891c		/* set netmask */
#define SIOCGIFMTU	0x8921		/* get MTU */
#define SIOCSIFMTU	0x8922		/* set MTU */
#define SIOCGIFHWADDR	0x8927		/* get hardware (MAC) address */
#define SIOCSIFHWADDR	0x8924		/* set hardware address */
#define SIOCGIFINDEX	0x8933		/* get iface index */

#endif /* _FNX_NETDEV_H */
