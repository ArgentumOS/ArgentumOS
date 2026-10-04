# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSFileProviderService — W8 slice 9's acceptance (foundation-plan.md §60).

The class's subject is a subsystem this system does not have: a File Provider extension, reached through XPC.
So the probe's whole job is to check that the ABSENCE is ANSWERED rather than hidden — an empty dictionary
from the door Apple publishes FOR LOOKING (which lives on NSFileManager, and Apple's own service page has
only two members), and a connection door that REFUSES BY NAME, because "the custom communication channel" is
an NSXPCConnection and this system has no XPC.

The one object a caller can actually hold here is one it made itself, since the lookup never returns one —
so `-name` answers an empty string for a service nothing named, and both facts are asserted, which is what
keeps this class from being declarations no check can reach.

The probe is `/System/Shared/tests/foundation_fileproviderservice`, ONE unit, importing only
`<Foundation/Foundation.h>`. NO FIXTURE AND NO TREE: there is nothing to build for a subsystem that is not
there.

  * `provider-the-lookup-answers-an-empty-dictionary` — the answer is empty AND the handler IS CALLED: "no
                                 file provider service is registered" is exactly what an extensionless
                                 system can say;
  * `provider-the-lookup-answers-for-a-missing-item-too` — the answer does not depend on the item existing,
                                 because the reason there is no service is not the item;
  * `provider-a-service-can-be-made-and-its-name-is-empty` — the only object a caller can hold is one it
                                 made, and its name is empty rather than missing;
  * `provider-the-connection-is-refused-by-name` — the door answers nil WITH an error (ENOTSUP), which is
                                 why the class is worth declaring at all: a program can ASK and find out.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_fileproviderservice"
CHECKS = ("provider-the-lookup-answers-an-empty-dictionary",
          "provider-the-lookup-answers-for-a-missing-item-too",
          "provider-a-service-can-be-made-and-its-name-is-empty",
          "provider-the-connection-is-refused-by-name")


class Case(BaseCase):
    title = "NSFileProviderService: the lookup is empty and the connection is refused by name"
    tier = "fast"
    # No fixture and no file system work: it can share a guest.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_fileproviderservice")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-FILEPROVIDERSERVICE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-FILEPROVIDERSERVICE "):
                self.note(line)

        done = "FOUNDATION-FILEPROVIDERSERVICE DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-FILEPROVIDERSERVICE DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-FILEPROVIDERSERVICE %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-FILEPROVIDERSERVICE RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-FILEPROVIDERSERVICE-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
