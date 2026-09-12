"""The desktop comes up at every supported guest RAM size.

The physical memory map, the boot-structures window and the direct map have all
regressed here before, and they regress in a way that only one size shows: a
build that boots at 256M can die inside start_kernel() at 1G printing nothing
at all, because the console does not exist yet.

So this case boots each size and asks only for two things: the desktop, and the
absence of fatal faults.  It is the regression guard for the memory work, and it
is deliberately in the slow tier - it boots one guest per size.

FNX_TEST_MEM_LIST overrides the sizes, e.g. FNX_TEST_MEM_LIST="256M 4G".
"""

import os
import re

from harness import BaseCase

DEFAULT_SIZES = "256M 1G 2G 4G 8G"
FATAL = r"KERNEL EXCEPTION|cannot map the page|Page Fault at 0x"


class Case(BaseCase):
    title = "every supported guest RAM size boots to a drawn desktop"
    tier = "slow"
    timeout = 1800

    def run(self, ctx):
        sizes = os.environ.get("FNX_TEST_MEM_LIST", DEFAULT_SIZES).split()
        for size in sizes:
            session = ctx.boot(name="mem-%s" % size, mem=size)
            up = session.wait_for(r"KESTREL-READY", 150)
            log = session.log_text()
            faults = len(re.findall(FATAL, log))

            self.check("desktop-%s" % size, up, "%s of guest RAM" % size)
            self.check("no-faults-%s" % size, faults == 0,
                       "%d fatal fault line(s)" % faults)
            self.note("%s: %s" % (size, self.find(
                log, r"(total=\d+KB, user=\d+KB)", str, "no memory report")))

            # Stop each guest before the next one: they share one image.
            session.stop()
