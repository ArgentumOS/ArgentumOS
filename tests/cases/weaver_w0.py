"""Weaver W0: the object-graph document (proxies/objects/connections/classes).

The v2 document adds the GORM-style graph to the interface tree: three
proxy identifier records, non-view objects, a connections table and a
classes table. The gate runs the display-free round-trip probe and then
drives Weaver's own --new/--roundtrip/--outline path.
"""

from harness import BaseCase

WEAVER = "/Applications/Weaver.app/bin/Weaver"
PROBE = "/System/Shared/tests/interface_v2"


class Case(BaseCase):
    title = "Weaver W0: the v2 object-graph document round-trips"
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
        session.run("test -x %s && %s; echo V2-EXIT=$?" % (PROBE, PROBE))
        out = session.output_since(mark)
        self.check("probe-ran", "V2-OK" in out,
                   "the v2 probe ran and its graph round-tripped")
        self.check("probe-exit-zero", "V2-EXIT=0" in out,
                   "the probe exited 0")

        mark = len(session.log_text())
        session.run("%s --new weaver_w0.conf --roundtrip --outline; "
                    "echo WEAVER-EXIT=$?" % WEAVER)
        out = session.output_since(mark)
        for line in out.strip().splitlines():
            if "WEAVER" in line:
                self.note(line)

        self.check("new-v2-document",
                   "WEAVER: new /Users/Admin/Documents/weaver_w0.conf "
                   "from template (3 nodes)" in out,
                   "a new document was created from the template")
        self.check("roundtrip-ok",
                   "WEAVER: roundtrip /Users/Admin/Documents/weaver_w0.conf "
                   "OK" in out,
                   "the v2 document survived its own emitter")
        self.check("proxies-in-outline",
                   "WEAVER: outline proxy owner" in out
                   and "WEAVER: outline proxy firstResponder" in out
                   and "WEAVER: outline proxy fontManager" in out,
                   "the outline lists the three proxy records")
        self.check("exit-zero", "WEAVER-EXIT=0" in out,
                   "the Weaver run exited 0")
