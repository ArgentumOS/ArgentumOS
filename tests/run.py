#!/usr/bin/env python3
"""The FNX test harness: boot the assembled OS and assert on what it does.

    make test                    the fast tier (a few minutes)
    make test-all                fast + slow (the slow tier boots many guests)
    make test T=audio            one case; globs work: T='wm_*'
    make test-list               what exists, with tiers and timeouts

Environment knobs (see tests/README.md): FNX_TEST_MEM, FNX_TEST_ROOTIMG,
FNX_TEST_AUDIODEV, FNX_TEST_MACHINE, FNX_TEST_TIER, FNX_TEST_ALLOW_OTHER_QEMU.

Cases live in tests/cases/*.py; the machinery they share lives in
tests/harness/.  Artifacts (guest logs, screenshots) land in .build/tests/.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from harness import runner  # noqa: E402  (after sys.path)

if __name__ == "__main__":
    sys.exit(runner.main())
