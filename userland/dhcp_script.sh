#!/bin/sh
# toybox dhcp event script (udhcpc-style env: $interface, $ip, $subnet).
# 'ifconfig' pushes the leased address into the FNX kernel (SIOCSIFADDR
# -> ext_ip); there are no runtime flags to clear on deconfig.
case "$1" in
	bound|renew)
		ifconfig "$interface" "$ip" netmask "$subnet"
		;;
	deconfig)
		:
		;;
esac
