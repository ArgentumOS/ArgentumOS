"""Xfb: can the server move a window at all?

A raw-Xlib probe (`userland/tests/x_move.cpp`, no toolkit) creates a
window, moves it 20 times with a flush after each move, and then asks the
server a question. It exists because a toolkit drag was wedging, and the
first thing to establish is which side is at fault.

  XMOVE-DONE       the server survives moving a window
  XMOVE-NO-ANSWER  it stopped answering after the moves
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/x_move"


class Case(BaseCase):
    title = "Xfb: a window can be moved"
    tier = "slow"
    timeout = 420

    def run(self, ctx):
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready, "the serial console has a shell")
        if not ready:
            return
        mark = len(session.log_text())
        session.run("%s &" % PROBE)
        session.wait_for(r"XMOVE-DONE|XMOVE-NO-ANSWER", 90)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("XMOVE-"):
                self.note(line)
        moved = len([l for l in out.splitlines()
                     if l.startswith("XMOVE-") and " x=" in l])
        self.check("moves-issued", moved == 20,
                   "%d of 20 moves were issued by the client" % moved)
        self.check("server-still-answers", "XMOVE-ALIVE" in out,
                   "the server answered a round trip AFTER the moves"
                   if "XMOVE-ALIVE" in out
                   else "the server stopped answering: Xfb wedges on a "
                        "window move, and no toolkit on it can drag")
        rs = re.search(r"XMOVE-RESIZE asked=(\d+)x(\d+) got=(\d+)x(\d+)", out)
        self.check("resize-is-honoured",
                   rs is not None and rs.group(1) == rs.group(2 + 1)
                   and rs.group(2) == rs.group(4),
                   "the window was %sx%s after asking for %sx%s"
                   % (rs.group(3), rs.group(4), rs.group(1), rs.group(2))
                   if rs else "the server never answered the resize")
        self.check("probe-completed", "XMOVE-DONE" in out,
                   "the mover ran to the end" if "XMOVE-DONE" in out
                   else "the mover never finished")
