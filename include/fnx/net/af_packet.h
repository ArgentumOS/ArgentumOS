/*
 * fnx/include/fnx/net/af_packet.h
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#ifdef CONFIG_NET

#ifndef _FNX_NET_AF_PACKET_H
#define _FNX_NET_AF_PACKET_H

extern struct proto_ops packet_ops;

int packet_init(void);

#endif /* _FNX_NET_AF_PACKET_H */

#endif /* CONFIG_NET */
