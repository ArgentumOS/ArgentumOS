# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""Sterling K1, the guest half: the chain, end to end.

Sterling is the Objective-C surface language (`docs/design/sterling-syntax.md`);
`sterlingc` is its transpiler, and K1 is the plan's risk gate
(`docs/design/sterling-plan.md` §4) — "the only gate that can kill the plan".
Its claim is the *chain*, not any feature:

    MyClass.ag -> sterlingc -> MyClass.h/.m -> clang -> libobjc2 -> Foundation

The four host legs (`make sterlingc-check`) prove the emitted TEXT: the golden
diff against the document, the corpus, the reject cases, and that the output
compiles. **None of them runs anything**, which is exactly the gap this case
closes. `sterlingc_k1` is the compiler's own output linked with a hand-written
driver (`userland/tests/sterlingc_k1.m`) and executed on a guest boot, so the
class it prints about is one a `.ag` file produced.

The probe prints one `STERLING <name> ok|FAIL <detail>` line per check and a
final `STERLING RESULT ok=N fail=M`; the case asserts every check BY NAME, the
probe's own tally, its exit status, and that no FAIL line exists anywhere — the
`objc_smoke.py` contract verbatim. A probe that stopped early cannot pass by
printing a good-looking tally.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/sterlingc_k1"
CHECKS = ("init", "class-name", "foundation-superclass", "stored-default",
          "stored-property", "readonly-property", "class-method-called-c",
          "class-method-forwards-arg", "instance-method")


class Case(BaseCase):
    title = "Sterling: the compiler's emitted class runs against libobjc2"
    tier = "fast"
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("sterlingc_k1")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo STERLING-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("STERLING "):
                self.note(line)

        done = "STERLING DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no STERLING DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        # Every check, by name - a probe that stops early cannot pass by
        # printing a good-looking tally.
        missing = [c for c in CHECKS
                   if not re.search(r"^STERLING %s ok$" % re.escape(c),
                                    out, re.M)]
        self.check("every-check-passed", not missing,
                   "%d of %d checks reported ok" % (len(CHECKS) - len(missing),
                                                    len(CHECKS))
                   if missing else "all %d checks reported ok" % len(CHECKS))

        fails = [l for l in out.splitlines() if l.endswith("FAIL")
                 or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails
                   else "; ".join(fails))

        tally = re.search(r"STERLING RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally
                                                  else "missing"))

        self.check("exit-status", "STERLING-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
