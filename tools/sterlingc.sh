#!/bin/sh
#
# sterlingc.sh — the Sterling compiler driver (K1).
#
# Copyright © 2026 Kyle J. Cardoza. MIT licence (see docs/LICENSE).
#
#   sterlingc.sh --build           build the compiler itself
#   sterlingc.sh <file.ag> [-o D]  compile one file to <Class>.h and <Class>.m
#   sterlingc.sh --golden          K1's host gate: the §1 specimen must emit
#                                  exactly what sterling-syntax.md §2 shows
#   sterlingc.sh --corpus          lex every file in tests/ and report
#
# The compiler is built with the tree's pinned clang (mk/00-base.mk's CLANG19),
# never with a bare `clang` — the host has no unversioned one on PATH.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC="$ROOT/tools/sterlingc"
OUT="$ROOT/.build/sterlingc"
CLANG19=${CLANG19:-/usr/lib/llvm-19/bin/clang}
SYNTAX="$ROOT/docs/design/sterling-syntax.md"

build() {
	mkdir -p "$OUT"
	# shellcheck disable=SC2086
	"$CLANG19" -std=c99 -Wall -Wextra -Wno-unused-parameter \
		-I"$SRC" -o "$OUT/sterlingc" \
		"$SRC/lexer.c" "$SRC/parser.c" "$SRC/check.c" \
		"$SRC/emit.c" "$SRC/emit_main.c"
	"$CLANG19" -std=c99 -Wall -Wextra -Wno-unused-parameter \
		-I"$SRC" -o "$OUT/lexdump" "$SRC/lexer.c" "$SRC/main.c"
}

# §2's two blocks, by line: the .h is 56..71 and the .m is 77..100. Extracting
# by line rather than by fence keeps the check independent of the prose around
# them, and it fails loudly if the document moves.
golden() {
	build
	mkdir -p "$OUT/emitted"
	"$OUT/sterlingc" -o "$OUT/emitted" >/dev/null
	sed -n '56,71p' "$SYNTAX" > "$OUT/doc.h"
	sed -n '77,100p' "$SYNTAX" > "$OUT/doc.m"
	rc=0
	if ! diff -q "$OUT/doc.h" "$OUT/emitted/MyClass.h" >/dev/null; then
		echo "GOLDEN-FAIL: emitted MyClass.h differs from §2"
		diff -u "$OUT/doc.h" "$OUT/emitted/MyClass.h" || true
		rc=1
	fi
	if ! diff -q "$OUT/doc.m" "$OUT/emitted/MyClass.m" >/dev/null; then
		echo "GOLDEN-FAIL: emitted MyClass.m differs from §2"
		diff -u "$OUT/doc.m" "$OUT/emitted/MyClass.m" || true
		rc=1
	fi
	[ "$rc" -eq 0 ] && echo "GOLDEN-OK the specimen emits §2 byte-for-byte"
	return "$rc"
}

# Every corpus file must at least lex. A lex error ends the token stream, so a
# failure here also means the rest of the file was never looked at.
corpus() {
	build
	fail=0
	parsed=0
	total=0
	for f in "$SRC"/tests/*.ag; do
		out=$("$OUT/lexdump" "$f" 2>&1)
		n=$(printf '%s\n' "$out" | wc -l | tr -d ' ')
		e=$(printf '%s\n' "$out" | grep -c '^ERROR' || true)
		# Parsing is reported, not required. The parser is §1-scoped and the
		# corpus is the map of what it has yet to learn, so this count is the
		# progress bar; it becomes a gate when it reaches every file.
		total=$((total + 1))
		if "$OUT/sterlingc" "$f" >/dev/null 2>&1; then
			state="parses"
			parsed=$((parsed + 1))
		else
			state="--"
		fi
		printf '%-40s tokens=%-5s lex=%-3s %s\n' "$(basename "$f")" "$n" "$e" "$state"
		if [ "$e" != "0" ]; then
			printf '%s\n' "$out" | grep '^ERROR' | head -3
			fail=1
		fi
	done
	[ "$fail" -eq 0 ] && echo "CORPUS-OK every file lexes clean"
	echo "CORPUS-PARSE $parsed of $total files parse"
	return "$fail"
}

# The negative half. Every file in tests/reject/ must be **rejected**, and each
# is named for the rule it breaks.
#
# A rule the parser enforces needs two tests: a corpus file that uses the form
# and passes, and one here that breaks it and fails. Without this half a
# permissive parser is indistinguishable from a correct one — which is exactly
# how six scanned declaration forms came to report "9 of 9" while accepting any
# bytes at all. The rule-messages are printed, not just a count, because the
# *message* is part of what is being checked: a rejection for the wrong reason
# is not a pass.
reject() {
	build
	fail=0
	total=0
	# An empty or missing directory would leave the glob literal, hand a
	# nonexistent path to the compiler, and count *that* as a rejection — a
	# silent false pass in the check written to prevent them. So the count is
	# asserted rather than assumed.
	if [ ! -d "$SRC/tests/reject" ] ||
	   [ -z "$(ls "$SRC"/tests/reject/*.ag 2>/dev/null)" ]; then
		echo "REJECT-FAIL no cases in $SRC/tests/reject" >&2
		return 1
	fi
	for f in "$SRC"/tests/reject/*.ag; do
		total=$((total + 1))
		msg=$("$OUT/sterlingc" "$f" 2>&1 >/dev/null | head -1)
		if [ -z "$msg" ]; then
			printf '%-46s ACCEPTED — should have been rejected\n' \
				"$(basename "$f")"
			fail=1
		else
			printf '%-46s %s\n' "$(basename "$f")" "$msg"
		fi
	done
	if [ "$fail" -eq 0 ]; then
		echo "REJECT-OK all $total files rejected"
	fi
	return "$fail"
}

case "${1:-}" in
--build)	build && echo "built $OUT/sterlingc" ;;
--golden)	golden ;;
--corpus)	corpus ;;
--reject)	reject ;;
"")		echo "usage: sterlingc.sh --build | --golden | --corpus | --reject | <file.ag> [-o DIR]" >&2
		exit 2 ;;
*)		build
		# No shift: "$@" still holds the file name. Shifting it away made
		# the driver compile the built-in specimen instead, which is how
		# the literal-free crash was reached in the first place.
		"$OUT/sterlingc" "$@" ;;
esac
