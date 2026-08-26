/*
 * fnx/tools/kernel-headers/linux/if_ether.h
 *
 * Minimal Linux-uapi-compatible <linux/if_ether.h> for the FNX userland
 * builds. FNX's musl toolchain ships no kernel headers, so applets like
 * toybox's dhcp/arping cannot compile; this subset provides exactly the
 * Ethernet ABI constants/structs those applets expect, with the same
 * names and values as the real Linux uapi header. Not used by the FNX
 * kernel itself (which keeps its own definitions under include/fnx/).
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#ifndef _LINUX_IF_ETHER_H
#define _LINUX_IF_ETHER_H

#define ETH_ALEN	6	/* octets in one Ethernet addr */

/* these are the defined Ethernet Protocol ID's */
#define ETH_P_IP	0x0800	/* Internet Protocol packet */
#define ETH_P_ARP	0x0806	/* Address Resolution packet */
#define ETH_P_ALL	0x0003	/* every packet (be careful!) */

struct ethhdr {
	unsigned char	h_dest[ETH_ALEN];	/* destination eth addr */
	unsigned char	h_source[ETH_ALEN];	/* source ether addr */
	unsigned short	h_proto;		/* packet type ID field (network order) */
};

#endif /* _LINUX_IF_ETHER_H */
