"""U3d: the search and token fields.

The board carries a search field (a magnifier and a clear button) and a
token field (Return or a comma commits what was typed). This case types
into both and clicks the clearance, then reads the board's log:

  * Return in the search field sends its action with the text
  * the CLEAR button empties the field AND sends the action (so an app has
    one place to listen)
  * a comma commits a token without sending an action, Return commits and
    sends, and Backspace on an empty entry takes the last token back

Coordinates come from the board's own log, and the monitor's Y is
MIRRORED — screen_h - y, as every pointer case here must do.
"""

import re

from harness import BaseCase

ZOO = "/Applications/WidgetZoo"


class Case(BaseCase):
    title = "UIKit U3d: the search and token fields"
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
        mark = len(session.log_text())
        session.run("%s 60 &" % ZOO)
        if not session.wait_for(r"ZOO-READY", 120):
            self.check("board-up", False,
                       "no ZOO-READY; guest tail: " + session.tail())
            return
        out = session.output_since(mark)
        sh = re.search(r"ZOO-SCREEN w=\d+ h=(\d+)", out)
        pts = {m.group(1): (float(m.group(2)), float(m.group(3)))
               for m in re.finditer(r"ZOO-AT(?:CLEAR)? (\S+) x=([\d.]+) y=([\d.]+)",
                                    out)}
        clear = re.search(r"ZOO-ATCLEAR x=([\d.]+) y=([\d.]+)", out)
        self.check("controls-located",
                   sh is not None and "SEARCH" in pts and "TOKEN" in pts
                   and clear is not None,
                   "screen %s, search at %s, token field at %s, clear at %s"
                   % (sh.group(1) if sh else "?", pts.get("SEARCH"),
                      pts.get("TOKEN"),
                      clear.groups() if clear else None))
        if sh is None or clear is None or "SEARCH" not in pts:
            return
        screen_h = int(sh.group(1))
        mon = session.monitor()

        def at(x, y):
            return int(x), int(screen_h - y)

        # ---- the search field ----
        mon.park()
        sx, sy = pts["SEARCH"]
        mon.click_at(*at(sx, sy), settle=1.0)
        for ch in "abc":
            mon.key(ch, settle=0.3)
        mon.key("ret", settle=0.6)
        session.wait_for(r"ZOO-SEARCH", 30)
        out2 = session.output_since(mark)
        self.check("search-sends-its-text", "ZOO-SEARCH [abc]" in out2,
                   "Return sent the search field's text"
                   if "ZOO-SEARCH [abc]" in out2
                   else "no search action; tail: " + session.tail())

        # the clear button: the text goes AND the action fires
        cx, cy = float(clear.group(1)), float(clear.group(2))
        mon.click_at(*at(cx, cy), settle=0.8)
        session.wait_for(r"ZOO-SEARCH \[\]", 30)
        out3 = session.output_since(mark)
        self.check("clear-button-empties-and-fires",
                   "ZOO-SEARCH []" in out3,
                   "the clear button emptied the field and sent the action"
                   if "ZOO-SEARCH []" in out3
                   else "the clear button did nothing; tail: " + session.tail())

        # ---- the token field ----
        tx, ty = pts["TOKEN"]
        mon.click_at(*at(tx, ty), settle=1.0)
        for ch in "one":
            mon.key(ch, settle=0.25)
        mon.key("comma", settle=0.5)		# commits WITHOUT sending
        for ch in "two":
            mon.key(ch, settle=0.25)
        mon.key("ret", settle=0.6)		# commits AND sends
        session.wait_for(r"ZOO-TOKENS \[one,two\]", 30)
        out4 = session.output_since(mark)
        self.check("tokens-commit",
                   "ZOO-TOKENS [one,two]" in out4,
                   "the token list became one,two (a comma and a Return "
                   "each committed)" if "ZOO-TOKENS [one,two]" in out4
                   else "tokens did not commit; tail: " + session.tail())
        self.check("return-sends-from-a-token-field",
                   "ZOO-TOKENS n=2" in out4,
                   "and Return sent the token field's action")

        # Backspace on the empty entry takes the last token back
        mon.key("backspace", settle=0.6)
        session.wait_for(r"ZOO-TOKENS \[one\]", 30)
        out5 = session.output_since(mark)
        self.check("backspace-takes-a-token-back",
                   "ZOO-TOKENS [one]" in out5,
                   "the last token went back on Backspace"
                   if "ZOO-TOKENS [one]" in out5
                   else "the token survived Backspace; tail: " + session.tail())
