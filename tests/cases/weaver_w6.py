"""Weaver W6: grouping and names.

The selection can be grouped into a container (Box / ScrollView /
SplitView) — the members re-home into the container's coordinates — and
ungrouped again; and an object can be named (GORM's Set Name), which is
what the outline shows. The container comes from the registry, so the
grouped node is a real class the app can build.
"""

from harness import BaseCase

WEAVER = "/Applications/Weaver.app/bin/Weaver"
FIXTURE = "/System/Shared/tests/weaver_ib2.conf"
DOC = "/Users/Admin/Documents/weaver_w6.conf"


class Case(BaseCase):
    title = "Weaver W6: group into a container, ungroup, and set names"
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
        session.run("cp %s %s && rm -f %s.weaverundo && echo DOC-COPIED"
                    % (FIXTURE, DOC, DOC))
        out = session.output_since(mark)
        self.check("fixture-staged", "DOC-COPIED" in out,
                   "the fixture is in %s" % DOC)

        # select both controls (the marquee covers them) and group in a Box
        mark = len(session.log_text())
        session.run("%s --open weaver_w6.conf --marquee 10 10 130 90 "
                    "--group box --outline --save --reload --roundtrip; "
                    "echo WEAVER-EXIT=$?" % WEAVER)
        out = session.output_since(mark)
        for line in out.strip().splitlines():
            if "WEAVER" in line:
                self.note(line)

        # greeting (20,16 240x20) + okButton (20,60 90x24)
        #   -> union (20,16 240x68); members become 0,0 and 0,44
        self.check("group-logged",
                   "WEAVER: group Box 2 (20,16 240x68)" in out,
                   "the selection was grouped into a Box over its union")
        self.check("members-rehomed",
                   "WEAVER: outline   Box (20,16 240x68)" in out
                   and "WEAVER: outline     Label greeting "
                       "(0,0 240x20)" in out
                   and "WEAVER: outline     Button okButton "
                       "(0,44 90x24)" in out,
                   "the members moved into the container's coordinates, "
                   "in their original order")
        self.check("roundtrip-ok",
                   "WEAVER: roundtrip /Users/Admin/Documents/weaver_w6.conf "
                   "OK" in out,
                   "the grouped document survived the emitter")
        self.check("exit-zero", "WEAVER-EXIT=0" in out,
                   "the run exited 0")

        # select the Box itself (inside it, clear of its children) and
        # ungroup; then name a control
        mark = len(session.log_text())
        session.run("%s --open weaver_w6.conf --click 250 70 --ungroup "
                    "--setname okButton actionButton --outline; "
                    "echo WEAVER-EXIT=$?" % WEAVER)
        out = session.output_since(mark)
        for line in out.strip().splitlines():
            if "WEAVER" in line:
                self.note(line)
        self.check("ungroup-logged", "WEAVER: ungroup 2" in out,
                   "the container was ungrouped")
        self.check("children-restored",
                   "WEAVER: outline   Label greeting (20,16 240x20)" in out
                   and "WEAVER: outline   Button actionButton "
                       "(20,60 90x24)" in out,
                   "the children returned to the root at their old frames, "
                   "and the renamed control shows its new name")
        self.check("setname-logged",
                   "WEAVER: setname okButton actionButton" in out,
                   "the control was renamed")

        # the grouped container is a registry class, so it BUILDS
        session.run("cp %s %s && rm -f %s.weaverundo" % (FIXTURE, DOC, DOC))
        mark = len(session.log_text())
        session.run("%s --open weaver_w6.conf --marquee 10 10 130 90 "
                    "--group scroll --outline; echo WEAVER-EXIT=$?" % WEAVER)
        out = session.output_since(mark)
        self.check("group-scrollview",
                   "WEAVER: group ScrollView 2" in out,
                   "a ScrollView group is available too")
        # the mixed-parent guard: after a group, a marquee that also
        # touches the container must be refused
        session.run("cp %s %s && rm -f %s.weaverundo" % (FIXTURE, DOC, DOC))
        mark = len(session.log_text())
        session.run("%s --open weaver_w6.conf --marquee 10 10 130 90 "
                    "--group box --marquee 10 10 130 90 --group split; "
                    "echo WEAVER-EXIT=$?" % WEAVER)
        out = session.output_since(mark)
        self.check("mixed-parents-refused",
                   "WEAVER: group FAIL (the selection must share a "
                   "parent)" in out,
                   "grouping a container together with its own child is "
                   "refused")
