# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""The class-cluster mechanism — M0, and the shipped family's contract — M1.

The plan's §C.3 lists the contract the library will match in sixteen families. M0
landed the MECHANISM first and proved it on a cluster the probe defines ITSELF, so
that no shipped class changed behaviour in that unit. M1 then does the same to the
first family, and the probe's second half asserts it ON NSArray/NSMutableArray and
on a class written over the primitives ALONE. Three of these checks are the ones
nothing else in the suite can make:

  * `an-ordinary-class-substitutes-nothing` — the new `-classForCoder` default must
    be inert for every class that is not a cluster, which is the whole library;
  * `the-archive-names-no-private-class` — NSKeyedArchiver's archive must carry the
    PUBLIC class name and no private one. That is the hinge §C.4 is about, and it
    is measured by SEARCHING THE ARCHIVE'S OWN BYTES rather than by asking an object
    what it would answer: a check that only asked would pass even if the archiver
    went on recording `-class`. `nsarray-archive-names-no-private-class` is the same
    measurement on a REAL array;
  * `nsarray-primitives-drive-*` — a subclass that overrides ONLY the two primitives
    the header documents must be correct through equality, hash, description,
    slicing, the searches and fast enumeration. That is what makes the documented
    primitive set a contract rather than a claim.
"""

import os
import re
import shutil
import subprocess
import tempfile

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_clusters"
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OBJC = os.path.join(REPO, "tools", "musl-clang-objc64.sh")

# THE COMPILE PROBE, WHICH IS M1'S THIRD ACCEPTANCE BULLET. The snippets are the assertion; the FLAGS are
# the instrument, and both halves of the flag set were MEASURED before they were written down:
#
#   * WITHOUT -Werror=incompatible-pointer-types the "must be refused" snippet COMPILES, exit 0 - clang
#     treats a generic-parameter mismatch as a WARNING. Written without that flag, the refusal check would
#     have passed no matter what the header said: it would have been vacuous.
#   * The NEGATIVE CONTROL has to mirror the include tree. Compiling the covariance snippet against a
#     patched copy of Foundation/ ALONE fails on `CoreGraphics/CGGeometry.h` file not found - a compile
#     failure with nothing to do with variance, which reads exactly like the instrument working.
# THE NEGATIVE HALF OF THE COMPILE PROBE, and it is a first-class assertion rather than an omission:
# Apple measured NSNumber as NOT parameterized (no `ObjectType` anywhere in its declaration), so a
# declaration that applies type arguments to it MUST be refused. Without this, "we did not parameterize
# NSNumber" would be indistinguishable from "we never checked", which is how a measurement rots.
UNPARAMETERIZED_SNIPPET = """\
#import <Foundation/Foundation.h>

/* NSNumber is NOT parameterized: applying type arguments to it must be REFUSED. */
void ag_unparameterized(void)
{
\tNSNumber<NSString *> *wrong = nil;

\t(void)wrong;
}
"""

# THE SAME NEGATIVE ASSERTION FOR THE OTHER TWO FAMILIES M7 MEASURED AS UNPARAMETERIZED, and for the same
# reason: "we did not parameterize it" and "we never checked" are indistinguishable in a tree. One snippet each,
# because a single snippet naming two classes could not say WHICH one accepted the arguments.
UNPARAMETERIZED_ATTRIBUTED_SNIPPET = """\
#import <Foundation/Foundation.h>

/* NSAttributedString is NOT parameterized (measured): type arguments must be REFUSED. */
void ag_unparameterized_attributed(void)
{
\tNSAttributedString<NSString *> *wrong = nil;

\t(void)wrong;
}
"""

UNPARAMETERIZED_POINTERARRAY_SNIPPET = """\
#import <Foundation/Foundation.h>

/* NSPointerArray is NOT parameterized (measured): type arguments must be REFUSED. */
void ag_unparameterized_pointerarray(void)
{
\tNSPointerArray<NSString *> *wrong = nil;

\t(void)wrong;
}
"""

UNPARAMETERIZED_M8_SNIPPETS = (
    ("NSCharacterSet", "NSCharacterSet", "NSCharacterSet<NSString *> *wrong = nil;"),
    ("NSValue", "NSValue", "NSValue<NSString *> *wrong = nil;"),
    ("NSNotification", "NSNotification", "NSNotification<NSString *> *wrong = nil;"),
)

UNPARAMETERIZED_M8_TEMPLATE = """\
#import <Foundation/Foundation.h>

/* %s is NOT parameterized (measured): type arguments must be REFUSED. */
void ag_unparameterized_%s(void)
{
\t%s

\t(void)wrong;
}
"""

COVARIANT_SNIPPET = """\
#import <Foundation/Foundation.h>

/* NSArray is declared __covariant, so a mutable-specialized array IS a string-specialized one. */
void ag_covariance(void)
{
	NSArray<NSMutableString *> *mutableStrings = nil;
	NSArray<NSString *> *strings = mutableStrings;

	(void)strings;
}
"""

UNRELATED_SNIPPET = """\
#import <Foundation/Foundation.h>

/* UNRELATED specializations must NOT convert: covariance runs along the class hierarchy, not sideways. */
void ag_unrelated(void)
{
	NSArray<NSString *> *strings = nil;
	NSArray<NSNumber *> *numbers = strings;

	(void)numbers;
}
"""
CHECKS = (
          "the-front-is-allocatable-and-inits-empty",
          "class-answers-the-concrete-class",
          "an-instance-is-still-kind-of-the-front",
          "plus-class-answers-the-front",
          "a-constructor-chooses-the-concrete-class-by-the-data",
          "class-for-coder-answers-the-front",
          "class-for-archiver-defaults-to-class-for-coder",
          "an-ordinary-class-substitutes-nothing",
          "the-archive-is-not-empty",
          "the-archive-names-the-public-class",
          "the-archive-names-no-private-class",
          "the-archive-round-trips-through-the-front",
          # M1: the same contract, on the shipped family.
          "nsarray-class-answers-a-concrete-class",
          "nsarray-four-cases-are-distinct-classes",
          "nsarray-alloc-init-is-the-empty-singleton",
          "nsarray-mutable-construction-answers-a-mutable-class",
          "nsarray-class-for-coder-answers-the-front",
          "nsarray-copy-is-the-receiver-and-mutable-copy-is-mutable",
          "nsarray-primitives-drive-equality-and-hash",
          "nsarray-primitives-drive-slicing-and-search",
          "nsarray-primitives-drive-fast-enumeration",
          "nsarray-archive-names-the-public-class",
          "nsarray-archive-names-no-private-class",
          # M2: the same contract, on the shipped dictionary family.
          "nsdictionary-class-answers-a-concrete-class",
          "nsdictionary-empty-instance-and-the-zero-count-singleton",
          "nsdictionary-a-copy-of-nothing-is-the-empty-singleton",
          "nsdictionary-mutable-construction-answers-a-mutable-class",
          "nsdictionary-class-for-coder-answers-the-front",
          "nsdictionary-empty-answers-every-read",
          "nsdictionary-archive-names-the-public-class",
          "nsdictionary-archive-names-no-private-class",
          "nsdictionary-primitives-drive-keys-values-and-equality",
          "nsdictionary-primitives-drive-getobjects-andkeys",
          "nsdictionary-primitives-drive-fast-enumeration",
          # M3: the set ladder, and the set family's primitives. Archiving a set is NOT asserted:
          # NSKeyedArchiver has no set path (see docs/TODO-before-release.md §3).
          "nsset-class-answers-a-concrete-class",
          "nsset-alloc-init-is-the-empty-singleton",
          "nsset-mutable-and-counted-answer-their-own-concrete-classes",
          "nsset-class-for-coder-answers-the-front",
          "nsset-empty-answers-every-read",
          "nsset-copy-is-the-receiver-and-mutable-copy-is-mutable",
          "nsset-primitives-drive-objects-and-count",
          "nsset-primitives-drive-equality-subset-and-intersection",
          "nsset-primitives-drive-hash-and-description",
          "nsset-primitives-drive-fast-enumeration",
          "nsset-primitives-drive-copy-and-mutable-copy",
          # M4: the number family, and its primitives.
          "nsnumber-class-answers-a-concrete-class",
          "nsnumber-class-for-coder-answers-the-front-but-not-for-a-public-subclass",
          "nsnumber-the-matrix-round-trips",
          "nsnumber-primitives-drive-conversions-equality-and-hash",
          # M5: the string family - the two doors, and its primitives.
          "nsstring-class-answers-a-concrete-class",
          "nsstring-alloc-init-is-a-concrete-empty-string",
          "nsstring-class-for-coder-answers-the-front-and-the-public-subclass",
          "nsstring-empty-answers-every-read",
          "nsstring-primitives-drive-equality-hash-and-search",
          # M6: the ordered set - the cluster, and its primitives.
          "nsorderedset-class-answers-a-concrete-class",
          "nsorderedset-alloc-init-is-the-empty-singleton",
          "nsorderedset-mutable-and-class-for-coder",
          "nsorderedset-empty-answers-every-read",
          "nsorderedset-primitives-drive-order-equality-and-hash",
          # M6: NSData - the cluster core (its primitive move is owed).
          "nsdata-class-answers-a-concrete-class",
          "nsdata-empty-is-the-shared-singleton",
          "nsdata-mutable-and-class-for-coder",
          "nsdata-empty-answers-every-read",
          "nsdata-primitives-drive-equality-hash-slicing-and-encoding",
          # M6: NSIndexSet - the cluster core (its primitive move is owed).
          "nsindexset-class-answers-a-concrete-class",
          "nsindexset-alloc-init-is-the-empty-singleton",
          "nsindexset-mutable-and-class-for-coder",
          "nsindexset-empty-answers-every-read",
          "nsindexset-mutable-accumulates",
          "nsindexset-primitives-drive-the-iterator-doors",
          "nsindexset-primitives-drive-the-bulk-door",
          # M7: NSHashTable - a front, not a cluster.
          "nshashtable-archiver-answer-and-empty-instance",
          "nshashtable-primitives-drive-every-read",
          "nshashtable-primitives-drive-fast-enumeration",
          # M7: NSPointerArray - a mutable front.
          "nspointerarray-archiver-answer-and-reads",
          "nspointerarray-fast-enumeration-walks-the-slots",
          "nspointerarray-primitives-drive-allobjects-and-enumeration",
          "nspointerarray-primitives-drive-fast-enumeration",
          # M7: NSAttributedString - a front with a public mutable subclass.
          "nsattributedstring-archiver-answer-for-front-and-public-subclass",
          "nsattributedstring-primitives-drive-length-hash-and-attribute",
          # M7: NSMapTable - a front whose three doors landed over the primitives.
          "nsmaptable-answer-and-own-reads",
          "nsmaptable-primitives-drive-the-three-doors",
          "nsmaptable-primitives-drive-dictionaryrepresentation",
          "nsmaptable-primitives-drive-objectenumerator",
          "nsmaptable-primitives-drive-fast-enumeration",
          # M8: NSNotification - not a cluster; item 4 and nothing else.
          "nsnotification-archiver-answer-and-payload",
          )
# NOTE: the three COMPILE-PROBE checks below are NOT in this tuple, and that is deliberate - this tuple
# is matched against the GUEST probe's stdout, and the compile probe prints nothing there: it is a
# host-side check reported through -check(), exactly like shell-ready and probe-ran.


class Case(BaseCase):
    title = "the class-cluster mechanism: private concrete classes, and the archiver's hinge"
    tier = "fast"
    # The probe defines its own classes and touches no fixture, so it can share a guest like the rest.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_clusters")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-CLUSTERS-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-CLUSTERS "):
                self.note(line)

        done = "FOUNDATION-CLUSTERS DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-CLUSTERS DONE; output tail: "
                        + out.strip()[-400:])
        if not done:
            return

        # NOT ANCHORED AT `^`: the probe prints its checks INDENTED (the style the sibling probes use),
        # and the first version of this case anchored anyway — it matched nothing while the probe's own
        # tally said ok=12 fail=0. A FAIL line cannot match this either, because it ends with the detail
        # rather than with " ok".
        missing = [c for c in CHECKS
                   if not re.search(r"FOUNDATION-CLUSTERS %s ok$" % re.escape(c),
                                    out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS),
                           ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-CLUSTERS RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-CLUSTERS-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")

        self._compile_probe()

    # ---- THE COMPILE PROBE (M1's third acceptance bullet) -------------------------------------------
    #
    # A case that only RAN the guest could not see any of this: type arguments are ERASED before IR
    # generation, so the variance of a header is invisible at runtime and only the COMPILER can testify.

    def _objc_syntax_only(self, source, include_root):
        """(exit status, output) for one snippet compiled against `include_root`."""
        work = tempfile.mkdtemp(prefix="ag-compile-probe-")
        try:
            path = os.path.join(work, "snippet.m")
            with open(path, "w") as handle:
                handle.write(source)
            result = subprocess.run(
                [OBJC, "-fsyntax-only", "-fobjc-arc",
                 "-Werror=incompatible-pointer-types",
                 "-Wno-unused-command-line-argument",
                 "-I", include_root, path],
                cwd=REPO, capture_output=True, text=True)
            return result.returncode, (result.stdout or "") + (result.stderr or "")
        finally:
            shutil.rmtree(work, ignore_errors=True)

    def _variance_removed_tree(self):
        """A MIRROR of userland/ in which Foundation/ is a COPY with its `__covariant` removed.

        The mirror is built out of SYMLINKS to everything else, because the headers reach into sibling
        trees (NSGeometry.h includes CoreGraphics/CGGeometry.h): a copy of Foundation/ alone cannot
        compile at all, so it could not tell variance from a missing header. The real tree is untouched.
        """
        work = tempfile.mkdtemp(prefix="ag-compile-probe-neg-")
        userland = os.path.join(REPO, "userland")
        for name in os.listdir(userland):
            os.symlink(os.path.join(userland, name), os.path.join(work, name))
        os.remove(os.path.join(work, "Foundation"))
        shutil.copytree(os.path.join(userland, "Foundation"), os.path.join(work, "Foundation"))
        header = os.path.join(work, "Foundation", "NSArray.h")
        with open(header) as handle:
            text = handle.read()
        with open(header, "w") as handle:
            handle.write(text.replace("__covariant ", ""))
        return work

    def _compile_probe(self):
        userland = os.path.join(REPO, "userland")

        status, output = self._objc_syntax_only(COVARIANT_SNIPPET, userland)
        self.check("covariant-assignment-compiles", status == 0,
                   "a mutable-specialized array converts to a string-specialized one (__covariant)"
                   if status == 0
                   else "the covariant assignment did not compile: " + output.strip()[-300:])

        status, output = self._objc_syntax_only(UNRELATED_SNIPPET, userland)
        self.check("unrelated-specialization-is-refused", status != 0,
                   "an unrelated specialization does not convert"
                   if status != 0
                   else "the unrelated assignment COMPILED, so this check asserts nothing - the flag that "
                        "makes the refusal a failure is missing")

        status, output = self._objc_syntax_only(UNPARAMETERIZED_SNIPPET, userland)
        self.check("an-unparameterized-class-refuses-type-arguments", status != 0,
                   "a class Apple measured as NOT parameterized refuses type arguments (the measurement "
                   "M4 rests on, asserted rather than assumed)"
                   if status != 0
                   else "NSNumber ACCEPTED type arguments, so it is parameterized after all and the "
                        "parameterization clause has a blind spot")

        for label, snippet in (("NSAttributedString", UNPARAMETERIZED_ATTRIBUTED_SNIPPET),
                               ("NSPointerArray", UNPARAMETERIZED_POINTERARRAY_SNIPPET)):
            status, output = self._objc_syntax_only(snippet, userland)
            self.check("an-unparameterized-family-refuses-type-arguments-%s" % label.lower(), status != 0,
                       "%s refuses type arguments, as measured" % label
                       if status != 0
                       else "%s ACCEPTED type arguments, so the parameterization clause has a blind spot" % label)

        for label, ident, decl in UNPARAMETERIZED_M8_SNIPPETS:
            snippet = UNPARAMETERIZED_M8_TEMPLATE % (label, ident.lower(), decl)
            status, output = self._objc_syntax_only(snippet, userland)
            self.check("an-unparameterized-family-refuses-type-arguments-%s" % label.lower(), status != 0,
                       "%s refuses type arguments, as measured" % label
                       if status != 0
                       else "%s ACCEPTED type arguments, so the parameterization clause has a blind spot" % label)

        negative = self._variance_removed_tree()
        try:
            status, output = self._objc_syntax_only(COVARIANT_SNIPPET, negative)
        finally:
            shutil.rmtree(negative, ignore_errors=True)
        self.check("the-instrument-is-not-vacuous",
                   status != 0 and "incompatible" in output,
                   "with __covariant removed from a COPY the same snippet fails for THAT reason, so the "
                   "first check is measuring the header and not the compiler's goodwill"
                   if status != 0 and "incompatible" in output
                   else "removing __covariant changed nothing (status=%d): the covariance check proves "
                        "nothing" % status)
