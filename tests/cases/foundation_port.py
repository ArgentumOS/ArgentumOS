# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSPort / NSSocketPort — W6b's acceptance.

docs/design/foundation-plan.md §43. A port is a COMMUNICATION ENDPOINT, and on this system it is a
descriptor the run loop watches: that is the half of Apple's NSPort that §11.5 did not strike. The
message type (`NSPortMessage`), the delegate protocol (`NSPortDelegate`), the connection class
(`NSConnection`) and the two other concrete subclasses (`NSMachPort`, `NSMessagePort`) are all
Apple-deprecated and excluded BY NAME, so a scheduled port's readiness goes to `-portDidBecomeReadable`,
which is OURS.

The probe is `/System/Shared/tests/foundation_port`, ONE unit, importing
`<Foundation/Foundation.h>` plus the socket headers — because a port IS a socket, and the checks talk to
the kernel on the other side of one (`getsockname(2)`, `accept(2)`, `fcntl(2)`).

  * `port-valid-until-invalidated`   — valid, then invalid, and a SECOND `-invalidate` changes nothing;
  * `port-invalidation-notification` — the notification's name AND its object (the port itself);
  * `socketport-local-init`          — `-init` binds a real AF_INET/SOCK_STREAM socket on an EPHEMERAL port;
  * `socketport-tcp-port-binds`      — `-initWithTCPPort:`, then `getsockname(2)` on `-socket`: the KERNEL
                                       is asked what was bound, so this cannot agree with a wrong accessor;
  * `socketport-remote-connects`     — a full loopback round trip: `accept(2)` on the server, `write(2)` on
                                       the client, `read(2)` the byte back — plus the peer port `-address`
                                       reports for a REMOTE port;
  * `socketport-wraps-a-descriptor`  — a descriptor the CALLER made is adopted and NOT owned: still open
                                       after `-invalidate` (NSFileHandle's rule, on a socket);
  * `socketport-closes-what-it-owns` — a descriptor the port CREATED is closed (`fcntl(2)` → EBADF);
  * `port-schedule-fires-on-readiness` — THE SEAM, through a port: silent with nobody connected, and told
                                       the moment one is (a pair, both halves required);
  * `port-remove-from-runloop`       — after `-removeFromRunLoop:forMode:` the same readiness says nothing;
  * `port-source-respects-the-mode`  — a port scheduled in one mode is NOT fired by a pass in another;
  * `port-archiving-refused`         — Apple documents "coding only by an NSPortCoder"; the refusal is
                                       checked rather than asserted in prose.

The probe binds a fixed port (45678) for the TCP checks and uses ephemeral ports everywhere else, so a
reused guest does not collide with itself.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_port"
CHECKS = (
    "port-valid-until-invalidated", "port-invalidation-notification",
    "socketport-local-init", "socketport-tcp-port-binds", "socketport-remote-connects",
    "socketport-wraps-a-descriptor", "socketport-closes-what-it-owns",
    "port-schedule-fires-on-readiness", "port-remove-from-runloop",
    "port-source-respects-the-mode", "port-archiving-refused",
)


class Case(BaseCase):
    title = "NSPort / NSSocketPort: a port is a descriptor the run loop watches"
    tier = "fast"
    # It runs a probe and reads its output, and the probe invalidates every port it makes (which closes
    # the sockets it created), so a reused guest answers the same.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_port")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-PORT-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-PORT "):
                self.note(line)

        done = "FOUNDATION-PORT DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-PORT DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-PORT %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-PORT RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-PORT-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
