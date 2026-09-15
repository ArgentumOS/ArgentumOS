"""Weaver W2: connections — record one, list it, and dispatch it.

The document's connections table (W0's format) gains its semantics: the
scripted --connect records an action connection, the inspector lists the
selection's connections, save/reload/roundtrip keep them, and the
display-free dispatcher probe proves a connection RUNS the app's binding
while an unbound selector is refused (D4/D6).
"""

from harness import BaseCase

WEAVER = "/Applications/Weaver.app/bin/Weaver"
PROBE = "/System/Shared/tests/interface_dispatch"
FIXTURE = "/System/Shared/tests/weaver_ib2.conf"
DOC = "/Users/Admin/Documents/weaver_w2.conf"


class Case(BaseCase):
    title = "Weaver W2: connections record, list, round-trip and dispatch"
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
        session.run("test -x %s && %s; echo W2-EXIT=$?" % (PROBE, PROBE))
        out = session.output_since(mark)
        self.check("probe-ran", "W2-OK" in out,
                   "the dispatcher probe ran and dispatched")
        self.check("probe-exit-zero", "W2-EXIT=0" in out,
                   "the probe exited 0")

        mark = len(session.log_text())
        session.run("cp %s %s && rm -f %s.weaverundo && echo DOC-COPIED"
                    % (FIXTURE, DOC, DOC))
        out = session.output_since(mark)
        self.check("fixture-staged", "DOC-COPIED" in out,
                   "the fixture is in %s" % DOC)

        mark = len(session.log_text())
        session.run("%s --open weaver_w2.conf --click 65 72 "
                    "--connect okButton action doThing owner --inspect "
                    "--save --reload --click 65 72 --inspect --roundtrip; "
                    "echo WEAVER-EXIT=$?" % WEAVER)
        out = session.output_since(mark)
        for line in out.strip().splitlines():
            if "WEAVER" in line:
                self.note(line)

        self.check("connect-logged",
                   "WEAVER: connect okButton action doThing owner" in out,
                   "the connection was recorded")
        self.check("connection-listed",
                   "WEAVER: connection okButton action doThing owner" in out,
                   "the inspector listed the selection's connection")
        self.check("save-logged", "WEAVER: save " in out,
                   "the connected document was saved")
        self.check("reload-keeps-doc",
                   "WEAVER: reload /Users/Admin/Documents/weaver_w2.conf "
                   "(3 nodes)" in out,
                   "the saved document reloaded")
        # the connection must survive the SAVE: inspect again after the
        # reload and require a SECOND connection line
        tail = out.split("WEAVER: reload", 1)[-1]
        self.check("connection-survives-save",
                   "WEAVER: connection okButton action doThing owner"
                   in tail,
                   "after reload the inspector still lists the connection")
        self.check("roundtrip-ok",
                   "WEAVER: roundtrip /Users/Admin/Documents/weaver_w2.conf "
                   "OK" in out,
                   "the connection survived the emitter")
        self.check("exit-zero", "WEAVER-EXIT=0" in out,
                   "the run exited 0")

        # a connection naming a node that is not in the document is refused
        mark = len(session.log_text())
        session.run("%s --open weaver_w2.conf --connect nosuch action x "
                    "owner; echo WEAVER-EXIT=$?" % WEAVER)
        out = session.output_since(mark)
        self.check("unknown-source-refused",
                   "WEAVER: connect FAIL no `nosuch`" in out
                   and "WEAVER-EXIT=1" in out,
                   "a connection to an unknown source failed loudly")
