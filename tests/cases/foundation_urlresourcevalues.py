# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSURL's resource values — W8 slice 6a's acceptance (foundation-plan.md §60).

The file's properties as VALUES, keyed by Cocoa's own key names, so a caller asks one question and gets
an object back - the same reason NSFileManager's `-attributesOfItemAtPath:error:` exists, from the other
side. The cache is part of the contract and not an optimisation: Apple documents that a URL object caches
what it has read and that the two `-removeCached…` doors take it back out, which is what makes two reads
of a file that changed between them an observable difference.

The probe is `/System/Shared/tests/foundation_urlresourcevalues`, ONE unit, importing only
`<Foundation/Foundation.h>`. It works in a tree of its own making under `/System/Temporary Files`, built
with POSIX calls so a failure cannot be the fixture's fault, and removes it at the end AND at the start.

  * `rv-the-keys-are-their-own-names` — the key and type constants are the published names (D2's
                                 spelling), distinct and non-nil;
  * `rv-a-file-answers-its-facts` — name, path, size, kinds, access answers, link count, inode
                                 identifier, resource type and parent, each against the same fixture's
                                 lstat;
  * `rv-a-directory-answers-its-own` — a directory is a directory and not a regular file;
  * `rv-a-link-is-about-the-link` — lstat AND NOT stat: a link's `-fileSizeKey` is the LENGTH OF ITS
                                 TARGET STRING (8), where a stat-based reader would say 5;
  * `rv-the-dot-rule-makes-an-item-hidden` — there is no hidden bit here, so the leading dot is the rule,
                                 asserted from both sides;
  * `rv-a-missing-file-refuses-with-its-errno` — NO and an ENOENT error, and the dictionary form answers
                                 EMPTY rather than nil (one unreadable key does not sink a set);
  * `rv-a-non-file-url-refuses` — D9's precedent, from the other class;
  * `rv-an-unknown-key-refuses` — a key this library does not answer is an error in BOTH shapes;
  * `rv-reachability-is-not-readability` — access(F_OK) for
                                 `-checkResourceIsReachableAndReturnError:`, with an error when absent;
  * `rv-the-cache-is-part-of-the-contract` — THE CENTREPIECE: read a size, change the file on disk, read
                                 again and get the CACHED answer;
  * `rv-removing-everything-clears-everything` — `-removeAllCachedResourceValues`, then the fresh value;
  * `rv-a-temporary-value-is-not-on-disk` — `-setTemporaryResourceValue:forKey:` answers what was set
                                 while a SECOND URL for the same file still answers the disk's truth, and
                                 nil takes the entry back out;
  * `rv-two-doors-agree` — the URL's modification date and NSFileManager's attribute are one fact;
  * `rv-a-set-writes-and-the-cache-forgets` — THE WRITE SIDE: a real write, read back through the URL,
                                 and a set FORGETS what the cache knew (so the read after a write is
                                 fresh, not stale);
  * `rv-a-set-is-visible-to-the-other-door` — the same fact through NSFileManager, which is the
                                 delegation: one implementation, two doors;
  * `rv-a-set-of-a-read-only-key-is-a-no-op` / `rv-a-set-of-an-unknown-key-is-a-no-op` /
                                 `rv-a-set-on-a-non-file-url-is-a-no-op` — Apple's sentence for BOTH
                                 setters, verbatim: such attempts "are ignored and are not considered
                                 errors" — so the door answers YES and writes nothing;
  * `rv-a-set-of-many-applies-what-it-can` — the dictionary form applies what it can and ignores the
                                 rest without reporting them;
  * `rv-set-many-reports-what-it-could-not-set` — and reports, under `NSURLKeysOfUnsetValuesKey`, exactly
                                 the keys whose write REACHED the file system and failed ("an array of
                                 URLResourceKey objects", the key's own page);
  * `rv-a-value-the-key-cannot-hold-is-refused` — the one refusal this door makes for its own reason;
  * `vol-capacity-is-the-filesystems-and-both-doors-agree` — a volume key asked of a file URL is a question
                                 about the volume HOLDING it, and the number agrees with NSFileManager's
                                 own file-system attributes (two doors, one superblock);
  * `vol-is-local-and-writable-here` — every file system this system can mount is local (there is no
                                 network file system client at all), and the temp tree's volume is
                                 writable;
  * `vol-supports-case-sensitive-names-and-the-proof` / `vol-supports-persistent-ids-and-the-proof` /
                                 `vol-supports-symbolic-links-and-the-proof` — THE THREE CLAIMS THE
                                 SUBSTRATE REALLY SUPPORTS, each PROVED by something the fixture does:
                                 two names differing only in case becoming two distinct inodes, one
                                 identifier answering from two independently built URLs, and a link
                                 reading back the name it points at. A volume's claimed capabilities are
                                 the worst place for a confident wrong answer, which is why the other 42
                                 volume rows are left OPEN with grounds rather than answered NO;
  * `probe-tree-removed`         — the tree is gone.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_urlresourcevalues"
CHECKS = ("rv-the-keys-are-their-own-names", "rv-a-file-answers-its-facts",
          "rv-a-directory-answers-its-own", "rv-a-link-is-about-the-link",
          "rv-the-dot-rule-makes-an-item-hidden", "rv-a-missing-file-refuses-with-its-errno",
          "rv-a-non-file-url-refuses", "rv-an-unknown-key-refuses",
          "rv-reachability-is-not-readability", "rv-the-cache-is-part-of-the-contract",
          "rv-removing-everything-clears-everything", "rv-a-temporary-value-is-not-on-disk",
          "rv-two-doors-agree",
          "rv-a-set-writes-and-the-cache-forgets", "rv-a-set-is-visible-to-the-other-door",
          "rv-a-set-of-a-read-only-key-is-a-no-op", "rv-a-set-of-an-unknown-key-is-a-no-op",
          "rv-a-set-on-a-non-file-url-is-a-no-op", "rv-a-set-of-many-applies-what-it-can",
          "rv-set-many-reports-what-it-could-not-set", "rv-a-value-the-key-cannot-hold-is-refused",
          "vol-capacity-is-the-filesystems-and-both-doors-agree", "vol-is-local-and-writable-here",
          "vol-supports-case-sensitive-names-and-the-proof", "vol-supports-persistent-ids-and-the-proof",
          "vol-supports-symbolic-links-and-the-proof", "probe-tree-removed")


class Case(BaseCase):
    title = "NSURL resource values: the file as a set of keyed values, with the cache asserted"
    tier = "fast"
    # It builds and removes a tree of its own under the temp directory, so it can share a guest.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_urlresourcevalues")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-URLRESOURCEVALUES-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-URLRESOURCEVALUES "):
                self.note(line)

        done = "FOUNDATION-URLRESOURCEVALUES DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-URLRESOURCEVALUES DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-URLRESOURCEVALUES %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-URLRESOURCEVALUES RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-URLRESOURCEVALUES-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
