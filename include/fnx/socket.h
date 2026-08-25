/*
 * fnx/include/fnx/socket.h
 *
 * Copyright 2023, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#ifdef CONFIG_NET

#ifndef _FNX_SOCKET_H
#define _FNX_SOCKET_H

#include <fnx/types.h>

/* supported address families (domains) */
#define AF_UNIX		1		/* UNIX domain socket */
#define AF_LOCAL	AF_UNIX		/* POSIX name for AF_UNIX */
#define AF_INET		2		/* IPv4 Internet domain socket */

/* protocol families */
#define PF_UNIX		AF_UNIX
#define PF_LOCAL	AF_LOCAL
#define PF_INET		AF_INET

/* types */
#define SOCK_STREAM	1
#define SOCK_DGRAM	2
#define SOCK_RAW	3

/* IP protocols */
#define IPPROTO_IP	0
#define IPPROTO_ICMP	1
#define IPPROTO_TCP	6
#define IPPROTO_UDP	17
#define IPPROTO_RAW	255

/* loopback address (network byte order: 127.0.0.1) */
#define INADDR_LOOPBACK	((__u32)0x7f000001)
#define INADDR_ANY	0

/* byte-order helpers (x86 is little-endian) */
static __inline__ __u16 htons(__u16 x) { return ((x & 0xff) << 8) | ((x >> 8) & 0xff); }
static __inline__ __u16 ntohs(__u16 x) { return htons(x); }
static __inline__ __u32 htonl(__u32 x) { return ((x & 0xff) << 24) | ((x & 0xff00) << 8) | ((x >> 8) & 0xff00) | ((x >> 24) & 0xff); }
static __inline__ __u32 ntohl(__u32 x) { return htonl(x); }

/* maximum queue length specifiable by listen() */
#define SOMAXCONN	128

/* states */
#define SS_UNCONNECTED		1
#define SS_CONNECTING		2
#define SS_CONNECTED		3
#define SS_DISCONNECTING	4

/* flags */
#define SO_ACCEPTCONN		0x10000

/* flags for send() and recv() */
#define MSG_PEEK		0x02
#define MSG_DONTWAIT		0x40
#define MSG_NOSIGNAL		0x4000
#define MSG_PEEK_SRC		0x08

typedef unsigned short int sa_family_t;


/* generic socket address structure */
struct sockaddr {
	sa_family_t sa_family;		/* address family: AF_xxx */
	char sa_data[14];		/* protocol specific address */
};

/* UNIX domain socket address structure */
struct sockaddr_un {
        sa_family_t sun_family;		/* AF_UNIX */
        char sun_path[108];		/* socket filename */
};

/* IPv4 socket address structure (user ABI, 16 bytes) */
struct sockaddr_in {
	sa_family_t sin_family;		/* AF_INET */
	__u16 sin_port;			/* port in network byte order */
	__u32 sin_addr;			/* address in network byte order */
	char sin_zero[8];
};

#endif /* _FNX_SOCKET_H */

#endif /* CONFIG_NET */
