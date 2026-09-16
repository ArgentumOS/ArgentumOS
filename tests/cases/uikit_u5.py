"""U5: the StackView — and the board's own columns standing on it.

Two halves in one boot.

The first half is the display-free probe (/System/Shared/tests/stack_view): it
builds stacks at known sizes and asserts the frames that come out. That is the
whole contract checked exactly, because a stack arranges BY CONSTRAINT — the
chain along the axis, alignment across it, all six distributions, the insets,
per-view custom spacing, and hidden arranged views detaching.

The second half is the widget zoo, whose three columns are StackViews now. The
board reports each column's rows and height, and the ROW PITCHES it logs are
what proves the stacks are doing the arranging: the button column is one
uniform gap, while the value column's gaps differ from row to row, so a
container that ignored setCustomSpacingAfterView would flatten them.

And the coordinates must be what they were BEFORE the migration — every other
board gate aims at these controls through ZOO-AT, so a migration that moved one
would be caught here rather than in six other cases.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/stack_view"
ZOO = "/Applications/WidgetZoo"


class Case(BaseCase):
    title = "UIKit U5: a stack arranges its views, and the board's columns are stacks"
    tier = "slow"
    timeout = 420

    def run(self, ctx):
        session = ctx.boot(machine="pc,i8042=off",
                           extra=["-device", "qemu-xhci",
                                  "-device", "usb-mouse",
                                  "-device", "usb-kbd"])
        ready = session.shell_ready(150)
        self.check("shell-ready", ready, "the serial console has a shell")
        if not ready:
            return

        # ---- the probe: the arrangement, exactly ------------------------
        mark = len(session.log_text())
        session.run("test -x %s && %s; echo U5-EXIT=$?" % (PROBE, PROBE))
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("U5-FAIL"):
                self.note(line)
        self.check("the-stack-probe-ran", "U5-OK (" in out,
                   "the probe arranged its stacks and reported"
                   if "U5-OK (" in out
                   else "no verdict; tail: " + session.tail(3))
        self.check("no-arrangement-failed", "U5-FAIL" not in out,
                   "every arrangement case held")
        self.check("the-stack-probe-exited-zero", "U5-EXIT=0" in out,
                   "the probe exited 0"
                   if "U5-EXIT=0" in out
                   else "the probe exited non-zero: " + session.tail(2))

        # ---- the board: the columns really are stacks -------------------
        mark = len(session.log_text())
        session.run("%s 30 &" % ZOO)
        if not session.wait_for(r"ZOO-READY", 120):
            self.check("board-up", False,
                       "no ZOO-READY; guest tail: " + session.tail(4))
            return
        board = session.output_since(mark)

        stacks = {m.group(1): (int(m.group(2)), float(m.group(3)))
                  for m in re.finditer(
                      r"ZOO-STACK (\S+) rows=(\d+) h=([\d.]+)", board)}
        self.check("the-three-columns-are-stacks",
                   set(stacks) == {"buttons", "text", "values"},
                   "the board reported stacks for %s" % sorted(stacks))
        if set(stacks) != {"buttons", "text", "values"}:
            return
        # 12 buttons of 28 on a 6pt gap; the text and value columns are their
        # own sizes and their own gaps
        self.check("the-stacks-hold-the-whole-column",
                   stacks["buttons"] == (12, 402.0)
                   and stacks["text"] == (7, 262.0)
                   and stacks["values"] == (12, 334.0),
                   "rows/heights are buttons %s, text %s, values %s; the sizes"
                   " and the gaps between them are 402/262/334"
                   % (stacks["buttons"], stacks["text"], stacks["values"]))

        pts = {m.group(1): (float(m.group(2)), float(m.group(3)))
               for m in re.finditer(r"ZOO-AT (\S+) x=([\d.]+) y=([\d.]+)",
                                    board)}

        def pitch(a, b):
            """The vertical distance between two rows' centres."""
            return pts[b][1] - pts[a][1]

        # THE UNIFORM COLUMN: 28-tall buttons on a 6pt gap is a 34pt pitch,
        # and the tenth row sits nine pitches below the first.
        self.check("a-uniform-column-keeps-its-pitch",
                   pitch("BEZEL-ROUNDED", "BEZEL-ROUNDRECT") == 34.0
                   and pitch("BEZEL-ROUNDED", "SWITCH") == 306.0,
                   "the button column's pitch is %s (34 expected), and the "
                   "tenth row is %s below the first (nine of them is 306)"
                   % (pitch("BEZEL-ROUNDED", "BEZEL-ROUNDRECT"),
                      pitch("BEZEL-ROUNDED", "SWITCH")))

        # THE COLUMN WITH ITS OWN GAPS, which is what custom spacing is for:
        # 24 and 30-tall rows 8 apart are 35 centres apart, 30 and 26-tall rows
        # 6 apart are 34, and 26 and 14-tall rows 8 apart are 28. A stack
        # using ONE spacing could not produce three different pitches.
        self.check("per-row-spacing-is-honoured",
                   pitch("SLIDER", "DIAL") == 35.0
                   and pitch("DIAL", "STEPPER") == 34.0
                   and pitch("STEPPER", "PROGRESS") == 28.0,
                   "the value column's pitches are %s/%s/%s between the "
                   "slider, dial, stepper and progress bar; the gaps the "
                   "cursor used to encode are 8/6/8 on 24/30/26/14-tall rows"
                   % (pitch("SLIDER", "DIAL"), pitch("DIAL", "STEPPER"),
                      pitch("STEPPER", "PROGRESS")))

        # THE MIGRATION MOVED NOTHING. These are the positions the other board
        # gates aim at, and they are the ones they saw before the columns
        # became stacks.
        self.check("the-migration-did-not-move-the-controls",
                   pts["EDITTEXT"] == (476.0, 265.0)
                   and pts["SEARCH"] == (476.0, 299.0)
                   and pts["TOKEN"] == (476.0, 333.0)
                   and pts["SLIDER"] == (746.0, 96.0)
                   and pts["SWITCH"] == (206.0, 404.0),
                   "editable field %s, search %s, token %s, slider %s, switch "
                   "%s — the same frames the y cursors produced"
                   % (pts["EDITTEXT"], pts["SEARCH"], pts["TOKEN"],
                      pts["SLIDER"], pts["SWITCH"]))
