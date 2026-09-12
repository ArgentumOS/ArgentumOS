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
