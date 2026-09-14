"""Weaver IB2 - the editor shell: open, select, move, save, reload
(docs/design/weaver-plan.md 8).

IB2 is driven by SCRIPTED COMMANDS (plan 8a fallback, decided 2026-09): the
editor accepts argv commands that drive its own state, and the gate asserts on
the editor's `WEAVER: ...` log rather than on the harness's aim. The one piece
that needs the toolkit's real dispatch - a click on a live Button in the canvas
must NOT fire the Button's action (D12) - is proven by the display-free
weaver_suppress probe, which drives the same Window dispatch path with the
canvas hit-testing on and off.

The pixel check does not aim at chrome: the editor's own log gives the client
size and px/pt, the WM's manage line gives the frame position (Kestrel's
clientRect inset: 4 px lip, 20 px band), and the gate only asks whether the
moved Button's box differs from the window's own background sample.
"""

import re

from harness import BaseCase

WEAVER = "/Applications/Weaver.app/bin/Weaver"
SUPP = "/System/Shared/tests/weaver_suppress"
FIXTURE = "/System/Shared/tests/weaver_ib2.conf"
DOC = "/Users/Admin/Documents/weaver_ib2.conf"

# Kestrel's clientRect (kestrel.cpp): the client inside the frame is inset
# by the lip on the sides and the title band on top.
FRAME_PX = 4
BAND_H = 20

MANAGE = (r"KESTREL: manage 0x[0-9a-f]+ 'Weaver' frame=0x[0-9a-f]+ "
          r"at (\d+),(\d+) (\d+)x(\d+)")
WINDOW = r"WEAVER: window 0x[0-9a-f]+ (\d+)x(\d+) ppt=([0-9.]+)"


class Case(BaseCase):
    title = "Weaver IB2: the editor shell - open, select, save"
    tier = "fast"
    timeout = 420

    def run(self, ctx):
        ctx.require_guest_file("Weaver")
        ctx.require_guest_file("weaver_suppress")
        session = ctx.boot_to_desktop(secs=150)
        ready = session.shell_ready(90)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        # --- D12 first: the suppression probe (synthetic dispatch) ---
        mark = len(session.log_text())
        session.run("test -x %s && %s; echo SUPP-EXIT=$?" % (SUPP, SUPP))
        out = session.output_since(mark)
        for line in out.strip().splitlines():
            if "SUPP" in line:
                self.note(line)
        self.check("suppression-probe-ran", "SUPP:" in out,
                   "the D12 probe ran" if "SUPP:" in out
                   else "no probe output - is weaver_suppress in the image?")
        self.check("suppression-probe-ok",
                   "SUPP-OK" in out and "SUPP-EXIT=0" in out,
                   "the non-hit-testable canvas suppressed the Button's "
                   "action")

        # --- stage the fixture into the user's own documents (D11) ---
        mark = len(session.log_text())
        session.run("cp %s %s && echo DOC-COPIED" % (FIXTURE, DOC))
        out = session.output_since(mark)
        self.check("fixture-staged", "DOC-COPIED" in out,
                   "the fixture is in %s" % DOC)

        # --- the editor's state run: open, click, move, save, reload ---
        mark = len(session.log_text())
        session.run("%s --open weaver_ib2.conf --click 65 72 "
                    "--move okButton 10 20 --save --reload --rect okButton; "
                    "echo WEAVER-EXIT=$?" % WEAVER)
        out = session.output_since(mark)
        for line in out.strip().splitlines():
            if "WEAVER" in line:
                self.note(line)

        self.check("open-logs-doc",
                   "WEAVER: open %s (3 nodes)" % DOC in out,
                   "the bare name resolved to the user's Documents")
        self.check("click-selects-button",
                   "WEAVER: select okButton" in out,
                   "a click at the Button's centre selected it by identifier")
        self.check("move-logs-old-to-new",
                   "WEAVER: move okButton 20,60 -> 30,80" in out,
                   "the move logged the old and new rect")
        self.check("save-logs-path", "WEAVER: save %s (" % DOC in out,
                   "save wrote the document back to its path")
        self.check("reload-logs", "WEAVER: reload %s (3 nodes)" % DOC in out,
                   "reload re-read the saved document")
        self.check("rect-after-reload",
                   "WEAVER: rect okButton = 30,80 90x24" in out,
                   "the moved rect is what reloaded (30,80 90x24)")
        self.check("state-exit-zero", "WEAVER-EXIT=0" in out,
                   "the state run exited 0")

        # --- the saved file on disk re-reads equal to the in-memory doc ---
        mark = len(session.log_text())
        session.run("cat %s; echo CAT-EXIT=$?" % DOC)
        cat = session.output_since(mark)
        self.check("saved-file-has-moved-frame",
                   "x = 30" in cat and "y = 80" in cat,
                   "the emitted file carries the moved frame")
        self.check("saved-file-keeps-title",
                   'title = "OK"' in cat,
                   "the emitted file still carries the Button's title")

        # --- the pixel check: show the SAVED document and sample its Button ---
        mark = len(session.log_text())
        session.run("DISPLAY=:0 %s --open weaver_ib2.conf --show &" % WEAVER)
        managed = session.wait_for(MANAGE, 45)
        self.check("show-window-managed", managed,
                   "the WM managed the editor's window" if managed
                   else "no manage line for Weaver")

        if managed:
            wm = re.search(MANAGE, session.log_text())
            fx, fy = int(wm.group(1)), int(wm.group(2))
            wl = re.search(WINDOW, session.log_text())
            ppt = float(wl.group(3)) if wl else (4.0 / 3.0)

            # the moved Button: pt 30,80 90x24 -> centre pt (75, 92)
            bx = fx + FRAME_PX + int(round(75 * ppt))
            by = fy + BAND_H + int(round(92 * ppt))
            shot = session.shot("weaver-show")

            # the window's own background, sampled away from every control
            bg = shot.px(fx + FRAME_PX + int(round(5 * ppt)),
                         fy + BAND_H + int(round(200 * ppt)))
            changed = 0
            for y in range(by - 9, by + 10):
                for x in range(bx - 9, bx + 10):
                    p = shot.px(x, y)
                    if any(abs(p[i] - bg[i]) > 30 for i in range(3)):
                        changed += 1
            self.check("button-drawn-at-moved-rect", changed > 0,
                       "%d px of the moved Button's box differ from the "
                       "window background" % changed)

        session.run("killall Weaver")
