#!/bin/sh
# kconf_corpus.sh — conformance corpus for the kernel.conf parser
# (docs/design/kernel-conf-plan.md M1 acceptance).
#
# Both parsers must produce the SAME effective key/value listing from the
# same file:
#   kernel:   kernel/kconf.c (the kernel subset parser, host-built)
#   userland: userland/libconfig.c parse_conf via config_get_all (the
#             canonical .conf parser the config CLI uses)
#
# Each corpus file in tools/kconf_corpus/*.conf is fed to both; outputs
# are diffed. Exits nonzero on any mismatch.

set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CORPUS="$ROOT/tools/kconf_corpus"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

cc -I"$ROOT/include" -o "$TMP/kconf_host" \
	"$ROOT/kernel/kconf.c" "$ROOT/tools/kconf_host.c"
cc -I"$ROOT/include" -I"$ROOT/userland" -o "$TMP/kconf_libc_host" \
	"$ROOT/userland/libconfig.c" "$ROOT/tools/kconf_libconfig_host.c"

fails=0
for f in "$CORPUS"/*.conf; do
	name="$(basename "$f" .conf)"
	# kernel parser: raw file
	"$TMP/kconf_host" "$f" > "$TMP/k.$name" 2>/dev/null
	# userland parser: domain file in a scratch system scope
	mkdir -p "$TMP/root/System/Configuration"
	cp "$f" "$TMP/root/System/Configuration/system.corpus.conf"
	(
		cd "$TMP"
		FNX_CONFIG_ROOT="$TMP/root" "$TMP/kconf_libc_host" "$TMP/root" \
			system.corpus > "u.$name" 2>"u.$name.err" || true
	)
	if diff -u "$TMP/u.$name" "$TMP/k.$name" > "$TMP/d.$name"; then
		echo "PASS: $name"
	else
		echo "FAIL: $name"
		sed -n '1,20p' "$TMP/d.$name"
		fails=$((fails + 1))
	fi
done
echo "corpus: $([ "$fails" -eq 0 ] && echo all-pass || echo "$fails FAILED")"
exit "$fails"
