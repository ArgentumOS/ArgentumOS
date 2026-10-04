#!/bin/sh
# foundation-unit.sh — the MECHANICAL half of a Foundation unit: verify, then optionally commit.
#
# WHY THIS EXISTS: a Foundation unit is edit -> sweeps -> build -> guest gate -> commit, and doing those by
# hand costs one agent round each. The judgement (which ROWS, and why) stays with the author; this script owns
# the part that is the same every time, so a round is spent writing a door rather than re-deriving commands.
#
# USAGE
#   tools/foundation-unit.sh <probe> [probe...]        verify only
#   tools/foundation-unit.sh -c <message-file> <probe> [...]   verify, then commit if green
#
# WHAT IT CHECKS, IN THE ORDER THE TRAPS DEMAND
#   1. --unimplemented   declarations with no definition  (key on the TEXT a patch adds, never a bare name)
#   2. --check           definitions the owner does not declare
#      These two read OPPOSITE directions (see §63.204); a unit that passes one and fails the other is the
#      classic half-applied edit.
#   3. make testimg      the build, with BOTH sweeps folded in (mk/00-base.mk 410/411)
#   4. make test TESTS=  the guest gates, in ONE boot (the harness shares a session)
#   5. commit            only with -c, only if 1-4 are green, never with `git add -A`
#
# EXIT: 0 green (and committed, with -c); 1 something failed (nothing committed).

set -e

MSG=""
if [ "$1" = "-c" ]; then
	MSG="$2"
	shift 2
	if [ ! -f "$MSG" ]; then
		echo "foundation-unit: no such message file: $MSG" >&2
		exit 2
	fi
fi
if [ $# -eq 0 ]; then
	echo "usage: tools/foundation-unit.sh [-c <message-file>] <probe> [probe...]" >&2
	exit 2
fi
PROBES=$(echo "$*" | tr " " ",")	# the harness wants them COMMA-separated (only=a,b)
LOG=${TMPDIR:-/tmp}/foundation-unit.log

step() { printf '  %-22s ' "$1"; }
ok()   { echo "ok"; }
bad()  { echo "FAILED"; echo; tail -"${2:-8}" "$LOG" | cut -c1-160; echo; echo "foundation-unit: $1 failed; nothing committed"; exit 1; }

echo "foundation-unit: probes: $PROBES"

step "sweep --unimplemented"
python3 tools/foundation-sweep.py --unimplemented > "$LOG" 2>&1 || bad "sweep --unimplemented"
grep -q "NEW, and this mode fails on them: 0" "$LOG" || bad "sweep --unimplemented" 12
ok

step "sweep --check"
python3 tools/foundation-sweep.py --check > "$LOG" 2>&1 || bad "sweep --check"
ok

step "families (regenerate)"
python3 tools/foundation-sweep.py --families --write > "$LOG" 2>&1 || bad "families"
ok

step "make testimg"
if make testimg > "$LOG" 2>&1; then ok; else bad "make testimg" 20; fi

step "guest gates"
if make test TESTS="$PROBES" > "$LOG" 2>&1; then ok; else bad "guest gates" 25; fi
grep -hE "^TESTS-(OK|FAIL)" "$LOG" | tail -1 | sed 's/^/    /'

if [ -n "$MSG" ]; then
	step "commit"
	git add -u
	git commit -q -F "$MSG" || bad "commit"
	git log --oneline -1 | sed 's/^/    /'
	ok
fi

echo "foundation-unit: green"
