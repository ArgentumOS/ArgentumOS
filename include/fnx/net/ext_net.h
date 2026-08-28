/*
 * fnx/include/fnx/net/ext_net.h
 *
 * The ext_* API: raw Ethernet-frame access over whichever real NIC is
 * present (virtio-net, rtl8139, ne2k, tulip). One driver is active at a
 * time; net/ext_net.c, net/ipv4.c and net/af_packet.c call the ext_*
 * functions in drivers/net/ext_dev.c, which forward to the active
 * driver's ops table.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#ifdef CONFIG_NET

#ifndef _FNX_NET_EXT_NET_H
#define _FNX_NET_EXT_NET_H

#include <fnx/types.h>
#include <fnx/socket.h>

struct ext_net_ops {
	int (*open)(int, int, int);
	int (*close)(int);
	int (*bind)(int, const struct sockaddr *, int);
	int (*listen)(int, int);
	int (*connect)(int, const struct sockaddr *, int);
	int (*accept)(int, struct sockaddr *, unsigned int *);
	int (*ioctl)(int, int, void *);
	int (*sendto)(int, const void *, __size_t, const struct sockaddr *, int);
	int (*recvfrom)(int, void *, __size_t, struct sockaddr *, int *);
	int (*read)(int, void *, __size_t);
	int (*write)(int, const void *, __size_t);
	int (*poll)(int, int);
	unsigned char mac[6];	/* the NIC's MAC (set by the probe) */
};

/* driver probes: return the driver's ops table when the NIC is present
 * and initialized (and the network framing is configured), NULL otherwise */
struct ext_net_ops *virtio_net_probe(void);
struct ext_net_ops *rtl8139_probe(void);
struct ext_net_ops *ne2k_probe(void);
struct ext_net_ops *tulip_probe(void);
struct ext_net_ops *pcnet_probe(void);
struct ext_net_ops *e1000_probe(void);
struct ext_net_ops *ne2k_isa_probe(void);
struct ext_net_ops *eepro100_probe(void);
struct ext_net_ops *vmxnet3_probe(void);
struct ext_net_ops *e1000e_probe(void);
struct ext_net_ops *igb_probe(void);

/* the ext_* dispatcher (drivers/net/ext_dev.c) */
int ext_init(void);
int ext_open(int, int, int);
int ext_close(int);
int ext_bind(int, const struct sockaddr *, int);
int ext_listen(int, int);
int ext_connect(int, const struct sockaddr *, int);
int ext_accept(int, struct sockaddr *, unsigned int *);
int ext_ioctl(int, int, void *);
int ext_sendto(int, const void *, __size_t, const struct sockaddr *, int);
int ext_recvfrom(int, void *, __size_t, struct sockaddr *, int *);
int ext_read(int, void *, __size_t);
int ext_write(int, const void *, __size_t);
int ext_poll(int, int);

#endif /* _FNX_NET_EXT_NET_H */

#endif /* CONFIG_NET */

/* runtime NIC registration (USB CDC-ECM etc.); -EBUSY when set */
int ext_net_register_nic(struct ext_net_ops *);
