# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSFileManagerDelegate — W8 slice 2's acceptance (foundation-plan.md §60).

The four operations a file manager can be told to refuse — copying, moving, linking, removing — and
the two questions each family asks: whether the operation "should begin at all", and whether it
"should proceed when an error occurs". Every rule the probe asserts is a RULE from Apple's published
pages, not a taste; §60 records which sentence each one came from.

The probe is `/System/Shared/tests/foundation_filemanagerdelegate`, ONE unit, importing only
`<Foundation/Foundation.h>` (plus `<unistd.h>` for the `symlink(2)` its fixture makes).

  * `delegate-defaults-to-nil-and-round-trips` — the property's default is nil, and it is ASSIGN (the
                                      manager hands back the same object);
  * `a-delegate-implementing-nothing-is-legal` — every protocol member is optional, so a conforming
                                      object with no methods at all is a valid delegate;
  * `copy-asks-once-for-the-directory-and-once-for-each-item` — "once for the directory and once for
                                      each item in the directory";
  * `the-path-form-is-asked-when-it-is-all-there-is` — the preference rule's other half;
  * `move-asks-only-for-the-item-itself` — a moved directory's CONTENTS are never asked about;
  * `remove-asks-for-every-item` — "prior to removing each item";
  * `link-asks-for-the-item-and-the-link-is-real` — and the link is a second NAME (same inode);
  * `the-url-form-is-preferred-when-both-exist` — "always prefers methods that take an NSURL object
                                      over those that take an NSString object";
  * `a-veto-skips-the-item-and-the-operation-succeeds` — a refusal is not an error: nothing is copied
                                      for that item, the rest is, and the operation says YES;
  * `a-veto-on-a-directory-skips-its-contents` — and its contents are never even asked about;
  * `a-veto-on-remove-keeps-the-whole-subtree` — Apple's own sentence: "returning NO prevents both the
                                      directory and its children from being deleted" — and the
                                      operation then reports NO with ENOTEMPTY, because the directory it
                                      was asked about cannot come out while a child remains (a REAL
                                      errno, unlike the refusal, which has none);
  * `a-veto-on-remove-needs-the-error-door-to-succeed` — and that same shape succeeds once the error
                                      door answers YES to the ENOTEMPTY;
  * `no-delegate-still-fails-on-an-error` — the old behaviour, preserved;
  * `an-unimplemented-error-door-leaves-the-error-standing` — "may also call" — a delegate cannot
                                      swallow a failure by having no opinion;
  * `the-error-door-can-swallow-an-error` — YES ignores the error and the walk carries on;
  * `the-error-door-can-abort` — NO stops the operation with that error;
  * `the-proceed-question-carries-the-errno` — the NSError handed over is NSPOSIXErrorDomain with the
                                      failing call's errno;
  * `probe-tree-removed` — the tree is gone.

IT WORKS IN A TREE OF ITS OWN MAKING under `/System/Temporary Files` and removes it at the end AND at
the start. It uses a manager of its OWN (`[[NSFileManager alloc] init]`) rather than `+defaultManager`,
which is Apple's own advice for a delegate and keeps the probe from changing what any other caller
sees.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_filemanagerdelegate"
CHECKS = ("delegate-defaults-to-nil-and-round-trips", "a-delegate-implementing-nothing-is-legal",
          "copy-asks-once-for-the-directory-and-once-for-each-item",
          "the-path-form-is-asked-when-it-is-all-there-is",
          "move-asks-only-for-the-item-itself", "remove-asks-for-every-item",
          "link-asks-for-the-item-and-the-link-is-real",
          "the-url-form-is-preferred-when-both-exist",
          "a-veto-skips-the-item-and-the-operation-succeeds",
          "a-veto-on-a-directory-skips-its-contents",
          "a-veto-on-remove-keeps-the-whole-subtree",
          "a-veto-on-remove-needs-the-error-door-to-succeed",
          "no-delegate-still-fails-on-an-error",
          "an-unimplemented-error-door-leaves-the-error-standing",
          "the-error-door-can-swallow-an-error", "the-error-door-can-abort",
          "the-proceed-question-carries-the-errno", "probe-tree-removed")


class Case(BaseCase):
    title = "NSFileManagerDelegate: the vetoes, the error doors and the URL-over-path preference"
    tier = "fast"
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_filemanagerdelegate")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-FILEMANAGERDELEGATE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-FILEMANAGERDELEGATE "):
                self.note(line)

        done = "FOUNDATION-FILEMANAGERDELEGATE DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-FILEMANAGERDELEGATE DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-FILEMANAGERDELEGATE %s ok$" % re.escape(c),
                                    out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-FILEMANAGERDELEGATE RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-FILEMANAGERDELEGATE-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
