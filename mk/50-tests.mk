# The test harness (tests/) - `make test`.
#
# `make test` boots the assembled OS under QEMU and asserts on what it does:
# what the guest logs, what it draws, and how it answers input.  It tests the
# images that are ON DISK (the runner warns when .build/rootagfs.img is older
# than the sources it was built from); it does not rebuild them, because a
# `rootagfs` dependency would re-run the whole userland build on every check.
#
#   make test                 the fast tier (a few minutes)
#   make test-all             fast + slow (boots one guest per case; ~20 min)
#   make test TESTS=audio     one case, or a glob: TESTS='wm_*'
#   make test-list            the cases, their tiers and their timeouts
#
# FNX_TEST_TIER=slow turns `make test` into the slow tier; see tests/README.md
# for the other environment knobs (RAM size, root image, audio backend).

TESTS ?=

.PHONY: test test-all test-list

# OVMF is the one input that is safe to fetch here: it is a single download and
# it cannot make the images stale.  Everything else is checked, not rebuilt.
test: .build/ovmf/OVMF.fd
	@python3 tests/run.py --only '$(TESTS)'

test-all: .build/ovmf/OVMF.fd
	@python3 tests/run.py --tier all --only '$(TESTS)'

test-list:
	@python3 tests/run.py --list

# ---- Sterling's compiler (K2) -------------------------------------------
#
# The host legs. They run here because libobjc2 is built on the host too — see
# tools/sterlingc-compile.sh's header for how.
#
# The guest half is `make test TESTS=sterlingc_k1`: K1's chain, exercised by
# linking the compiler's own output with a hand-written driver. That leg stays
# K1's; the statement/expression core is host-only, and the driver it will need
# is the Sterling probe the emitter cannot write yet.
.PHONY: sterlingc-check sterlingc-golden sterlingc-corpus sterlingc-reject \
	sterlingc-refuse sterlingc-compile

sterlingc-check: sterlingc-golden sterlingc-corpus sterlingc-reject \
	sterlingc-refuse sterlingc-compile

# §1's specimen must emit sterling-syntax.md §2 byte-for-byte, AND every
# tests/golden/<Class>.ag must emit the <Class>.h/.m checked in beside it. §2
# covers no local, no assignment, no send and no operator, so without the
# specimens the whole of the core below goes unchecked; see sterlingc.sh's
# golden_specimens for why a golden is an assertion rather than a recording.
sterlingc-golden:
	@tools/sterlingc.sh --golden

# Every corpus file must lex and parse. A lex error is not a cosmetic failure:
# it ends the token stream, so everything after it goes unchecked. Parsing and
# emission are counted separately — the corpus is the MAP of the surface, so most
# of it is constructs the emitter does not have yet, and one count standing for
# both is how the parse number fell when only the emitter changed.
sterlingc-corpus:
	@tools/sterlingc.sh --corpus

# The front end's negative half, and the one that keeps the others honest: every
# file in tests/reject/ must be *rejected by the front end*, and each is named for
# the rule it breaks. A rule the parser enforces needs a test in both directions —
# without this a permissive parser is indistinguishable from a correct one, which
# is exactly how six scanned declaration forms reported "9 of 9" while accepting
# any bytes at all, and how a `?.` branch came to exist for a construct §9.6
# forbids.
sterlingc-reject:
	@tools/sterlingc.sh --reject

# The emitter's negative half: every file in tests/refuse/ must be ACCEPTED by
# the front end and refused BY NAME by the emitter. The emitter used to skip what
# it could not write, so a program compiled to source with statements silently
# missing — and two forms were worse than missing: a closure emitted `nil` and
# `x!` emitted its operand with the trap left out. Both compiled. The `.expect`
# beside each case is matched, not just the failure, because a refusal for the
# wrong reason is not a pass.
sterlingc-refuse:
	@tools/sterlingc.sh --refuse

# The emitted .m must *compile* under -fobjc-arc against libobjc2 — the specimen
# and every golden. This is the gate that catches a wrong emission the byte
# comparison cannot see: §2's own specimen emitted `return false;`, which does
# not compile, and only this check found it. A diff proves the text is what was
# expected; it does not prove what was expected was C.
sterlingc-compile:
	@tools/sterlingc-compile.sh
