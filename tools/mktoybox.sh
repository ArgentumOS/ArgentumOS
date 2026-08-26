#!/bin/sh
# Build the toybox submodule (third_party/toybox, pinned 0.8.11) as a static
# musl i386 multi-call binary, the same way init/dash are built.
#
# FNX's musl toolchain does not ship kernel headers, so any applet that
# includes a <linux/*.h> or <asm/*.h> header cannot compile: those applets are
# disabled after `make defconfig` (which enables everything else).
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CC_WRAP="${TOYBOX_CC:-$ROOT/tools/musl-gcc.sh}"
cd "$ROOT/third_party/toybox"

make defconfig

python3 - <<'EOF'
import os, re
offenders = []
# applets that need kernel headers (musl has none); android + pending are
# disabled wholesale (android is bionic-specific, pending is unfinished),
# EXCEPT toys/pending/dhcp.c: FNX vendors the linux headers it needs
# (tools/kernel-headers) so the DHCP client can be built.
for d in ('toys/android', 'toys/pending'):
    for f in os.listdir(d):
        if f.endswith('.c'):
            if d == 'toys/pending' and f == 'dhcp.c':
                continue
            offenders.append(os.path.join(d, f))
offenders += [
    'toys/net/rfkill.c', 'toys/net/tunctl.c',
    'toys/other/blockdev.c', 'toys/other/blkdiscard.c', 'toys/other/eject.c',
    'toys/other/fsfreeze.c', 'toys/other/gpiod.c', 'toys/other/i2ctools.c',
    'toys/other/lsattr.c', 'toys/other/nbd_client.c', 'toys/other/nsenter.c',
    'toys/other/openvt.c', 'toys/other/rtcwake.c', 'toys/other/vconfig.c',
    'toys/other/hwclock.c', 'toys/other/losetup.c', 'toys/other/mix.c',
    'toys/other/watchdog.c',
]
syms = set()
for f in offenders:
    src = open(f).read()
    for m in re.finditer(r'^config (\w+)', src, re.M):
        syms.add(m.group(1))
lines = open('.config').read().splitlines()
out = []
for line in lines:
    if line.startswith('CONFIG_') and line.endswith('=y') and line[7:-2] in syms:
        out.append('# CONFIG_%s is not set' % line[7:-2])
    else:
        out.append(line)
# re-enable the toybox DHCP client (pending applet, default n; it now has
# the linux headers it needs via tools/kernel-headers)
out = [l if l != '# CONFIG_DHCP is not set' else 'CONFIG_DHCP=y' for l in out]
open('.config', 'w').write('\n'.join(out) + '\n')
print("mktoybox: disabled %d applets needing kernel headers" % len(syms))
EOF

make CC="$CC_WRAP" CFLAGS= LDFLAGS=
# toybox's build leaves the binary read-only (0555); strip needs write access
chmod +w toybox 2>/dev/null || true
strip toybox
echo "mktoybox: $(file -b toybox)"
