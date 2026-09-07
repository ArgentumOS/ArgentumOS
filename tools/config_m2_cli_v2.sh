#!/bin/sh
# CV2-M2 CLI: surgical bracket-path write + nested read round-trip
# (docs config-v2-plan.md CV2-M2). Runs against a scratch FNX_CONFIG_ROOT.
set -e
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
export FNX_CONFIG_ROOT="$TMP"
mkdir -p "$TMP/System/Configuration"
CFG="$TMP/config"
cc -Iinclude -Iuserland -o "$CFG" userland/tools/config.c userland/libconfig.c

cat > "$TMP/System/Configuration/fonts.conf" <<'XEOF'
cachedir = "/System/Variable Data/fontconfig"
rules = [
  {
    match = pattern
    edits = [
      {
        object = family
        mode = assign
        value = "DejaVu Sans"
      }
    ]
  }
]
XEOF

# surgical edit via a bracket key must persist and be readable
"$CFG" write -s fonts rules[0].edits[0].value "DejaVu Serif Bold"
out=$("$CFG" read -s fonts rules[0].edits[0].value)
[ "$out" = "DejaVu Serif Bold" ] || { echo "FAIL: surgical write/read"; exit 1; }

# a plain flat key still works in the same domain
"$CFG" write -s fonts rescan 1
[ "$("$CFG" read -s fonts rescan)" = "1" ] || { echo "FAIL: flat key"; exit 1; }

# nested read prints the tree (contains the new value)
"$CFG" read -s fonts rules | grep -q "DejaVu Serif Bold" || {
	echo "FAIL: nested read"; exit 1; }

# rewrite stability: second identical write keeps bytes
cp "$TMP/System/Configuration/fonts.conf" "$TMP/a.txt"
"$CFG" write -s fonts rules[0].edits[0].value "DejaVu Serif Bold"
cmp -s "$TMP/System/Configuration/fonts.conf" "$TMP/a.txt" || {
	echo "FAIL: not byte-stable after identical write"; exit 1; }

echo "M2 CLI v2: ALL PASS"
