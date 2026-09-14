"""Weaver guides + marquee + snapping (the IB3 refinements, post-IB7).

Guides: while a move/resize drag is active, the editor snaps the closest
edge/centre within GUIDE_HIT to the target (move shifts the frame, resize
adjusts the moving edge), logs the alignment, and the overlay draws the
hairlines. Marquee: a press-drag on empty canvas draws a rubber band and,
on release, logs the hits and selects the topmost one.
Both are driven through the SAME gesture engine a real pointer reaches.
"""

from harness import BaseCase

WEAVER = "/Applications/Weaver.app/bin/Weaver"
FIXTURE = "/System/Shared/tests/weaver_ib2.conf"
DOC = "/Users/Admin/Documents/weaver_guides.conf"


class Case(BaseCase):
    title = "Weaver guides + marquee: snapping hairlines, rubber-band multi-select"
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
        session.run("cp %s %s && echo DOC-COPIED" % (FIXTURE, DOC))
        out = session.output_since(mark)
        self.check("fixture-staged", "DOC-COPIED" in out,
                   "the fixture is in %s" % DOC)

        # --- guides: drag okButton so its top edge aligns with greeting ---
        mark = len(session.log_text())
        session.run("%s --open weaver_guides.conf --click 65 72 "
                    "--drag 65 72 65 28; echo WEAVER-EXIT=$?" % WEAVER)
        out = session.output_since(mark)
        for line in out.strip().splitlines():
            if "WEAVER" in line:
                self.note(line)

        self.check("vertical-guide",
                   "WEAVER: guide vertical x=20 okButton.left ~ "
                   "greeting.left" in out,
                   "the left edges aligned (a vertical guide)")
        self.check("horizontal-guide",
                   "WEAVER: guide horizontal y=16 okButton.top ~ "
                   "greeting.top" in out,
                   "the top edges aligned (a horizontal guide)")
        self.check("commit-after-guide",
                   "WEAVER: commit move okButton 20,60 90x24 -> "
                   "20,16 90x24" in out,
                   "the guided move committed")
        self.check("guides-exit-zero", "WEAVER-EXIT=0" in out,
                   "the guide run exited 0")

        # --- marquee: a rubber band over both controls selects BOTH ---
        mark = len(session.log_text())
        session.run("%s --open weaver_guides.conf --marquee 10 10 130 90 "
                    "--click 65 72; echo WEAVER-EXIT=$?" % WEAVER)
        out = session.output_since(mark)
        for line in out.strip().splitlines():
            if "WEAVER" in line:
                self.note(line)

        self.check("marquee-hits",
                   "WEAVER: marquee 10,10 120x80 hits=2 select=2 "
                   "greeting okButton" in out,
                   "the marquee found both controls and selected both")
        self.check("marquee-multi-selection",
                   "WEAVER: select 2 greeting,okButton primary=okButton"
                   in out,
                   "the selection set holds both, primary = the topmost")
        self.check("click-collapses-to-single",
                   "WEAVER: select okButton" in out,
                   "a plain click replaces the multi-selection with one node")
        self.check("marquee-exit-zero", "WEAVER-EXIT=0" in out,
                   "the marquee run exited 0")

        # --- snap (move): the pointer lands 2pt off; the frame snaps on ---
        mark = len(session.log_text())
        session.run("%s --open weaver_guides.conf --click 65 72 "
                    "--drag 65 72 65 30; echo WEAVER-EXIT=$?" % WEAVER)
        out = session.output_since(mark)
        for line in out.strip().splitlines():
            if "WEAVER" in line:
                self.note(line)

        self.check("move-snap-committed",
                   "WEAVER: commit move okButton 20,60 90x24 -> "
                   "20,16 90x24" in out,
                   "the pointer's 20,18 landing snapped up to greeting's "
                   "top edge (20,16)")
        self.check("move-snap-guide",
                   "WEAVER: guide horizontal y=16 okButton.top ~ "
                   "greeting.top" in out,
                   "the snapped guide is the one logged")
        self.check("move-snap-exit-zero", "WEAVER-EXIT=0" in out,
                   "the move-snap run exited 0")

        # --- snap (resize): the right edge lands 2pt short; it snaps ---
        mark = len(session.log_text())
        session.run("%s --open weaver_guides.conf --click 65 72 "
                    "--resize 110 84 138 84; echo WEAVER-EXIT=$?" % WEAVER)
        out = session.output_since(mark)
        for line in out.strip().splitlines():
            if "WEAVER" in line:
                self.note(line)

        self.check("resize-snap-committed",
                   "WEAVER: commit resize okButton 20,60 90x24 -> "
                   "20,60 120x24" in out,
                   "the pointer's 118pt width snapped to greeting's centre "
                   "(120pt, right edge 140)")
        self.check("resize-snap-guide",
                   "WEAVER: guide vertical x=140 okButton.right ~ "
                   "greeting.cx" in out,
                   "the snapped resize guide is the one logged")
        self.check("resize-snap-exit-zero", "WEAVER-EXIT=0" in out,
                   "the resize-snap run exited 0")
