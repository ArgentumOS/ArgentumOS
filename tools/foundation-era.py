# -*- coding: utf-8 -*-
# WHAT IS THIS TREE'S SURFACE AT A macOS BASELINE? Report only; nothing is deleted.
# THE RULE, AFTER THREE WRONG VERSIONS: THE NEAREST NON-COMMENT, NON-MACRO LINE ABOVE A CLASS DECLARATION DECIDES.
#   - if it is a STANDALONE `API_AVAILABLE(macos(X.Y), ...)` line -- that is the class's introduction version;
#   - anything else (a `} API_AVAILABLE(...)` closing an enum, an `extern ... API_AVAILABLE(...)`, `NS_SWIFT_SENDABLE`
#     - which is MACRO-skipped - or plain code) means THE CLASS CARRIES NO ANNOTATION.
# !!!! AND THE ASYMMETRY THAT FOLLOWS IS THE FINDING: THE GROUND IS SOUND IN THE CUT DIRECTION AND UNSOUND IN THE KEEP
# DIRECTION. Annotated-later => cut. But UNANNOTATED DOES NOT MEAN 10.0 -- NSLocale, NSCalendar and NSDateComponents are
# 10.4 and carry no annotation at all -- so those are LISTED as undecided rather than assumed.
import io, os, re, glob, sys
CORPUS, TREE = "/tmp/mac145/", "/home/kyle/Development/Fiwix/userland/Foundation/"
BASE = tuple(int(x) for x in (sys.argv[1] if len(sys.argv) > 1 else "10.2").split("."))
def vt(s):
    m = re.match(r"(\d+)\.(\d+)", s or "")
    return (int(m.group(1)), int(m.group(2))) if m else None
MACRO = re.compile(r"^(?:[A-Z_][A-Z0-9_]*|NS_HEADER_AUDIT_(?:BEGIN|END)\s*\(.*\)|NS_REFINED_FOR_SWIFT|NS_SWIFT_SENDABLE)$")
def ann(line):
    s = line.strip()
    if not re.match(r"^(API_AVAILABLE|NS_AVAILABLE|NS_CLASS_AVAILABLE)", s):
        return None                      # !! standalone only: `} API_AVAILABLE(\u2026)` belongs to what it terminates
    m = re.search(r"macos\s*\(?\s*([\d.]+)\s*\)?", s)
    return vt(m.group(1)) if m else None
def ann_inline(line):
    """A MEMBER's version: the annotation rides the END of the declaration, not a standalone line."""
    if "API_AVAILABLE" not in line:
        return None
    m = re.search(r"macos\s*\(?\s*([\d.]+)\s*\)?", line)
    return vt(m.group(1)) if m else None
cls = {}
for h in glob.glob(CORPUS + "*.h"):
    lines = io.open(h, encoding="utf-8", errors="replace").read().splitlines()
    for i, l in enumerate(lines):
        m = re.match(r"@(?:interface|protocol)\s+(\w+)\s*(.*)$", l)
        if not m or m.group(2).lstrip().startswith("("):
            continue                                     # a CATEGORY, not the class declaration
        name = m.group(1)
        v = ann(l)
        if v is None:
            for j in range(i - 1, max(-1, i - 10), -1):
                p = lines[j].strip()
                if not p or p.startswith("*") or p.startswith("//") or MACRO.match(p):
                    continue
                v = ann(lines[j])                        # !! annotation FIRST -- it ends with `)`, so a terminator test
                break                                    #   before this one broke on the line being looked for
        cls.setdefault(name, v)
# !! AND THE CORPUS PARSE ABOVE IS NOW A FALLBACK, NOT THE GROUND: docs/reference/foundation-era.txt carries Apple's
# OWN introducedAt for 187 owners, collected by tools/foundation-era-fetch.py, and it WINS where it has a name. It has
# to: the header ground is sound for cutting and unsound for keeping, MEASURED -- NSUserNotification is a 10.8 class
# whose header annotates NEITHER the class nor any of its 10 members, which is why the header ground's cut came out 34
# classes short of the verified 85.
ART = os.path.join(os.path.dirname(os.path.dirname(TREE.rstrip("/"))), "docs/reference/foundation-era.txt")
ART_GROUND = {}
if os.path.exists(ART):
    for _line in io.open(ART, encoding="utf-8"):
        if _line.startswith("#") or not _line.strip():
            continue
        _f = _line.rstrip("\n").split("\t")
        if len(_f) >= 2 and vt(_f[1]):
            ART_GROUND[_f[0]] = vt(_f[1])
for _n, _v in ART_GROUND.items():
    cls[_n] = _v                                 # the artefact replaces the header reading wherever it speaks
def members(c):
    p = CORPUS + c + ".h"
    if not os.path.exists(p):
        return None
    tot = late = 0
    for l in io.open(p, encoding="utf-8", errors="replace").read().splitlines():
        if not re.match(r"\s*(?:[-+]\s*\(|@property)", l):
            continue
        tot += 1
        a = ann_inline(l)                            # !! members carry their annotation INLINE, at the end of the
        if a and a > BASE:                           #    declaration - `ann()` requires a standalone line and would
            late += 1                                #    read every member as unannotated, which is how this read 0
    return tot, late
ours = sorted(os.path.basename(p)[:-2] for p in glob.glob(TREE + "*.h"))
cut, keep, undec, noc = [], [], [], []
for c in ours:
    if c not in cls: noc.append(c)
    elif cls[c] and cls[c] > BASE: cut.append((c, "%d.%d" % cls[c]))
    else:
        keep.append(c)
        if cls[c] is None: undec.append(c)
mt = ml = 0
for c in keep:
    r = members(c)
    if r: mt += r[0]; ml += r[1]
print("BASELINE macOS %d.%d  -- REPORT ONLY, nothing deleted" % BASE)
print("  headers %d   |   CUT %d   |   KEEP %d   |   no corpus header %d" % (len(ours), len(cut), len(keep), len(noc)))
print("  members cut from keepers: %d of %d declarations (%.0f%%)" % (ml, mt, 100.0*ml/max(1,mt)))
print()
era = {}
for c, v in cut: era.setdefault(v, []).append(c)
print("  CUT (annotated later than %d.%d), %d classes:" % (BASE + (len(cut),)))
for e in sorted(era, key=vt):
    print("    %-6s %3d  %s" % (e, len(era[e]), ", ".join(sorted(era[e]))))
print()
print("  !! UNDECIDED -- unannotated, so the header does not say 10.0 vs 10.1\u201310.4: %d classes" % len(undec))
io.open("/tmp/undecided.txt", "w").write("\n".join(undec))
print("     written to /tmp/undecided.txt")
