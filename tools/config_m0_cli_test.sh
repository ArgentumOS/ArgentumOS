#!/bin/sh
# config_m0_cli_test.sh — M0 acceptance via the `config` CLI: nested
# record domains parse/read/write canonically; flat domains stay flat;
# duplicate-record-name and '{'-prefixed bare-value files are parse
# errors. Builds the CLI on the host, runs it against a scratch
# $FNX_CONFIG_ROOT tree. Prints PASS/FAIL; exit status = failures.
set -u
REPO=$(cd "$(dirname "$0")/.." && pwd)
ROOT=$(mktemp -d)
trap 'rm -rf "$ROOT"' EXIT
mkdir -p "$ROOT/System/Configuration" "$ROOT/Shared/Configuration" \
	 "$ROOT/Users/root/Configuration"
export FNX_CONFIG_ROOT="$ROOT"
CONF="$REPO/.build/m0config"

cc -I"$REPO/include" -I"$REPO/userland" -o "$CONF" \
	"$REPO/userland/tools/config.c" "$REPO/userland/libconfig.c" || exit 2

fails=0
ok()  { echo "PASS: $1"; }
bad() { echo "FAIL: $1"; fails=$((fails + 1)); }

DOM=system.m0

# 1) start from a shipped-style nested record file; extend it via CLI
cat > "$ROOT/System/Configuration/$DOM.conf" <<'EOF2'
user = {
    admin = {
        uid = 0
        gecos = "Root Admin"
    }
}
EOF2
"$CONF" write -s "$DOM" user.bob.uid 1
[ "$("$CONF" read -s "$DOM" user.admin.uid)" = "0" ] \
	&& ok "read nested key user.admin.uid" || bad "read nested key"
[ "$("$CONF" read -s "$DOM" user.admin.gecos)" = "Root Admin" ] \
	&& ok "read nested string key" || bad "read nested string key"

# 2) the domain file must be written in canonical nested spelling
if grep -q '^user = {$' "$ROOT/System/Configuration/$DOM.conf" &&
   grep -q '^    admin = {$' "$ROOT/System/Configuration/$DOM.conf" &&
   grep -q '^        gecos = "Root Admin"$' "$ROOT/System/Configuration/$DOM.conf" &&
   ! grep -q 'user.admin' "$ROOT/System/Configuration/$DOM.conf"; then
	ok "record domain written in nested spelling"
else
	bad "record domain written in nested spelling"
fi

# 3) whole-domain read lists dotted keys
"$CONF" read -s "$DOM" | grep -q '^user.admin.uid = 0$' \
	&& ok "whole-domain read shows nested keys flat" \
	|| bad "whole-domain read shows nested keys flat"

# 4) round-trip canonically: write an existing key with the same value,
#    file must be byte-identical
cp "$ROOT/System/Configuration/$DOM.conf" "$ROOT/before.conf"
"$CONF" write -s "$DOM" user.bob.uid 1 >/dev/null
cmp -s "$ROOT/before.conf" "$ROOT/System/Configuration/$DOM.conf" \
	&& ok "canonical rewrite is byte-stable" || bad "byte-stable rewrite"

# 5) pure flat dotted domain stays flat
FLAT=system.m0flat
"$CONF" write -s "$FLAT" window.width 100
"$CONF" write -s "$FLAT" window.height 300
if grep -q '^window.width = 100$' "$ROOT/System/Configuration/$FLAT.conf" &&
   ! grep -q '^window = {$' "$ROOT/System/Configuration/$FLAT.conf"; then
	ok "flat domain stays flat"
else
	bad "flat domain stays flat"
fi

# 6) duplicate record names -> parse error (exit 1, malformed message)
BAD=system.m0bad
cat > "$ROOT/System/Configuration/$BAD.conf" <<'EOF2'
user = {
    a = {
        x = 1
    }
    a = {
        y = 2
    }
}
EOF2
if "$CONF" read -s "$BAD" user.a.x >/dev/null 2>&1; then
	bad "duplicate record name rejected"
else
	ok "duplicate record name rejected"
fi

# 7) a bare value beginning with '{' -> parse error
BAD2=system.m0bad2
printf 'key = {nope\n' > "$ROOT/System/Configuration/$BAD2.conf"
if "$CONF" read -s "$BAD2" key >/dev/null 2>&1; then
	bad "bare '{' value rejected"
else
	ok "bare '{' value rejected"
fi

[ "$fails" -eq 0 ] && echo "M0 CLI: ALL PASS" || echo "M0 CLI: $fails FAILURE(S)"
exit "$fails"
