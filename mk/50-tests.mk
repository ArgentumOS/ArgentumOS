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

# ---- Sterling's compiler (K1) -------------------------------------------
#
# K1's gate, runnable on the host because libobjc2 is now built here too — see
# tools/sterlingc-compile.sh's header for how. What remains for the guest is
# *running* what was compiled: the probe's checks are calls, and the emitter
# cannot emit calls yet.
.PHONY: sterlingc-check sterlingc-golden sterlingc-corpus sterlingc-reject \
	sterlingc-compile

sterlingc-check: sterlingc-golden sterlingc-corpus sterlingc-reject \
	sterlingc-compile

# The §1 specimen must emit sterling-syntax.md §2 byte-for-byte.
sterlingc-golden:
	@tools/sterlingc.sh --golden

# Every corpus file must lex. A lex error is not a cosmetic failure: it ends
# the token stream, so everything after it goes unchecked.
sterlingc-corpus:
	@tools/sterlingc.sh --corpus

# The negative half, and the one that keeps the others honest: every file in
# tests/reject/ must be *rejected*, and each is named for the rule it breaks.
# A rule the parser enforces needs a test in both directions — without this a
# permissive parser is indistinguishable from a correct one, which is exactly
# how six scanned declaration forms reported "9 of 9" while accepting any bytes
# at all, and how a `?.` branch came to exist for a construct §9.6 forbids.
sterlingc-reject:
	@tools/sterlingc.sh --reject

# The emitted .m must *compile* under -fobjc-arc against libobjc2. This is the
# gate that catches a wrong emission the byte comparison cannot see: §2's own
# specimen emitted `return false;`, which does not compile, and only this check
# found it.
sterlingc-compile:
	@tools/sterlingc-compile.sh
