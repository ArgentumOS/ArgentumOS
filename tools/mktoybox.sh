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

# The M4 account tools (passwd/chsh/useradd/userdel/groupadd/groupdel)
# write the .conf identity domains through libconfig (userland/libconfig.c),
# which is compiled once and linked into the toybox binary via LDFLAGS.
mkdir -p "$ROOT/.build"
"$CC_WRAP" -I"$ROOT/include" -c "$ROOT/userland/libconfig.c" \
  -o "$ROOT/.build/toybox-libconfig.o"

# Source patches (third_party/toybox-m4.patch when present) are applied to
# the clean checkout before building and rolled back afterwards, so the
# submodule stays pristine; with no patch file the tree is built as-is
# (the development loop).
PATCHED=
if [ -f "$ROOT/third_party/toybox-m4.patch" ]; then
  git checkout -- . 2>/dev/null || true
  rm -f lib/configedit.c   # patch-added file would block git apply
  git apply "$ROOT/third_party/toybox-m4.patch"
  PATCHED=1
fi

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
            if d == 'toys/pending' and f in (
                    'dhcp.c',           # FNX vendors its linux headers
                    'chsh.c',           # M4: account tools on the domains
                    'useradd.c', 'userdel.c',
                    'groupadd.c', 'groupdel.c'):
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
# re-enable the applets FNX supports: the toybox DHCP client (pending,
# default n; it now has the linux headers it needs via tools/kernel-headers)
# and the M4 account tools (chsh + the user/group add/del applets), which
# are read/write against the .conf identity domains through libconfig.
renable = ['DHCP', 'PASSWD', 'CHSH', 'USERADD', 'USERDEL', 'GROUPADD', 'GROUPDEL']
for sym in renable:
    out = [l if l != '# CONFIG_%s is not set' % sym else 'CONFIG_%s=y' % sym
           for l in out]
open('.config', 'w').write('\n'.join(out) + '\n')
print("mktoybox: disabled %d applets needing kernel headers" % len(syms))
EOF

make CC="$CC_WRAP" CFLAGS="-I$ROOT/include" \
  LDFLAGS="$ROOT/.build/toybox-libconfig.o"
# toybox's build leaves the binary read-only (0555); strip needs write access
chmod +w toybox 2>/dev/null || true
strip toybox
# Install the applets into the stage dir now (fresh config + flags), so the
# userland64 target can just copy this tree instead of re-running toybox's
# make (which would rebuild against the reverted checkout and old config).
rm -rf "$ROOT/.build/toybox-root"
make CC="$CC_WRAP" CFLAGS="-I$ROOT/include" \
  LDFLAGS="$ROOT/.build/toybox-libconfig.o" \
  install PREFIX="$ROOT/.build/toybox-root" >/dev/null 2>&1 || \
  make CC="$CC_WRAP" install PREFIX="$ROOT/.build/toybox-root"
if [ -n "$PATCHED" ]; then
  git checkout -- . 2>/dev/null || true
  rm -f lib/configedit.c   # untracked patch-added file
fi
rm -f toybox64            # cp artifact from the top-level Makefile
echo "mktoybox: $(file -b toybox)"
