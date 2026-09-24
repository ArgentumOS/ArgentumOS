# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSDirectoryEnumerator — W8 slice 1's acceptance (foundation-plan.md §60).

The deep walk as a CLASS rather than a private recursion: an enumerator the caller drives one item at
a time, whose three answers are three rules — the pathnames are RELATIVE to the directory being
enumerated (Apple's own class page: "These pathnames are relative to the directory"), `-level` counts
THAT directory as 0 so its immediate children are 1, and the walk does not resolve symbolic links nor
recurse through one. The two `-subpaths` doors collect the same walk into an array.

The probe is `/System/Shared/tests/foundation_directoryenumerator`, ONE unit, importing only
`<Foundation/Foundation.h>` (plus `<unistd.h>` for the `symlink(2)` its fixture makes).

  * `walk-yields-the-whole-subtree`       — every descendant exactly once, and nothing else;
  * `walk-paths-are-relative-to-the-directory` — nothing carries the directory's own path;
  * `walk-paths-join-back-to-real-items`  — appending one to the directory finds the real item, which
                                           is what Apple's own Objective-C example does;
  * `level-counts-the-enumerated-directory-as-zero` — the directory is 0, its children 1, the deepest
                                           item here 3;
  * `file-attributes-are-the-most-recent-items` — the CURRENT item's dictionary (a file's size, a
                                           directory's type) and nil before the first item;
  * `directory-attributes-are-the-starting-directorys` — a different question from the one above;
  * `the-walk-does-not-resolve-symbolic-links` — both links answer as LINKS, and neither is entered;
  * `a-file-enumerates-nothing`           — a FILE path gives a spent enumerator, not an error and not
                                           nil (Apple's own word for this case);
  * `all-objects-is-the-rest-of-the-walk` — the inherited cursor contract;
  * `skip-descendents-prunes-one-level`   — the pruning is exactly that subtree, and the item stays;
  * `skip-descendants-is-the-same-method` — the two spellings are one method;
  * `subpaths-agree-with-the-enumerator`  — the array doors and the walk are one walk;
  * `a-symbolic-link-given-as-the-path-is-traversed` — a link AS the path IS followed;
  * `an-empty-directory-answers-an-empty-array-not-nil` — "no items" is not "not a directory";
  * `subpaths-refuse-what-is-not-a-directory` — nil AND the errno, for a file (ENOTDIR) and a name
                                           that is not there (ENOENT);
  * `post-order-is-not-what-this-slice-builds` — the boundary this slice NAMES rather than hides;
  * `probe-tree-removed`                  — the tree is gone.

IT WORKS IN A TREE OF ITS OWN MAKING under `/System/Temporary Files` — this system's temp directory,
spelled the FSH way — and removes the whole tree at the end AND at the start.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_directoryenumerator"
CHECKS = ("walk-yields-the-whole-subtree", "walk-paths-are-relative-to-the-directory",
          "walk-paths-join-back-to-real-items",
          "level-counts-the-enumerated-directory-as-zero",
          "file-attributes-are-the-most-recent-items",
          "directory-attributes-are-the-starting-directorys",
          "the-walk-does-not-resolve-symbolic-links",
          "post-order-is-not-what-this-slice-builds",
          "a-file-enumerates-nothing", "all-objects-is-the-rest-of-the-walk",
          "skip-descendents-prunes-one-level", "skip-descendants-is-the-same-method",
          "subpaths-agree-with-the-enumerator",
          "a-symbolic-link-given-as-the-path-is-traversed",
          "an-empty-directory-answers-an-empty-array-not-nil",
          "subpaths-refuse-what-is-not-a-directory",
          "url-listing-keeps-hidden-and-drops-resource-forks", "a-listing-keeps-other-hidden-files",
          "url-listing-of-an-empty-directory-is-an-empty-array",
          "url-listing-refuses-what-is-not-a-directory",
          "url-listing-prefetches-the-keys", "url-listing-honours-skips-hidden-files",
          "url-enumerator-walks-and-yields-urls", "url-enumerator-prefetches-the-keys",
          "url-enumerator-over-a-file-enumerates-nothing",
          "url-enumerator-honours-skips-hidden-files",
          "probe-url-tree-removed", "probe-tree-removed")


class Case(BaseCase):
    title = "NSDirectoryEnumerator: the deep walk, its relative paths and its levels"
    tier = "fast"
    # It builds and removes a tree of its own under the temp directory; every read is a fixture it
    # made, so it can share a guest like the rest.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_directoryenumerator")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-DIRECTORYENUMERATOR-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-DIRECTORYENUMERATOR "):
                self.note(line)

        done = "FOUNDATION-DIRECTORYENUMERATOR DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-DIRECTORYENUMERATOR DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-DIRECTORYENUMERATOR %s ok$" % re.escape(c),
                                    out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-DIRECTORYENUMERATOR RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-DIRECTORYENUMERATOR-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
