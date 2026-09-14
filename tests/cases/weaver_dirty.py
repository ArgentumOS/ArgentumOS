"""Weaver dirty marker (D14): an edited document marks itself in the window
title.

The title is set at window init from the editor's dirty state, so the WM's
manage line carries it: a dirty document maps as `Weaver - <doc> (edited)`,
a saved one maps as plain `Weaver`. Two show runs prove both states.
"""

import re
import time

from harness import BaseCase

WEAVER = "/Applications/Weaver.app/bin/Weaver"
FIXTURE = "/System/Shared/tests/weaver_ib2.conf"
DOC = "/Users/Admin/Documents/weaver_dirty.conf"

MANAGE = (r"KESTREL: manage 0x[0-9a-f]+ '([^']*)' frame=0x[0-9a-f]+ "
          r"at \d+,\d+ \d+x\d+")


class Case(BaseCase):
    title = "Weaver dirty marker: the title carries (edited) until save"
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

        # stage the fixture into the user's own documents (D11)
        mark = len(session.log_text())
        session.run("cp %s %s && echo DOC-COPIED" % (FIXTURE, DOC))
        out = session.output_since(mark)
        self.check("fixture-staged", "DOC-COPIED" in out,
                   "the fixture is in %s" % DOC)

        def wait_for(pattern, start, secs=45):
            deadline = time.time() + secs
            log = session.log_text()[start:]
            while time.time() < deadline:
                m = re.search(pattern, log)
                if m:
                    return m, log
                time.sleep(0.5)
                log = session.log_text()[start:]
            return None, log

        # --- a dirty document maps with the marker ---
        mark = len(session.log_text())
        session.run("DISPLAY=:0 %s --open weaver_dirty.conf "
                    "--move okButton 1 1 --show &" % WEAVER)
        m, log = wait_for(MANAGE, mark)
        self.check("dirty-manage-title",
                   bool(m) and " (edited)" in m.group(1),
                   "the WM managed the dirty document's marked title (%s)"
                   % (m.group(1) if m else "no manage line"))
        self.check("dirty-title-logged",
                   'WEAVER: title "Weaver - weaver_dirty.conf (edited)"'
                   in log,
                   "the editor logged the marked title")
        session.run("killall Weaver")

        # --- the SAME document, saved, maps clean ---
        mark = len(session.log_text())
        session.run("DISPLAY=:0 %s --open weaver_dirty.conf "
                    "--move okButton 1 1 --save --show &" % WEAVER)
        m, log = wait_for(MANAGE, mark)
        self.check("clean-manage-title",
                   bool(m) and m.group(1) == "Weaver",
                   "the WM managed the saved document's clean title (%s)"
                   % (m.group(1) if m else "no manage line"))
        self.check("clean-title-logged",
                   'WEAVER: title "Weaver"' in log,
                   "the editor logged the clean title after save")
        session.run("killall Weaver")
