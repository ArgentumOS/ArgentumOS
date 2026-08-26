/*
 * fnx/net/core.c
 *
 * Copyright 2025, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/config.h>
#include <fnx/errno.h>
#include <fnx/ioctl.h>
#include <fnx/netdev.h>
#include <fnx/netdevice.h>
#include <fnx/fs.h>
#include <fnx/socket.h>
#include <fnx/string.h>
#include <fnx/mm.h>

#ifdef CONFIG_NET
/* the real NIC is exposed to userland as a fixed pseudo-interface
 * "eth0" with ifindex 1; its address is the kernel's ext_ip (set by
 * the in-kernel DHCP at boot, or by SIOCSIFADDR from userland) */
#define EXT_IFNAME	"eth0"
#define EXT_IFINDEX	1

static int eth0_check(const char *name)
{
	return strncmp(name, EXT_IFNAME, IFNAMSIZ);
}

int dev_ioctl(int cmd, void *arg)
{
	struct ifreq ifr;
	struct sockaddr_in *sin;
	extern int ext_net_present(void);
	extern unsigned int ext_net_get_ip(void);
	extern void ext_net_set_ip(unsigned int);
	extern void ext_net_get_mac(unsigned char *);

	if(!ext_net_present()) {
		return -ENODEV;
	}
	if(!arg) {
		return -EFAULT;
	}
	if(check_user_area(VERIFY_READ, arg, sizeof(struct ifreq))) {
		return -EFAULT;
	}
	memcpy_b(&ifr, arg, sizeof(struct ifreq));
	if(eth0_check(ifr.ifr_name)) {
		return -ENODEV;	/* unknown interface */
	}

	switch(cmd) {
		case SIOCGIFFLAGS:
			ifr.ifr_flags = IFF_UP | IFF_BROADCAST | IFF_RUNNING | IFF_MULTICAST;
			break;
		case SIOCSIFFLAGS:
			/* accept-and-ignore: the ext NIC has no runtime flags */
			break;
		case SIOCGIFINDEX:
			ifr.ifr_ifindex = EXT_IFINDEX;
			break;
		case SIOCGIFHWADDR:
			ifr.ifr_hwaddr.sa_family = ARPHRD_ETHER;
			ext_net_get_mac((unsigned char *)ifr.ifr_hwaddr.sa_data);
			break;
		case SIOCGIFADDR:
			sin = (struct sockaddr_in *)&ifr.ifr_addr;
			sin->sin_family = AF_INET;
			sin->sin_port = 0;
			sin->sin_addr = ext_net_get_ip();
			break;
		case SIOCSIFADDR:
			sin = (struct sockaddr_in *)&ifr.ifr_addr;
			if(sin->sin_family != AF_INET) {
				return -EINVAL;
			}
			ext_net_set_ip(sin->sin_addr);	/* network order */
			break;
		case SIOCGIFNETMASK:
			sin = (struct sockaddr_in *)&ifr.ifr_netmask;
			sin->sin_family = AF_INET;
			sin->sin_port = 0;
			sin->sin_addr = 0x00ffffff;	/* 255.255.255.0 (SLIRP subnet) */
			break;
		case SIOCSIFNETMASK:
			/* accept-and-ignore: the ext path is a single subnet */
			break;
		case SIOCGIFBRDADDR:
			sin = (struct sockaddr_in *)&ifr.ifr_broadaddr;
			sin->sin_family = AF_INET;
			sin->sin_port = 0;
			sin->sin_addr = 0xff02000a;	/* 10.0.2.255 in network order */
			break;
		case SIOCGIFMTU:
			ifr.ifr_mtu = 1500;
			break;
		default:
			return -EINVAL;
	}

	if(check_user_area(VERIFY_WRITE, arg, sizeof(struct ifreq))) {
		return -EFAULT;
	}
	memcpy_b(arg, &ifr, sizeof(struct ifreq));
	return 0;
}
#endif /* CONFIG_NET */
