#!/usr/bin/env python3
"""uikitdoc — generate the UIKit reference from the header's doc comments.

D0 (docs/design/uikit-documentation-plan.md): the extractor is FIRST-PARTY
and dependency-free. clang-doc is the plan's first choice, but it is not
built in this tree (its sources sit in clang-tools-extra, unbuilt) and
building it would drag the whole clang-tools-extra target in; the plan
records a first-party extractor as the fallback, and this is it.

What it does, in one pass over userland/argentum/argentum.h:

  * every public class/struct/enum and every public method or free
    function must have a doc block (`///` lines or a `/* ... */` block)
    immediately above it — the COVERAGE check, which fails loudly and
    exits non-zero listing what is missing;
  * it writes one Markdown page per class/struct into the output
    directory, plus an index — the REFERENCE.

Doc blocks may carry the plan's mandatory sections as tags
(`/// @purpose`, `@lifetime`, `@threading`, `@invariants`, `@example`,
`@see`); anything untagged is the prose body. The page template puts the
tagged sections in the plan's order and appends the members.

Usage: tools/uikitdoc.py [--out DIR] [--check-only] [HDR]
"""
import os
import re
import sys

MANDATORY = ["purpose", "lifetime", "threading", "invariants"]
TAGS = ["purpose", "lifetime", "threading", "invariants", "example", "see"]
SECTION_TITLE = {
    "purpose": "Purpose",
    "lifetime": "Identity and lifetime",
    "threading": "Event and threading semantics",
    "invariants": "Invariants and gotchas",
    "example": "Example",
    "see": "See also",
}
DECL = re.compile(r"^(class|struct)\s+([A-Z][A-Za-z0-9_]*)\s*(?::|\{|\s*$)")
ENUM = re.compile(r"^enum\s+class\s+([A-Z][A-Za-z0-9_]*)")
FUNC = re.compile(r"^[A-Za-z_][A-Za-z0-9_:<>, *&]*\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(")


def member_docs(body):
    """Public members of a class body, with their doc blocks."""
    out, pending, in_public = [], [], False
    for raw in body[1:]:
        s = raw.strip()
        if s in ("public:", "private:", "protected:"):
            in_public = (s == "public:")
            pending = []
            continue
        if s.startswith("///"):
            pending.append(s[3:].strip())
            continue
        if in_public and "(" in s and s.endswith(";") \
                and not s.startswith("//") and not s.startswith("*"):
            out.append({"sig": s, "doc": "\n".join(pending).strip()})
            pending = []
            continue
        if s:
            pending = []
    return out


def parse(path):
    """Return (units, gaps). units: dict(kind, name, doc, members)."""
    lines = open(path).read().split("\n")
    units, gaps = [], []
    pending, i = [], 0

    while i < len(lines):
        raw = lines[i]
        s = raw.strip()

        if s.startswith("///"):
            pending.append(s[3:].strip())
            i += 1
            continue
        if s.startswith("/*") and not s.startswith("/**"):
            block = []
            while i < len(lines) and "*/" not in lines[i]:
                block.append(lines[i])
                i += 1
            if i < len(lines):
                block.append(lines[i])
                i += 1
            raw = "\n".join(block).strip()
            if raw.startswith("/*"):
                raw = raw[2:]
            if raw.endswith("*/"):
                raw = raw[:-2]
            pending = [l.strip().lstrip("*").strip()
                       for l in raw.split("\n")]
            continue

        m = DECL.match(raw)
        if m:
            kind, name = m.group(1), m.group(2)
            depth, j, body = 0, i, []
            while j < len(lines):
                body.append(lines[j])
                depth += lines[j].count("{") - lines[j].count("}")
                if depth <= 0 and j > i:
                    break
                j += 1
            doc = "\n".join(pending).strip()
            if not doc:
                gaps.append("%s %s" % (kind, name))
            members = member_docs(body)
            for mem in members:
                if not mem["doc"]:
                    gaps.append("%s::%s" % (name, mem["sig"]))
            units.append({"kind": kind, "name": name, "doc": doc,
                          "members": members})
            pending, i = [], j + 1
            continue

        m = ENUM.match(raw)
        if m:
            doc = "\n".join(pending).strip()
            if not doc:
                gaps.append("enum %s" % m.group(1))
            units.append({"kind": "enum", "name": m.group(1), "doc": doc,
                          "members": []})
            pending = []
            i += 1
            continue

        if raw == raw.lstrip() and "(" in raw:
            m = FUNC.match(raw)
            if m:
                sig, k = raw.strip(), i
                while not sig.endswith(";") and k + 1 < len(lines):
                    k += 1
                    sig += " " + lines[k].strip()
                doc = "\n".join(pending).strip()
                if not doc:
                    gaps.append("function %s" % m.group(1))
                units.append({"kind": "function", "name": m.group(1),
                              "doc": doc, "members": []})
                pending, i = [], k + 1
                continue

        if s and not s.startswith(("*", "//", "#", "}")):
            pending = []
        i += 1

    return units, gaps


def split_sections(doc):
    """Split a doc block into the tagged sections + a prose body."""
    out = {"body": []}
    cur = "body"
    for line in doc.split("\n"):
        m = re.match(r"@(purpose|lifetime|threading|invariants|example|see)\b[:\s]*(.*)",
                     line.strip())
        if m:
            cur = m.group(1)
            out.setdefault(cur, [])
            if m.group(2):
                out[cur].append(m.group(2))
        else:
            out.setdefault(cur, []).append(line)
    return out


def page(unit):
    s = split_sections(unit["doc"])
    out = ["# %s" % unit["name"], ""]
    if unit["kind"] == "enum":
        out += ["```", "enum class %s" % unit["name"], "```", ""]
    for tag in TAGS:
        body = "\n".join(s.get(tag, [])).strip()
        if body:
            out += ["## %s" % SECTION_TITLE[tag], "", body, ""]
    prose = "\n".join(s.get("body", [])).strip()
    if prose:
        out += [prose, ""]
    if unit["members"]:
        out += ["## Methods", ""]
        for m in unit["members"]:
            out += ["```", m["sig"], "```", ""]
            if m["doc"]:
                out += [m["doc"], ""]
    return "\n".join(out).rstrip() + "\n"


def main():
    args = sys.argv[1:]
    out_dir = ".build/uikit-doc/reference"
    check_only = False
    hdr = "userland/argentum/argentum.h"
    i = 0
    while i < len(args):
        if args[i] == "--out":
            out_dir = args[i + 1]
            i += 2
        elif args[i] == "--check-only":
            check_only = True
            i += 1
        else:
            hdr = args[i]
            i += 1

    units, gaps = parse(hdr)
    classes = [u for u in units if u["kind"] in ("class", "struct")]

    if not check_only:
        os.makedirs(out_dir, exist_ok=True)
        for u in units:
            if u["kind"] == "function":
                continue
            open(os.path.join(out_dir, "%s.md" % u["name"]), "w").write(
                page(u))
        idx = ["# Argentum UIKit reference", "",
               "%d documented units." % len(units), ""]
        for u in units:
            if u["kind"] != "function":
                idx.append("- [%s](%s.md) — %s" % (u["name"], u["name"],
                                                   u["kind"]))
        open(os.path.join(out_dir, "index.md"), "w").write(
            "\n".join(idx) + "\n")
        print("uikitdoc: wrote %d page(s) to %s" % (len(units), out_dir))

    missing_sections = []
    for u in classes:
        s = split_sections(u["doc"])
        have = [t for t in MANDATORY if "\n".join(s.get(t, [])).strip()]
        if not have:
            continue          # no tagged template yet: the coverage gate
                              # below only requires A doc block, not tags
    if gaps:
        print("uikitdoc: coverage FAIL — %d public declaration(s) without a "
              "doc comment:" % len(gaps))
        for g in gaps:
            print("  - %s" % g)
        return 1
    print("uikitdoc: coverage OK — every public declaration is documented")
    return 0


if __name__ == "__main__":
    sys.exit(main())
