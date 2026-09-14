"""Weaver IB7 - templates and multiple documents (docs/design/weaver-plan.md 8).

`--new` writes a document from the BUILT-IN template (the same shape Wren
resolves: greeting + okButton), `--roundtrip` proves the new document survives
its own emitter (emit(load(emit(load))) byte-identical, the IB0 property), and
Wren booted with the document's path shows a sample app can load it. The last
clause - two editor windows hold two different documents - launches two editor
processes in show mode and reads their window logs, which now carry the
document path.
"""

import time

from harness import BaseCase

WEAVER = "/Applications/Weaver.app/bin/Weaver"
WREN = "/Applications/Wren.app/bin/Wren"
DOC1 = "/Users/Admin/Documents/weaver_ib7.conf"
DOC2 = "/Users/Admin/Documents/weaver_ib7b.conf"


class Case(BaseCase):
    title = "Weaver IB7: new-from-template round-trips, boots Wren, two windows"
    tier = "fast"
    timeout = 420

    def run(self, ctx):
        ctx.require_guest_file("Weaver")
        ctx.require_guest_file("Wren")
        session = ctx.boot_to_desktop(secs=150)
        ready = session.shell_ready(90)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        # --- new from template, and the round-trip is clean ---
        mark = len(session.log_text())
        session.run("%s --new weaver_ib7.conf --roundtrip; "
                    "echo WEAVER-EXIT=$?" % WEAVER)
        out = session.output_since(mark)
        for line in out.strip().splitlines():
            if "WEAVER" in line:
                self.note(line)

        self.check("new-from-template",
                   "WEAVER: new %s from template (3 nodes)" % DOC1 in out,
                   "the template produced a 3-node document")
        self.check("new-doc-saved",
                   "WEAVER: save %s (" % DOC1 in out,
                   "the new document was saved")
        self.check("roundtrip-clean",
                   "WEAVER: roundtrip %s OK (" % DOC1 in out,
                   "emit(load(emit(load))) is byte-identical")
        self.check("exit-zero", "WEAVER-EXIT=0" in out,
                   "the template run exited 0")

        # --- the generated document boots a sample app (Wren, user path) ---
        mark = len(session.log_text())
        session.run("%s %s; echo WREN-EXIT=$?" % (WREN, DOC1))
        out = session.output_since(mark)
        for line in out.strip().splitlines():
            if "WREN" in line:
                self.note(line)

        self.check("wren-loads-template-doc",
                   "WREN: load %s (3 nodes)" % DOC1 in out,
                   "Wren booted the template-produced document")
        self.check("wren-resolves-template-outlets",
                   "WREN: outlet greeting resolved" in out
                   and "WREN: outlet okButton resolved" in out,
                   "the template's outlets resolved by identifier")
        self.check("wren-exit-zero",
                   "WREN-OK (2 outlet(s) resolved)" in out
                   and "WREN-EXIT=0" in out,
                   "the sample app exited 0")

        # --- two editor windows hold two different documents ---
        mark = len(session.log_text())
        session.run("%s --new weaver_ib7b.conf; echo NEW2=$?" % WEAVER)
        out = session.output_since(mark)
        self.check("second-doc-created",
                   "WEAVER: new %s from template (3 nodes)" % DOC2 in out,
                   "a second template document was created")

        mark = len(session.log_text())
        session.run("DISPLAY=:0 %s --open %s --show &" % (WEAVER, DOC1))
        session.run("DISPLAY=:0 %s --open %s --show &" % (WEAVER, DOC2))

        deadline = time.time() + 45
        log = session.log_text()
        while time.time() < deadline:
            if ("doc=%s" % DOC1) in log and ("doc=%s" % DOC2) in log:
                break
            time.sleep(0.5)
            log = session.log_text()

        self.check("two-windows-logged",
                   log.count("WEAVER: window") >= 2
                   and ("doc=%s" % DOC1) in log
                   and ("doc=%s" % DOC2) in log,
                   "two editor windows logged two different documents")

        session.run("killall Weaver")
