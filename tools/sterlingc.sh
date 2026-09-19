#!/bin/sh
#
# sterlingc.sh — the Sterling compiler driver.
#
# Copyright © 2026 Kyle J. Cardoza. MIT licence (see docs/LICENSE).
#
#   sterlingc.sh --build           build the compiler itself
#   sterlingc.sh <file.ag> [-o D]  compile one file to <Class>.h and <Class>.m
#   sterlingc.sh --parse <file.ag> stop after the front end; emit nothing
#   sterlingc.sh --golden          K1's gate: the §1 specimen must emit exactly
#                                  what sterling-syntax.md §2 shows, AND every
#                                  tests/golden/<Class>.ag must emit the
#                                  <Class>.h/.m checked in beside it
#   sterlingc.sh --corpus          lex and parse every file in tests/ and report
#   sterlingc.sh --reject          every file in tests/reject/ must be rejected
#                                  by the FRONT END
#   sterlingc.sh --refuse          every file in tests/refuse/ must be ACCEPTED
#                                  by the front end and refused BY NAME by the
#                                  emitter
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

# The golden specimens: every tests/golden/<Class>.ag must emit <Class>.h and
# <Class>.m byte-for-byte as checked in beside it.
#
# This is the emitter's coverage, and it exists because §2's specimen covers
# almost none of it — no local, no assignment, no send, no operator. A golden is
# also the only way to see the *shape* of the output: `} else \tif (…)` is valid C
# and visibly wrong, and only a diff against a reviewed file notices.
#
# A checked-in golden is an ASSERTION, not a recording: when one changes, read
# the diff and decide whether the emitter or the expectation is wrong. That is
# the whole value — regenerating it without reading it turns the check into a
# receipt for whatever the compiler happened to do.
golden_specimens() {
	build
	dir="$SRC/tests/golden"
	fail=0
	total=0

	# An empty or missing directory would leave the glob literal and diff
	# nothing, reporting success — a silent false pass in the check written to
	# prevent them. So the count is asserted, as in --reject.
	if [ ! -d "$dir" ] || [ -z "$(ls "$dir"/*.ag 2>/dev/null)" ]; then
		echo "GOLDEN-FAIL no specimens in $dir" >&2
		return 1
	fi
	rm -rf "$OUT/golden"
	mkdir -p "$OUT/golden"
	for f in "$dir"/*.ag; do
		base=$(basename "$f" .ag)
		total=$((total + 1))
		if ! "$OUT/sterlingc" "$f" -o "$OUT/golden" >/dev/null \
		     2>"$OUT/golden/err"; then
			printf '%-24s REFUSED: %s\n' "$base" \
				"$(head -1 "$OUT/golden/err")"
			fail=1
			continue
		fi
		for ext in h m; do
			if ! diff -q "$dir/$base.$ext" "$OUT/golden/$base.$ext" \
				>/dev/null 2>&1; then
				echo "GOLDEN-FAIL $base.$ext differs:"
				diff -u "$dir/$base.$ext" "$OUT/golden/$base.$ext" ||
					true
				fail=1
			fi
		done
		[ "$fail" -eq 0 ] && printf '%-24s matches\n' "$base"
	done
	[ "$fail" -eq 0 ] &&
		echo "GOLDEN-SPECIMENS-OK all $total specimens match byte-for-byte"
	return "$fail"
}

# Every corpus file must at least lex. A lex error ends the token stream, so a
# failure here also means the rest of the file was never looked at.
corpus() {
	build
	fail=0
	parsed=0
	emitted=0
	total=0
	for f in "$SRC"/tests/*.ag; do
		out=$("$OUT/lexdump" "$f" 2>&1)
		n=$(printf '%s\n' "$out" | wc -l | tr -d ' ')
		e=$(printf '%s\n' "$out" | grep -c '^ERROR' || true)
		# Parsing is reported, not required. The parser is §1-scoped and the
		# corpus is the map of what it has yet to learn, so this count is the
		# progress bar; it becomes a gate when it reaches every file.
		#
		# `--parse`, NOT the plain driver: the plain driver also *emits*, so
		# its exit status answers two questions at once. While the emitter
		# accepted everything they had the same answer; now that it refuses
		# by name, one answer would have been read as the other and the parse
		# count would have fallen with nothing wrong in the parser.
		total=$((total + 1))
		if "$OUT/sterlingc" --parse "$f" >/dev/null 2>&1; then
			state="parses"
			parsed=$((parsed + 1))
		else
			state="--"
		fi
		# The emitter's own count, and a progress bar rather than a gate: the
		# corpus is the *map* of the surface, so most of it is constructs the
		# emitter does not have yet. What must never happen is that this
		# number moves without `--golden` moving with it.
		if "$OUT/sterlingc" "$f" >/dev/null 2>&1; then
			emitted=$((emitted + 1))
		fi
		printf '%-40s tokens=%-5s lex=%-3s %s\n' "$(basename "$f")" "$n" "$e" "$state"
		if [ "$e" != "0" ]; then
			printf '%s\n' "$out" | grep '^ERROR' | head -3
			fail=1
		fi
	done
	[ "$fail" -eq 0 ] && echo "CORPUS-OK every file lexes clean"
	echo "CORPUS-PARSE $parsed of $total files parse"
	echo "CORPUS-EMIT $emitted of $total files emit"
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
		# `--parse`: a rejection has to be the FRONT END's. The plain driver
		# also refuses to emit, so a case that parses cleanly and is merely
		# not-yet-emittable would print a message and be counted as a
		# rejection — a false pass in the leg whose whole purpose is to make
		# a permissive front end visible.
		msg=$("$OUT/sterlingc" --parse "$f" 2>&1 >/dev/null | head -1)
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

# The emitter's negative half, and the reason this slice exists: a construct the
# emitter cannot write must STOP, by name.
#
# The version before it *skipped* every statement it could not represent, so a
# program could compile to source that had silently lost statements — the same
# failure as a scanned declaration, and the harder kind to see, because the
# output compiles and only the meaning is gone. Two of these cases are worse than
# that and are the reason the check names the construct: a closure used to emit
# `nil`, and `x!` used to emit its operand with the trap missing. Both are wrong
# answers rather than absent ones.
#
# Each case is a program the FRONT END accepts (so it is not a parser test) and
# its `.expect` names the construct. The message is matched, not just the
# failure: a refusal for the wrong reason is not a pass, the same rule --reject
# applies to a wrong rejection.
refuse() {
	build
	dir="$SRC/tests/refuse"
	fail=0
	total=0

	if [ ! -d "$dir" ] || [ -z "$(ls "$dir"/*.ag 2>/dev/null)" ]; then
		echo "REFUSE-FAIL no cases in $dir" >&2
		return 1
	fi
	# The output directory has to exist: the driver reports "cannot write" as
	# its first line otherwise, which the expectation match below would read as
	# a wrong reason. A refusal is only a refusal when the compiler got far
	# enough to make one.
	rm -rf "$OUT/refuse"
	mkdir -p "$OUT/refuse"
	for f in "$dir"/*.ag; do
		base=$(basename "$f" .ag)
		total=$((total + 1))
		if [ ! -f "$dir/$base.expect" ]; then
			printf '%-30s NO EXPECTATION (%s.expect)\n' "$base" "$base"
			fail=1
			continue
		fi
		want=$(cat "$dir/$base.expect")
		# The plain driver: this leg is exactly about what EMISSION does.
		msg=$("$OUT/sterlingc" "$f" -o "$OUT/refuse" 2>&1 >/dev/null |
			head -1)
		if [ -z "$msg" ]; then
			printf '%-30s EMITTED — should have been refused\n' "$base"
			fail=1
		elif ! printf '%s\n' "$msg" | grep -q -- "$want"; then
			printf '%-30s WRONG REASON\n  want: %s\n  got:  %s\n' \
				"$base" "$want" "$msg"
			fail=1
		else
			printf '%-30s %s\n' "$base" "$msg"
		fi
	done
	if [ "$fail" -eq 0 ]; then
		echo "REFUSE-OK all $total refused, each by name"
	fi
	return "$fail"
}

case "${1:-}" in
--build)	build && echo "built $OUT/sterlingc" ;;
--golden)	golden && golden_specimens ;;
--corpus)	corpus ;;
--reject)	reject ;;
--refuse)	refuse ;;
"")		echo "usage: sterlingc.sh --build | --golden | --corpus | --reject | --refuse | <file.ag> [-o DIR]" >&2
		exit 2 ;;
*)		build
		# No shift: "$@" still holds the file name. Shifting it away made
		# the driver compile the built-in specimen instead, which is how
		# the literal-free crash was reached in the first place.
		"$OUT/sterlingc" "$@" ;;
esac
