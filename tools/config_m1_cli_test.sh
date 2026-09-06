#!/bin/sh
# config_m1_cli_test.sh — M1 acceptance via the `config` CLI:
# precedence system -> user -> shared and the winning-tier display on
# merged whole-domain reads. Runs against a scratch $FNX_CONFIG_ROOT.
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

DOM=system.m1

# same value in all three scopes -> `config read` returns the system one
"$CONF" write -s "$DOM" theme.size 11
"$CONF" write -g "$DOM" theme.size 12
"$CONF" write -u "$DOM" theme.size 13
[ "$("$CONF" read "$DOM" theme.size)" = "11" ] \
	&& ok "system value returned when in all three scopes" \
	|| bad "system value returned when in all three scopes"

# only in user + shared -> the user one
"$CONF" write -g "$DOM" theme.color red
"$CONF" write -u "$DOM" theme.color blue
[ "$("$CONF" read "$DOM" theme.color)" = "blue" ] \
	&& ok "user value returned over shared" || bad "user over shared"

# only in shared -> the shared one
"$CONF" write -g "$DOM" widget.alpha true
[ "$("$CONF" read "$DOM" widget.alpha)" = "true" ] \
	&& ok "shared value returned alone" || bad "shared alone"

# a user -u write no longer shadows a System value
"$CONF" write -u "$DOM" theme.size 99
[ "$("$CONF" read "$DOM" theme.size)" = "11" ] \
	&& ok "user -u write does not shadow the System value" \
	|| bad "user -u write shadows the System value"

# merged whole-domain read annotates the winning tier per value
OUT=$("$CONF" read "$DOM")
echo "$OUT" | grep -q '^theme.size = 11 (system)$' \
	&& ok "whole-domain read annotates (system)" \
	|| bad "whole-domain read annotates (system)"
echo "$OUT" | grep -q '^theme.color = blue (user)$' \
	&& ok "whole-domain read annotates (user)" \
	|| bad "whole-domain read annotates (user)"
echo "$OUT" | grep -q '^widget.alpha = true (shared)$' \
	&& ok "whole-domain read annotates (shared)" \
	|| bad "whole-domain read annotates (shared)"

# scoped reads are unchanged (no annotation)
[ "$("$CONF" read -s "$DOM" theme.size)" = "11" ] \
	&& [ "$("$CONF" read -u "$DOM" theme.size)" = "99" ] \
	&& ok "scoped reads stay raw and scope-exact" \
	|| bad "scoped reads stay raw and scope-exact"

[ "$fails" -eq 0 ] && echo "M1 CLI: ALL PASS" || echo "M1 CLI: $fails FAILURE(S)"
exit "$fails"
