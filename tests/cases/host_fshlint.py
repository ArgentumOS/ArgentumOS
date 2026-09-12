"""The FSH porting linter is clean over the staged System/Tools.

No guest is involved: this is the cheapest case in the harness, needs no QEMU,
and catches the mistake a new contributor is most likely to make - a tool that
hardcodes a conventional Unix path instead of an FSH one.

It reads the STAGED userland (.build/rootfs64), so a checkout that has not run
`make userland64` SKIPs with that instruction instead of failing.
"""

from harness import BaseCase
from harness import paths


class Case(BaseCase):
    title = "make fshlint is clean over the staged System/Tools"
    tier = "fast"
    needs_boot = False
    timeout = 600

    def run(self, ctx):
        ctx.require_exists(paths.STAGED_ROOT, "run `make userland64` first")
        rc, out = ctx.host(["make", "fshlint"], secs=420)
        lines = [line for line in (out or "").strip().splitlines() if line.strip()]
        # `make` brackets its own output; the linter's summary line is the one
        # worth reporting.
        summary = [line for line in lines if "scan:" in line] or lines[-1:]
        self.check("fshlint-clean", rc == 0,
                   "%s (rc=%d)" % (summary[-1].strip(), rc))
        for line in lines[:10]:
            self.note(line)
