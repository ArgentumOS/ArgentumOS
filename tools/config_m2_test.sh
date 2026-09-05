#!/bin/sh
# config_m2_test.sh — M2 acceptance: identity domains ship in
# /System/Configuration replacing the legacy passwd/group/shells files,
# and overridable first-party defaults (system.xfb) ship in
# /Shared/Configuration with a System copy overriding them (plan §5.0).
# Replays the Makefile staging into a scratch $FNX_CONFIG_ROOT and runs
# the host-built `config` CLI against it.
set -u
REPO=$(cd "$(dirname "$0")/.." && pwd)
ROOT=$(mktemp -d)
trap 'rm -rf "$ROOT"' EXIT
mkdir -p "$ROOT/System/Configuration" "$ROOT/Shared/Configuration"
export FNX_CONFIG_ROOT="$ROOT"
CONF="$REPO/.build/m0config"
CFG="$REPO/userland/configuration"

cc -I"$REPO/include" -o "$CONF" \
	"$REPO/userland/config.c" "$REPO/userland/libconfig.c" || exit 2

fails=0
ok()  { echo "PASS: $1"; }
bad() { echo "FAIL: $1"; fails=$((fails + 1)); }

# 1) stage exactly like the Makefile userland64 target
cp "$CFG/system.passwd.conf" "$ROOT/System/Configuration/"
cp "$CFG/system.group.conf"  "$ROOT/System/Configuration/"
cp "$CFG/system.shells.conf" "$ROOT/System/Configuration/"
cp "$CFG/system.xfb.conf"    "$ROOT/Shared/Configuration/"

# no legacy colon/line files are staged
[ ! -e "$ROOT/System/Configuration/passwd" ] && \
	[ ! -e "$ROOT/System/Configuration/group" ] && \
	[ ! -e "$ROOT/System/Configuration/shells" ] \
	&& ok "no legacy passwd/group/shells files staged" \
	|| bad "legacy files staged"

# 2) acceptance reads
[ "$("$CONF" read system.passwd user.admin.uid)" = "0" ] \
	&& ok "config read system.passwd user.admin.uid == 0" \
	|| bad "read passwd uid"
[ "$("$CONF" read system.passwd user.admin.gecos)" = "Admin" ] \
	&& ok "admin record re-expresses the Admin account" \
	|| bad "admin record"
[ "$("$CONF" read system.group group.admin.gid)" = "0" ] \
	&& ok "group domain reads gid 0" || bad "read group gid"
[ "$("$CONF" read system.shells shells)" = "/System/Tools/sh" ] \
	&& ok "shells list domain reads" || bad "read shells"

# 3) Xfb default domain reads from Shared...
[ "$("$CONF" read system.xfb display)" = "0" ] \
	&& ok "xfb defaults resolve from Shared" \
	|| bad "xfb defaults resolve from Shared"

# 4) ...and a System copy overrides it (system-first)
printf 'display = 7\n' > "$ROOT/System/Configuration/system.xfb.conf"
[ "$("$CONF" read system.xfb display)" = "7" ] \
	&& ok "System copy overrides the Shared xfb default" \
	|| bad "System copy overrides Shared xfb"

# 5) shipped record files parse canonically: a rewrite preserves the
#    key set (the writer strips comments, so compare parsed content)
"$CONF" read -s system.passwd > "$ROOT/pw.before"
"$CONF" write -s system.passwd user.admin.uid 0 >/dev/null
"$CONF" read -s system.passwd > "$ROOT/pw.after"
cmp -s "$ROOT/pw.before" "$ROOT/pw.after" \
	&& ok "passwd domain round-trips (key set stable)" \
	|| bad "passwd domain round-trips"
"$CONF" read -g system.xfb > "$ROOT/xfb.before"
"$CONF" write -g system.xfb display "0" >/dev/null
"$CONF" read -g system.xfb > "$ROOT/xfb.after"
cmp -s "$ROOT/xfb.before" "$ROOT/xfb.after" \
	&& ok "xfb domain round-trips (key set stable)" \
	|| bad "xfb domain round-trips"

[ "$fails" -eq 0 ] && echo "M2 CLI: ALL PASS" || echo "M2 CLI: $fails FAILURE(S)"
exit "$fails"
