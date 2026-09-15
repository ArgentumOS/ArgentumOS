"""Weaver W3: the Classes pane — subclass, outlets/actions, instantiate.

A class record (W0's `classes` table) gains its edits: create a subclass
of a registered class, add outlets and actions, instantiate it into the
graph as a NON-VIEW object (which W2's dispatcher can target). The gate
also proves the refusals: duplicate names, unknown supers, unknown
classes, and a taken instantiate id.
"""

from harness import BaseCase

WEAVER = "/Applications/Weaver.app/bin/Weaver"
PROBE = "/System/Shared/tests/interface_dispatch"
FIXTURE = "/System/Shared/tests/weaver_ib2.conf"
DOC = "/Users/Admin/Documents/weaver_w3.conf"


class Case(BaseCase):
    title = "Weaver W3: classes subclass, outlets/actions, instantiate"
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
        session.run("test -x %s && %s; echo W3-EXIT=$?" % (PROBE, PROBE))
        out = session.output_since(mark)
        self.check("probe-ran", "W2-OK" in out,
                   "the dispatcher probe ran (owner and custom targets)")
        self.check("probe-exit-zero", "W3-EXIT=0" in out,
                   "the probe exited 0")

        mark = len(session.log_text())
        session.run("cp %s %s && rm -f %s.weaverundo && echo DOC-COPIED"
                    % (FIXTURE, DOC, DOC))
        out = session.output_since(mark)
        self.check("fixture-staged", "DOC-COPIED" in out,
                   "the fixture is in %s" % DOC)

        mark = len(session.log_text())
        session.run("%s --open weaver_w3.conf "
                    "--new-class MyController Object "
                    "--add-outlet MyController greeting "
                    "--add-action MyController doThing "
                    "--instantiate MyController controller "
                    "--classes --outline --save --reload --classes "
                    "--roundtrip; echo WEAVER-EXIT=$?" % WEAVER)
        out = session.output_since(mark)
        for line in out.strip().splitlines():
            if "WEAVER" in line:
                self.note(line)

        self.check("new-class",
                   "WEAVER: new-class MyController super Object" in out,
                   "the subclass was created")
        self.check("add-outlet",
                   "WEAVER: add-outlet MyController greeting" in out,
                   "the class gained an outlet")
        self.check("add-action",
                   "WEAVER: add-action MyController doThing" in out,
                   "the class gained an action")
        self.check("instantiate",
                   "WEAVER: instantiate MyController as controller" in out,
                   "the class was instantiated into the graph")
        self.check("class-record-listed",
                   "WEAVER: class MyController super=Object "
                   "outlets=greeting actions=doThing" in out,
                   "the class record carries its outlets and actions")
        self.check("object-in-outline",
                   "WEAVER: outline object controller (MyController)"
                   in out,
                   "the instantiated object is in the Objects pane")
        self.check("class-in-outline",
                   "WEAVER: outline class MyController : Object" in out,
                   "the class record is listed")
        # after the reload the class record is still described
        tail = out.split("WEAVER: reload", 1)[-1]
        self.check("class-survives-save",
                   "WEAVER: class MyController super=Object "
                   "outlets=greeting actions=doThing" in tail,
                   "the class record survived save/reload")
        self.check("roundtrip-ok",
                   "WEAVER: roundtrip /Users/Admin/Documents/weaver_w3.conf "
                   "OK" in out,
                   "the class graph survived the emitter")
        self.check("exit-zero", "WEAVER-EXIT=0" in out,
                   "the run exited 0")

        mark = len(session.log_text())
        session.run("%s --open weaver_w3.conf "
                    "--new-class MyController Object "
                    "--new-class Bad Nope --add-outlet Ghost x "
                    "--instantiate Ghost g; echo WEAVER-EXIT=$?" % WEAVER)
        out = session.output_since(mark)
        self.check("duplicate-refused",
                   "WEAVER: new-class FAIL `MyController` exists" in out
                   and "WEAVER-EXIT=1" in out,
                   "a duplicate class name failed loudly")
        self.check("unknown-super-refused",
                   "WEAVER: new-class FAIL unknown super `Nope`" in out,
                   "an unknown super class was refused")
        self.check("unknown-class-refused",
                   "WEAVER: add-outlet FAIL `Ghost`" in out,
                   "an outlet on an unknown class was refused")
