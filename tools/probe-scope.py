# -*- coding: utf-8 -*-
"""SCOPE-AWARE probe splitting: which classes does each `check(...)` actually depend on?

WHY THIS EXISTS, AND WHY THE OBVIOUS VERSION IS WRONG. Removing a Foundation class means removing every probe check that
USES it, or the probe stops compiling (it imports the umbrella header, so it sees whatever Foundation declares). The
first attempt split the file by LINE RANGES between successive `check(` calls and was wrong in both directions --
MEASURED: it killed `pnc-securecoding` as an NSListFormatter check and SPARED `unit-length`, `unit-mass` and
`unit-angle`, which plainly test NSUnit subclasses. THE CAUSE IS THAT A CHECK'S SUBJECT IS DECLARED BEFORE ITS `check(`
CALL, so a range beginning at `check(` begins after the evidence.

THE RULE HERE: a check is attributed to the INNERMOST ENCLOSING BRACE-DELIMITED SCOPE, because that is where its subject
lives. `--scope` reports with the innermost scope only; `--any` widens to every enclosing scope, WHICH OVER-ATTRIBUTES
AND IS THEREFORE THE SAFE DIRECTION -- a dangling reference breaks the BUILD, while a needlessly removed check costs a
test and nothing else.

  python3 tools/probe-scope.py userland/tests/foundation_formatters.m --names NSUnit,NSMeasurement --scope
  python3 tools/probe-scope.py <probe> --check unit-length,pnc-securecoding --names NSUnit,NSMeasurement
"""
import io, re, sys


# !! THE FLOOR, ADDED AFTER TWO OVER-REMOVALS (2026-10-01, §63.142/§63.143). A scope-remover with no floor is a hole
# exactly the size of `main`: when the subject of a check sits in the FUNCTION BODY, "the innermost enclosing scope" IS
# the function, and removing it deletes the probe. Measured twice -- 15 checks taken from each of two probes the first
# time, 16 plus a syntax error the second. SO ANY REMOVAL MUST REFUSE A SPAN LARGER THAN THIS FRACTION OF THE FILE, and
# a caller that is not removing anything (this file's own reporting mode) must say out loud when it would have refused.
MAX_REMOVE_FRACTION = 0.25


def removable(text, start, end):
    """Would removing this span be safe? False means THE SPAN IS TOO LARGE TO BE A CHECK'S BLOCK."""
    return (end - start) <= MAX_REMOVE_FRACTION * len(text)


def scopes(text):
    """[(start, end, depth)] for every brace-delimited block, innermost resolvable by containment."""
    out, stack = [], []
    i = 0
    n = len(text)
    while i < n:
        ch = text[i]
        if ch == '"':                                  # skip string literals and char literals
            i += 1
            while i < n and text[i] != '"':
                i += 2 if text[i] == "\\" else 1
        elif ch == "'":
            i += 1
            while i < n and text[i] != "'":
                i += 2 if text[i] == "\\" else 1
        elif ch == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                i += 1
        elif ch == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            i = j + 1 if j >= 0 else n
        elif ch == "{":
            stack.append(i)
        elif ch == "}":
            if stack:
                out.append((stack.pop(), i, len(stack)))
        i += 1
    return out


def checks(text):
    return [(m.start(), m.group(1)) for m in re.finditer(r'check\(\s*"([a-z0-9-]+)"', text)]


def main():
    argv = sys.argv[1:]
    if not argv:
        print(__doc__)
        return 2
    path = argv[0]
    names = []
    for i, a in enumerate(argv):
        if a == "--names" and i + 1 < len(argv):
            names = [x for x in argv[i + 1].split(",") if x]
    only = []
    for i, a in enumerate(argv):
        if a == "--check" and i + 1 < len(argv):
            only = [x for x in argv[i + 1].split(",") if x]
    any_scope = "--any" in argv
    text = io.open(path, encoding="utf-8", errors="replace").read()
    sc = scopes(text)
    ch = checks(text)
    # !! PREFIX MATCHING, NOT WHOLE-WORD: `\bNSUnit\b` does NOT match `NSUnitLength`, so a family name never matched its
    # own subclasses and the splitter under-attributed every unit check. A family in this tree is a NAME PREFIX.
    pat = {n: re.compile(r"\b" + re.escape(n) + r"[A-Za-z0-9_]*\b") for n in names}
    rows = []
    for pos, name in ch:
        encl = [(s, e) for (s, e, _d) in sc if s < pos < e]
        if not encl:
            rows.append((name, [], ""))
            continue
        picked = encl if any_scope else [max(encl, key=lambda se: se[0])]
        hit = sorted({n for n, p in pat.items() for (s, e) in picked if p.search(text[s:e])})
        rows.append((name, hit, "innermost" if not any_scope else "any"))
    if only:
        print("%-34s %s" % ("CHECK", "DEPENDS ON"))
        for name, hit, mode in rows:
            if name in only:
                print("%-34s %s" % (name, ", ".join(hit) if hit else "(nothing in scope)"))
        return 0
    hit_rows = [r for r in rows if r[1]]
    print("%s: %d check(s), %d depend on %s" % (path, len(rows), len(hit_rows), ", ".join(names)))
    for name, hit, _m in hit_rows:
        print("   %-34s %s" % (name, ", ".join(hit)))
    print("   SURVIVE: %s" % ", ".join(r[0] for r in rows if not r[1]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
