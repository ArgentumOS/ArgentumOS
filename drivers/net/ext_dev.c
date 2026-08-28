/*
 * fnx/drivers/net/ext_dev.c
 *
 * The ext_* dispatcher: holds the active NIC driver's ops table and
 * forwards the raw-Ethernet API to it. Probing is ordered - virtio-net,
 * rtl8139, ne2k, tulip, pcnet, e1000, ne2k_isa, eepro100, vmxnet3,
 * e1000e, igb - and only one driver is active at a time, so
 * the kernel-side framing in net/ext_net.c (ARP/IP/DHCP) and the
 * sockets in net/ipv4.c / net/af_packet.c are driver-independent.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#include <fnx/config.h>
#include <fnx/errno.h>
#include <fnx/net.h>
#include <fnx/net/ext_net.h>
#include <fnx/socket.h>
#include <fnx/string.h>

#ifdef CONFIG_NET
static struct ext_net_ops *ext_ops;

int ext_init(void)
{
	struct ext_net_ops *nic;

	/* a NIC may already be registered: the USB CDC-ECM adapter
	 * enumerates during usb_init() (start_kernel), which runs BEFORE
	 * net_init() probes the PCI NICs here. A found PCI NIC overrides
	 * it (PCI is the primary); otherwise the USB NIC stays. */
	nic = NULL;
	if((nic = virtio_net_probe())) {
		goto configured;
	}
	if((nic = rtl8139_probe())) {
		goto configured;
	}
	if((nic = ne2k_probe())) {
		goto configured;
	}
	if((nic = tulip_probe())) {
		goto configured;
	}
	if((nic = pcnet_probe())) {
		goto configured;
	}
	if((nic = e1000_probe())) {
		goto configured;
	}
	if((nic = ne2k_isa_probe())) {
		goto configured;
	}
	if((nic = eepro100_probe())) {
		goto configured;
	}
	if((nic = vmxnet3_probe())) {
		goto configured;
	}
	if((nic = e1000e_probe())) {
		goto configured;
	}
	if((nic = igb_probe())) {
		goto configured;
	}
	return 0;	/* no PCI NIC: keep any already-registered USB NIC */
configured:
	/* the probe cannot configure the framing itself (the dispatcher's
	 * ops pointer is only set once it returns), so do it here: SLIRP
	 * defaults, and the in-kernel DHCP lease so ICMP is answered.
	 * A PCI NIC overrides a previously-registered USB NIC. */
	ext_ops = nic;
	{
		extern int ext_net_configure(const unsigned char *, unsigned int, unsigned int);
		ext_net_configure(ext_ops->mac, 0x0F02000A /*10.0.2.15*/, 0x0202000A /*10.0.2.2*/);
	}
	return 0;
}

/* runtime NIC registration (e.g. the USB CDC-ECM adapter enumerated by the
 * xhci after boot). The ext_* dispatcher supports one NIC: take it only
 * when none is configured yet, then wire the SLIRP defaults like above. */
int ext_net_register_nic(struct ext_net_ops *ops)
{
	extern int ext_net_configure(const unsigned char *, unsigned int, unsigned int);

	if(ext_ops) {
		return -EBUSY;
	}
	ext_ops = ops;
	ext_net_configure(ops->mac, 0x0F02000A /*10.0.2.15*/, 0x0202000A /*10.0.2.2*/);
	return 0;
}

int ext_open(int domain, int type, int protocol)
{
	return ext_ops ? ext_ops->open(domain, type, protocol) : -ENODEV;
}

int ext_close(int fd_ext)
{
	return ext_ops ? ext_ops->close(fd_ext) : -ENODEV;
}

int ext_bind(int fd_ext, const struct sockaddr *addr, int addrlen)
{
	return ext_ops ? ext_ops->bind(fd_ext, addr, addrlen) : -ENODEV;
}

int ext_listen(int fd_ext, int backlog)
{
	return ext_ops ? ext_ops->listen(fd_ext, backlog) : -ENODEV;
}

int ext_connect(int fd_ext, const struct sockaddr *addr, int addrlen)
{
	return ext_ops ? ext_ops->connect(fd_ext, addr, addrlen) : -ENODEV;
}

int ext_accept(int fd_ext, struct sockaddr *addr, unsigned int *addrlen)
{
	return ext_ops ? ext_ops->accept(fd_ext, addr, addrlen) : -ENODEV;
}

int ext_ioctl(int fd_ext, int cmd, void *arg)
{
	return ext_ops ? ext_ops->ioctl(fd_ext, cmd, arg) : -ENODEV;
}

int ext_sendto(int fd_ext, const void *buffer, __size_t count, const struct sockaddr *addr, int addrlen)
{
	return ext_ops ? ext_ops->sendto(fd_ext, buffer, count, addr, addrlen) : -ENODEV;
}

int ext_recvfrom(int fd_ext, void *buffer, __size_t count, struct sockaddr *addr, int *addrlen)
{
	return ext_ops ? ext_ops->recvfrom(fd_ext, buffer, count, addr, addrlen) : -ENODEV;
}

int ext_read(int fd_ext, void *buffer, __size_t count)
{
	return ext_ops ? ext_ops->read(fd_ext, buffer, count) : -ENODEV;
}

int ext_write(int fd_ext, const void *buffer, __size_t count)
{
	return ext_ops ? ext_ops->write(fd_ext, buffer, count) : -ENODEV;
}

int ext_poll(int fd_ext, int flag)
{
	return ext_ops ? ext_ops->poll(fd_ext, flag) : 0;
}
#endif /* CONFIG_NET */
