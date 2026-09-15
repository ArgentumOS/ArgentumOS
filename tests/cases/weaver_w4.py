"""Weaver W4: code generation from a class record.

`--gen-class <class> <dir>` writes a header and a source for the class
record: the class with its outlet/action declarations, the action stubs,
and the D6 binding table entry. The probe asserts the emitters are
deterministic and carry the shape; the gate generates in the guest and
checks the files' content.
"""

from harness import BaseCase

WEAVER = "/Applications/Weaver.app/bin/Weaver"
PROBE = "/System/Shared/tests/interface_codegen"
FIXTURE = "/System/Shared/tests/weaver_ib2.conf"
DOC = "/Users/Admin/Documents/weaver_w4.conf"
OUT = "/System/Temporary Files"


class Case(BaseCase):
    title = "Weaver W4: --gen-class emits C++ for the class record"
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
        session.run("test -x %s && %s; echo W4-EXIT=$?" % (PROBE, PROBE))
        out = session.output_since(mark)
        self.check("probe-ran", "W4-OK" in out,
                   "the codegen probe ran (deterministic, shape carried)")
        self.check("probe-exit-zero", "W4-EXIT=0" in out,
                   "the probe exited 0")

        mark = len(session.log_text())
        session.run("cp %s %s && rm -f %s.weaverundo && echo DOC-COPIED"
                    % (FIXTURE, DOC, DOC))
        out = session.output_since(mark)
        self.check("fixture-staged", "DOC-COPIED" in out,
                   "the fixture is in %s" % DOC)

        mark = len(session.log_text())
        session.run("%s --open weaver_w4.conf "
                    "--new-class MyController Object "
                    "--add-outlet MyController greeting "
                    "--add-action MyController doThing "
                    "--gen-class MyController \"%s\"; echo WEAVER-EXIT=$?"
                    % (WEAVER, OUT))
        out = session.output_since(mark)
        for line in out.strip().splitlines():
            if "WEAVER" in line:
                self.note(line)
        self.check("gen-logged",
                   "WEAVER: gen-class MyController %s/MyController.h "
                   "%s/MyController.cpp" % (OUT, OUT) in out,
                   "the generator named both files")
        self.check("exit-zero", "WEAVER-EXIT=0" in out,
                   "the run exited 0")

        mark = len(session.log_text())
        session.run("cat \"%s/MyController.h\"; echo HEADER-END" % OUT)
        header = session.output_since(mark)
        self.check("header-shape",
                   "class MyController {" in header
                   and "void doThing();" in header
                   and "argentum::View *greeting = nullptr;" in header,
                   "the header declares the class, its action and outlet")

        mark = len(session.log_text())
        session.run("cat \"%s/MyController.cpp\"; echo SOURCE-END" % OUT)
        src = session.output_since(mark)
        self.check("source-shape",
                   'MyController::MyController() {}' in src
                   and "void MyController::doThing() {" in src
                   and "weaverBind_MyController" in src
                   and 'd.bind(target, "doThing", [&obj]() { '
                       'obj.doThing(); });' in src,
                   "the source carries the ctor, the stub and the binding")
