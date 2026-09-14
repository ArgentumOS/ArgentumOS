"""Weaver multi-document: ONE editor process holds two documents in two
windows (the in-process refinement of the IB7 two-process fallback).

`--open` accumulates: each new open pushes the previous document into a
background list, and `--show` builds one window (with the same palette /
outline / canvas / inspector chrome) for each. The gate asserts two window
logs with two different doc paths, two layout logs, and two WM manage lines —
all from a single process.
"""

import re
import time

from harness import BaseCase

WEAVER = "/Applications/Weaver.app/bin/Weaver"
FIXTURE = "/System/Shared/tests/weaver_ib2.conf"
DOC1 = "/Users/Admin/Documents/weaver_multi1.conf"
DOC2 = "/Users/Admin/Documents/weaver_multi2.conf"

MANAGE = r"KESTREL: manage 0x[0-9a-f]+ 'Weaver' frame=0x[0-9a-f]+"


class Case(BaseCase):
    title = "Weaver multi-document: one process, two windows, two documents"
    tier = "fast"
    timeout = 420

    def run(self, ctx):
        ctx.require_guest_file("Weaver")
        session = ctx.boot_to_desktop(secs=150)
        ready = session.shell_ready(90)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("cp %s %s && cp %s %s && echo DOCS-COPIED"
                    % (FIXTURE, DOC1, FIXTURE, DOC2))
        out = session.output_since(mark)
        self.check("fixtures-staged", "DOCS-COPIED" in out,
                   "both fixtures are in the user's Documents")

        mark = len(session.log_text())
        session.run("DISPLAY=:0 %s --open weaver_multi1.conf "
                    "--open weaver_multi2.conf --show &" % WEAVER)

        deadline = time.time() + 45
        log = session.log_text()[mark:]
        while time.time() < deadline:
            if (("doc=%s" % DOC1) in log
                    and ("doc=%s" % DOC2) in log
                    and len(re.findall(MANAGE, log)) >= 2):
                break
            time.sleep(0.5)
            log = session.log_text()[mark:]

        self.check("two-window-logs",
                   log.count("WEAVER: window") >= 2
                   and ("doc=%s" % DOC1) in log
                   and ("doc=%s" % DOC2) in log,
                   "one process logged two windows with two different docs")
        self.check("two-layout-logs",
                   log.count("WEAVER: layout ") >= 2,
                   "both windows laid out their chrome")
        self.check("two-managed-windows",
                   len(re.findall(MANAGE, log)) >= 2,
                   "the WM manages two editor windows")

        session.run("killall Weaver")
