#!/usr/bin/env python3
# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""Foundation's BEHAVIOURAL coverage: every shipped selector against the tests that touch it.

WHY THIS EXISTS, and it is the gap the ledger cannot see. docs/reference/foundation-selector-surface.txt
says whether a selector is DECLARED; it says nothing about whether anything ever calls it with an assertion
attached. The project's own bar (docs/design/foundation-plan.md §11) puts SEMANTICS and ERRORS on the
failure side, and the probes' `inventory` arrays assert only that a name EXISTS — which cannot fail for the
reason behaviour can.

THE ORACLE IS SPECIFICATION + INVARIANTS (user decision, 2026-10-04): there is no Apple Foundation on this
host (linux/amd64, no Darwin), so "acts like Apple's" can never be settled by running both and diffing. A
check either encodes a published contract or a law Apple's Foundation satisfies — and a violated law is
proof of divergence.

THREE TIERS, AND THE MIDDLE ONE IS DELIBERATELY WEAK:

  asserted  a probe CLAIMS it, and the claim can only be made beside an assertion that held:
                covers("NSArray", "objectAtIndex:", someAssertion);
            which prints `COVERS NSArray objectAtIndex:`. A literal `COVERS <Class> <selector>` line in a
            probe's text counts too, for a probe that has no helper. THE ONLY TIER THAT MEANS PROVED.
  named     the probe NAMES the selector — `@selector(objectAtIndex:)`, or a string literal equal to it,
            which is how the inventory arrays are written. The probe touches it. That is NOT a claim that
            anything is asserted about it, and every report says so in those words.
  none      neither. THE WORK LIST — and the reason this mode fails on a NEW one: a selector that ships with
            no check is a door nobody has ever proved.

IT UNDER-CLAIMS ON PURPOSE. A message send that names nothing (`[a objectAtIndex:i]`) is invisible here.
That is the safe direction for an instrument: it can MISS coverage, so the work list runs long, but it
cannot invent coverage that does not exist. The tiers are printed separately for the same reason.

THREE LIMITATIONS, STATED RATHER THAN HIDDEN:
  * a CLASS method and an INSTANCE method with the same selector share one row, because probe text cannot
    tell them apart. A row is covered by its NAME.
  * `named` cannot see through a helper that takes the selector as a parameter.
  * `asserted` is a claim BY THE PROBE, not a proof by this tool: what makes it trustworthy is that the
    helper takes the assertion's result, so a failing assertion cannot print the line. A probe that emitted
    it unconditionally would be lying, and the corpus rule is that `covers()` is the only way to emit it.

IT BELONGS HERE RATHER THAN IN A NEW HARNESS, because the ledger it reads is the sweep's and so is the
discipline: a generated baseline with a reason column that fails only on NEW findings, exactly like
docs/reference/foundation-unimplemented.txt.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LEDGER = os.path.join(ROOT, "docs/reference/foundation-selector-surface.txt")
BASELINE = os.path.join(ROOT, "docs/reference/foundation-coverage.txt")
TESTDIRS = (os.path.join(ROOT, "userland/tests"),)

# The two ways a probe can claim a selector, both literal enough to be audited by eye.
CLAIM_LITERAL_RE = re.compile(r"COVERS\s+([A-Za-z_][A-Za-z0-9_]*)\s+([A-Za-z_][A-Za-z0-9_]*:?)")
CLAIM_HELPER_RE = re.compile(r'\bcovers\s*\(\s*"([A-Za-z_][A-Za-z0-9_]*)"\s*,\s*"([A-Za-z_][A-Za-z0-9_]*:?)"')
# A selector NAMED in probe text: @selector(x:) , or a whole string literal that is exactly a selector.
SEL_RE = re.compile(r"@selector\s*\(\s*([A-Za-z_][A-Za-z0-9_]*:?)")
LIT_RE = re.compile(r'"([A-Za-z_][A-Za-z0-9_]*:?)"')


def shipped():
    """[(owner, signless selector, row kind, row name)] for every SHIPPED selector row."""
    rows = []
    with open(LEDGER, encoding="utf-8") as fh:
        for line in fh:
            if line.startswith("#"):
                continue
            p = line.rstrip("\n").split("\t")
            if len(p) < 4 or p[1] != "shipped":
                continue
            kind, name, owner = p[0], p[2], p[3]
            if kind == "method":
                sel = name[1:] if name[:1] in "-+" else name
            elif kind == "property":
                sel = name            # a property's own name IS its getter's selector
            else:
                continue
            rows.append((owner, sel, kind, name))
    return rows


def evidence():
    """(asserted pairs, every selector name any probe mentions)."""
    asserted, named_any = set(), set()
    for d in TESTDIRS:
        for dirpath, _dirs, names in os.walk(d):
            for name in sorted(names):
                if not name.endswith((".m", ".c")):
                    continue
                with open(os.path.join(dirpath, name), encoding="utf-8", errors="replace") as fh:
                    text = fh.read()
                for owner, sel in CLAIM_LITERAL_RE.findall(text) + CLAIM_HELPER_RE.findall(text):
                    asserted.add((owner, sel))
                    asserted.add((owner, sel.rstrip(":")))
                for sel in SEL_RE.findall(text) + LIT_RE.findall(text):
                    named_any.add(sel)
                    named_any.add(sel.rstrip(":"))
    return asserted, named_any


def classify():
    asserted, named_any = evidence()
    out = []
    for owner, sel, kind, name in shipped():
        if (owner, sel) in asserted or (owner, sel.rstrip(":")) in asserted:
            tier = "asserted"
        elif sel in named_any:
            tier = "named"
        else:
            tier = "none"
        out.append((owner, sel, kind, name, tier))
    return out


def baseline():
    """The accepted gaps, as (owner, selector) KEYS. THE KEY IS THE PAIR, because that is what the gate
    counts: a class method and an instance method of one name are one uncovered hole, not two."""
    keys = set()
    if os.path.exists(BASELINE):
        with open(BASELINE, encoding="utf-8") as fh:
            for line in fh:
                if line.startswith("#") or not line.strip():
                    continue
                p = line.rstrip("\n").split("\t")
                if len(p) >= 2:
                    keys.add((p[0], p[1]))
    return keys


def tiers(rows):
    return {t: sum(1 for r in rows if r[4] == t) for t in ("asserted", "named", "none")}


def report(rows):
    per, tot = {}, tiers(rows)
    for owner, _s, _k, _n, tier in rows:
        per.setdefault(owner, {"asserted": 0, "named": 0, "none": 0})[tier] += 1
    distinct = len({(o, s) for o, s, _k, _n, t in rows if t == "none"})
    print("Foundation behavioural coverage — %d shipped selector ROWS across %d owners" % (len(rows), len(per)))
    print("  asserted (a probe claims behaviour, beside an assertion that held): %5d" % tot["asserted"])
    print("  named    (a probe names it; NOT a claim that anything is asserted): %5d" % tot["named"])
    print("  none     (the work list):                                          %5d rows / %d distinct"
          % (tot["none"], distinct))
    print()
    print("  %-34s %7s %8s %6s %6s" % ("owner", "shipped", "asserted", "named", "none"))
    for owner in sorted(per, key=lambda o: -per[o]["none"])[:24]:
        d = per[owner]
        print("  %-34s %7d %8d %6d %6d" % (owner, sum(d.values()), d["asserted"], d["named"], d["none"]))
    print("  (the 24 owners with the most uncovered rows; --by-class prints every owner)")


def main(argv):
    rows = classify()
    if "--write" in argv or "--refresh" in argv:
        distinct = {}
        for owner, sel, kind, name, tier in sorted(rows):
            if tier == "none":
                distinct.setdefault((owner, sel), (kind, name))
        with open(BASELINE, "w", encoding="utf-8") as fh:
            fh.write("# GENERATED by tools/foundation-cov.py --write — do not hand-edit the KEYS.\n")
            fh.write("#\n# A shipped selector NO test touches: it is DECLARED (the ledger says so) and nothing has\n")
            fh.write("# ever proved it behaves. ONE ROW PER (class, selector) PAIR, so a class method and an\n")
            fh.write("# instance method of the same name share a row. THE REASON COLUMN IS YOURS TO FILL IN:\n")
            fh.write("# \"needs a check\" is the work list, and this file IS the aggressive-test queue.\n")
            fh.write("#\n# THE TIERS the generator distinguishes, and what each is worth:\n")
            fh.write("#   asserted  the probe CLAIMS it with covers(\"Class\", \"selector\", assertion), so the claim\n")
            fh.write("#             can only be printed when the assertion held. The target state, and the only\n")
            fh.write("#             tier that means the selector is PROVED.\n")
            fh.write("#   named     the probe names the selector (an @selector or a bare literal, as the inventory\n")
            fh.write("#             arrays do). The probe TOUCHES it; nothing is claimed to be asserted.\n")
            fh.write("#   none      nothing touches it. Every such row is listed below.\n")
            fh.write("#\n# owner\tselector\tkind\tname\n")
            for (owner, sel), (kind, name) in sorted(distinct.items()):
                fh.write("%s\t%s\t%s\t%s\t# needs a check\n" % (owner, sel, kind, name))
        print("wrote %s (%d rows)" % (os.path.relpath(BASELINE, ROOT), len(distinct)))
        report(rows)
        return 0
    if "--by-class" in argv:
        per = {}
        for owner, _s, _k, _n, tier in rows:
            per.setdefault(owner, {"asserted": 0, "named": 0, "none": 0})[tier] += 1
        for owner in sorted(per):
            d = per[owner]
            print("  %-40s shipped %4d  asserted %4d  named %4d  none %4d" % (
                owner, sum(d.values()), d["asserted"], d["named"], d["none"]))
        return 0
    # --check: the baseline is the contract. A NEW uncovered selector fails; a stale row is reported.
    if not os.path.exists(BASELINE):
        print("foundation-cov: %s is MISSING — run tools/foundation-cov.py --write" % os.path.relpath(BASELINE, ROOT))
        return 1
    base = baseline()
    live = {(o, s) for o, s, _k, _n, t in rows if t == "none"}
    new = sorted(live - base)
    gone = sorted(base - live)
    tot = tiers(rows)
    print("foundation-cov: %d shipped selector rows; asserted %d, named %d, uncovered %d distinct (%d in the"
          " baseline)" % (len(rows), tot["asserted"], tot["named"], len(live), len(base)))
    for owner, sel in new:
        print("  NEW UNCOVERED      %-30s %s — a shipped selector with no test touching it" % (owner, sel))
    for owner, sel in gone:
        print("  STALE BASELINE ROW %-30s %s — it is covered now, or no longer shipped; drop the row" % (owner, sel))
    # INERT CLAIMS ARE REPORTED, NOT IGNORED. THE UNIVERSE IS THE LEDGER (user decision, 2026-10-04), so a
    # claim that names a row the ledger does not carry can never count — `-isEqual:`, `-hash` and `-copy` are
    # the usual ones, because the ledger tracks them under NSObject/NSString rather than under every class
    # that answers them. The probe's assertion behind such a claim is real; this gate simply cannot see it.
    # PRINTING THEM IS THE POINT: an inert claim in a probe's source otherwise looks exactly like counted
    # coverage, and the honest reading of one is "this gate cannot express it" — never "proved", and never
    # "unproven" either. Reported, and NOT fatal: the decision is about the gate's universe, not a defect in
    # the probe that made the claim.
    shipped_pairs = {(o, s) for o, s, _k, _n, _t in rows}
    inert = sorted({(o, s) for o, s in evidence()[0] if (o, s) not in shipped_pairs})
    if inert:
        print("  INERT CLAIM (%d) — a probe claims it and the ledger ships no such row, so it counts for"
              " nothing here (the assertion behind it may still be real; this gate cannot see it):" % len(inert))
        for owner, sel in inert:
            print('     covers("%s", "%s")' % (owner, sel))
    report(rows)
    return 1 if (new or gone) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
