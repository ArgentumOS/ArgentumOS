#!/usr/bin/env python3
# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""THE APPLICATION KIT AS IT WAS AT 10.2 — a work list pinned to an SDK, not to today.

WHY A FOURTH SWEEP, AND WHAT IT IS NOT. `tools/appkit-sweep.py` already ledgers the AppKit, but its
ground is `https://developer.apple.com/tutorials/data/index/appkit` — APPLE'S CURRENT DOCUMENTATION: a
10.15-era surface (12,475 rows, `@property` everywhere, `NS_ENUM`, availability annotations, classes
that did not exist until the 2010s). That is the right ground for "match Apple class-for-class" and it
is the WRONG ground for "build the AppKit the way it was in 10.2": the two questions have different
answers, and this file exists because they do.

THIS INSTRUMENT'S GROUND IS THE SDK ITSELF: `MacOSX10.2.8.sdk`'s `AppKit.framework/Headers`, read as
SOURCE rather than through a documentation index. That changes what a row can be, and the changes are
named here rather than left for a reader to discover:

  * THE ERA IS VISIBLE IN THE ROWS. Every 10.2 AppKit header is Objective-C 1.0 — there is not one
    `@property`, not one `@optional`/`@required`, not one `NS_ENUM`, and no availability annotation
    anywhere in the framework (verified by grep over the corpus before this file was written). So this
    ledger's `property` count is 0 BY CONSTRUCTION and accessor pairs are METHODS, which is a fact
    about the era and not a gap in the parser.

  * THE DELEGATE PATTERN OF 10.2 IS A CATEGORY ON `NSObject`, NOT A PROTOCOL. This corpus carries 130
    categories, most of them `@interface NSObject (NSXxxDelegate)` / `(NSXxxDataSource)` /
    `(NSXxxNotifications)`. A clone that reaches for formal protocols would be building 10.15's AppKit.
    They are therefore FIRST-CLASS ROWS with the kind `category`, which no other ledger in this tree has.

  * A ROW'S `src` IS A FILE, NOT A URL. The live ledger cites the page it read a name from; an era
    ground has no URL — the SDK is the source and the header is the citation — so `src` carries
    `AppKit/NSView.h`. That is the one column whose meaning differs, and it differs because the ground
    does.

  * MEMBERS ARE OWNER-SCOPED. The live ledger admits in its own words that it "can over-credit": it
    asks whether a NAME is declared somewhere, so landing `NSGraphicsContext`'s `-flipped` credits
    `NSView`'s and `NSRulerView`'s rows too. Here a method row is shipped only when ITS OWN OWNER
    declares that whole selector, because the corpus is PARSED rather than indexed and the owner's
    block is therefore available. Strictly better, and it costs nothing.

WHAT IT SHARES WITH THE OTHER THREE, DELIBERATELY: the row shape (kind, status, name, owner, family,
why, src), the three-value status vocabulary, the `--refresh` (the only mode that reads the corpus) /
`--check` (offline, a function of this tree alone) split, and the two inconsistency classes `--check`
fails on. `coregraphics-sweep.py` says why: "two sweeps that disagree about what a ledger row IS would
be worse than one".

`struck` EXISTS IN THE VOCABULARY AND CAN NEVER BE ASSIGNED HERE: the era ground is the pin, so there
is nothing later than it to deprecate a row. It is kept so a reader who knows the other ledgers is not
left wondering where it went, and `--check` reports 0 by construction.

THE CORPUS IS NOT IN THIS TREE AND MUST NOT BE (Apple's SDK is not redistributable; the same rule
`tools/foundation-sdk-subset.py` states). `.tmp/` is gitignored for exactly this reason, the fetch is
recorded in the plan document, and the artefact is regenerable from it: the generator ships, the data
does not.

USAGE

  tools/appkit-102-sweep.py --refresh            re-read the 10.2 corpus and rewrite the work list
  tools/appkit-102-sweep.py --check              verify the artefact against our headers (offline)
  tools/appkit-102-sweep.py --work-list [KIND]   print the open rows — the work list itself
  tools/appkit-102-sweep.py --families [--write] print (or rewrite into the plan) the family table
  tools/appkit-102-sweep.py --order              classes by superclass depth: the build order
"""

import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SURFACE = os.path.join(ROOT, "docs/reference/appkit-102-worklist.txt")
PLAN = os.path.join(ROOT, "docs/design/cocoa-parity-plan.md")
OURS = os.path.join(ROOT, "userland/AppKit/*.h")
TOOL = "tools/appkit-102-sweep.py"

# The corpus: an SDK mirror checked out by hand, never committed. APPKIT102_SDK overrides the path so
# the instrument is not welded to one checkout (the .tmp/ tree is mine; someone else's may differ).
CORPUS = os.environ.get(
    "APPKIT102_SDK",
    os.path.join(ROOT, ".tmp/mac102/MacOSX10.2.8.sdk/System/Library/Frameworks/AppKit.framework/Headers"))
SDK_NAME = "MacOSX10.2.8.sdk"
SDK_SOURCE = "https://github.com/phracker/MacOSX-SDKs (published mirror; Apple's SDK is not redistributable)"

STATUS_SHIPPED = "shipped"
STATUS_OPEN = "open"
STATUS_STRUCK = "struck"   # never assigned here — see the docstring

# THE KINDS THAT ARE API. `property` is absent from the extraction because the era has none; `category`
# is a kind no other ledger carries — see the docstring.
KINDS = ("category", "class", "protocol", "method", "enum", "case", "typealias", "struct",
         "func", "var", "macro")

# THE FAMILY TABLE IS OURS AND IS STATED AS A RULE, not read from the corpus: a 10.2 SDK carries no
# taxonomy (the live ledger's `family` column is Apple's documentation's, and there is no equivalent).
# A class that is not in this table is `unassigned` and `--check` FAILS on it, so the table cannot
# silently fall behind the corpus the way the plan's hand-written family table once did (§63, ab5eacd3).
#
# The clusters are the SHAPE OF THE BUILD rather than Apple's marketing taxonomy: bases before leaves,
# one cluster per subsystem, and CLUSTER_ORDER is the order the plan works them in.
CLUSTERS = {
    # the application, its session and its documents
    "NSApplication": "app", "NSWorkspace": "app", "NSDocument": "app", "NSDocumentController": "app",
    "NSHelpManager": "app", "NSInputManager": "app", "NSInputServer": "app",
    "NSInputServiceProvider": "app", "NSInputServerMouseTracker": "app",
    # the responder chain, events and cursors: what a view sits on
    "NSResponder": "responder", "NSEvent": "responder", "NSCursor": "responder",
    # the view tree and the cell architecture
    "NSView": "view-core", "NSControl": "view-core", "NSCell": "view-core",
    "NSActionCell": "view-core", "NSMatrix": "view-core", "NSClipView": "view-core",
    "NSBox": "view-core", "NSInterfaceStyle": "view-core",
    # drawing, colour and transforms
    "NSGraphicsContext": "graphics", "NSBezierPath": "graphics", "NSAffineTransform": "graphics",
    "NSColor": "graphics", "NSColorList": "graphics", "NSColorPanel": "graphics",
    "NSColorPicker": "graphics", "NSColorWell": "graphics",
    "NSColorPickingDefault": "graphics", "NSColorPickingCustom": "graphics",
    "NSQuickDrawView": "graphics",
    # the text system and the fonts it needs
    "NSText": "text", "NSTextField": "text", "NSTextFieldCell": "text",
    "NSSecureTextField": "text", "NSSecureTextFieldCell": "text", "NSTextView": "text",
    "NSTextStorage": "text", "NSTextContainer": "text", "NSLayoutManager": "text",
    "NSTypesetter": "text", "NSSimpleHorizontalTypesetter": "text", "NSTextAttachment": "text",
    "NSTextAttachmentCell": "text", "NSTextTab": "text", "NSParagraphStyle": "text",
    "NSMutableParagraphStyle": "text", "NSFont": "text", "NSFontManager": "text",
    "NSFontPanel": "text", "NSGlyphInfo": "text",
    "NSTextInput": "text", "NSChangeSpelling": "text", "NSIgnoreMisspelledWords": "text",
    # images and image reps
    "NSImage": "images", "NSImageRep": "images", "NSBitmapImageRep": "images",
    "NSCachedImageRep": "images", "NSCustomImageRep": "images", "NSEPSImageRep": "images",
    "NSPDFImageRep": "images", "NSPICTImageRep": "images", "NSImageCell": "images",
    "NSImageView": "images",
    # the leaf controls
    "NSButton": "controls", "NSButtonCell": "controls", "NSSlider": "controls",
    "NSSliderCell": "controls", "NSStepper": "controls", "NSStepperCell": "controls",
    "NSProgressIndicator": "controls", "NSPopUpButton": "controls",
    "NSPopUpButtonCell": "controls", "NSComboBox": "controls", "NSComboBoxCell": "controls",
    "NSForm": "controls", "NSFormCell": "controls",
    # the row-based views
    "NSTableView": "tables", "NSTableColumn": "tables", "NSTableHeaderCell": "tables",
    "NSTableHeaderView": "tables", "NSOutlineView": "tables", "NSBrowser": "tables",
    "NSBrowserCell": "tables",
    # the containers and the bars
    "NSScrollView": "containers", "NSScroller": "containers", "NSSplitView": "containers",
    "NSTabView": "containers", "NSTabViewItem": "containers", "NSRulerView": "containers",
    "NSRulerMarker": "containers", "NSToolbar": "containers", "NSToolbarItem": "containers",
    "NSStatusBar": "containers", "NSStatusItem": "containers",
    # menus
    "NSMenu": "menus", "NSMenuItem": "menus", "NSMenuItemCell": "menus", "NSMenuView": "menus",
    # panels, dialogs and printing
    "NSOpenPanel": "panels", "NSSavePanel": "panels", "NSPageLayout": "panels",
    "NSPrintInfo": "panels", "NSPrintOperation": "panels", "NSPrintPanel": "panels",
    "NSPrinter": "panels",
    # pasteboard, dragging, and the validation the menu and toolbar machinery calls back into
    "NSPasteboard": "pasteboard-drag", "NSDraggingInfo": "pasteboard-drag",
    "NSValidatedUserInterfaceItem": "pasteboard-drag", "NSUserInterfaceValidations": "pasteboard-drag",
    # nib loading and the connectors an Interface Builder file builds a graph out of
    "NSNibConnector": "nib", "NSNibControlConnector": "nib", "NSNibOutletConnector": "nib",
    "NSFileWrapper": "nib",
    # sound and the time-based view
    "NSSound": "media", "NSMovie": "media", "NSMovieView": "media",
    # OpenGL
    "NSOpenGLContext": "opengl", "NSOpenGLPixelFormat": "opengl", "NSOpenGLView": "opengl",
    # the spell checker and its server
    "NSSpellChecker": "spelling", "NSSpellServer": "spelling",
    # WINDOW is its own cluster: the wall between the toolkit and the display it lives on
    "NSWindow": "window", "NSPanel": "window", "NSDrawer": "window", "NSScreen": "window",
    "NSWindowController": "window",
}

# CLUSTER_ORDER is the BUILD ORDER of the clusters, and it is a claim: the bases have to exist before
# the leaves that subclass them, and the infrastructure (responder, view tree, drawing, text) before
# anything that is drawn with it.
CLUSTER_ORDER = ("responder", "view-core", "graphics", "text", "window", "controls", "containers",
                 "menus", "panels", "images", "tables", "pasteboard-drag", "nib", "app", "media",
                 "opengl", "spelling", "foundation-additions")

# CATEGORIES ON FOUNDATION'S CLASSES: 10.2's AppKit extends Foundation's classes rather than its own
# (NSStringDrawing, NSAttributedString's AppKit additions, NSBundle's nib/image/sound loading, …). They
# are a cluster of their own because their work is "write a category the substrate's class can carry".
FOUNDATION_CLASSES = {"NSObject", "NSString", "NSMutableString", "NSBundle", "NSAttributedString",
                      "NSMutableAttributedString", "NSCoder", "NSURL", "NSAppleScript", "NSData",
                      "NSArray", "NSDictionary"}

# THE ODD CATEGORIES THE PREFIX RULE CANNOT PLACE, named explicitly rather than left to land in the
# Foundation bucket: NSToolTipOwner is a VIEW feature with an NSObject base, and NSAccessibility is the
# 10.2 accessibility surface, which is view geometry by another name.
CATEGORY_CLUSTERS = {
    "NSToolTipOwner": "view-core",
    "NSAccessibility": "view-core",
    "NSStandardKeyBindingMethods": "responder",
    "NSUndoSupport": "responder",
    "NSDraggingDestination": "pasteboard-drag",
    "NSDraggingSource": "pasteboard-drag",
    "NSPasteboardOwner": "pasteboard-drag",
}

# THE COMPLETENESS INSTRUMENT. An incomplete corpus turns "we misspelled it" into "Apple does not have
# it" — the one way this file can lie — so every name below is API that MUST be in any 10.2 AppKit. A
# missing one is a fact about the CORPUS and is reported as such, exactly as foundation-sdk-subset.py
# does it. (Checked 2026-10-03: all present.)
#
# `NSAccessibility` IS ABSENT ON PURPOSE, and its absence is an era finding rather than an oversight: at
# 10.2 accessibility is neither a class nor a protocol — it is `@interface NSObject (NSAccessibility)`,
# an informal category of methods a view implements. It is asserted in its category form below.
CORPUS_MUST_DECLARE = (
    "NSActionCell", "NSAffineTransform", "NSApplication", "NSBezierPath",
    "NSBitmapImageRep", "NSBox", "NSBrowser", "NSButton", "NSButtonCell", "NSCell", "NSClipView",
    "NSColor", "NSControl", "NSCursor", "NSDocument", "NSEvent", "NSFont", "NSFontManager", "NSForm",
    "NSGraphicsContext", "NSImage", "NSImageRep", "NSImageView", "NSLayoutManager", "NSMatrix",
    "NSMenu", "NSMenuItem", "NSMovieView", "NSNibConnector", "NSOpenPanel", "NSOutlineView",
    "NSPanel", "NSParagraphStyle", "NSPasteboard", "NSPopUpButton", "NSPrinter", "NSPrintInfo",
    "NSPrintOperation", "NSProgressIndicator", "NSResponder", "NSSavePanel", "NSScreen", "NSScroller",
    "NSScrollView", "NSSlider", "NSSound", "NSSpellChecker", "NSSplitView", "NSStatusBar",
    "NSTableColumn", "NSTableView", "NSTabView", "NSText", "NSTextAttachment", "NSTextContainer",
    "NSTextField", "NSTextStorage", "NSTextView", "NSView", "NSWindow", "NSWindowController",
    "NSWorkspace")

# ...and the categories that MUST be there, checked the same way. NSAccessibility is the one that
# matters (it is the era's whole accessibility surface), and the two dragging ones are the era's whole
# drag-and-drop contract.
CORPUS_MUST_CATEGORIZE = ("NSAccessibility", "NSDraggingDestination", "NSDraggingSource")

# THE HEADERS THAT DECLARE NO CLASS AND NO PROTOCOL, and therefore cannot be clustered by what they
# declare: the kit's own macro header, the drawing FUNCTIONS, the error strings, and the Interface
# Builder attribute macros. Named here rather than defaulted, because a default would silently absorb
# the next container-less header that appears. EVERY ENTRY HERE IS FOR A HEADER THAT REALLY DECLARES NO
# CONTAINER (checked: `rg -c '^@(interface|protocol)'` is 0 for each) — `NSSpellProtocol.h` was on this
# list and had to come off, because it declares two protocols and its cluster is therefore DERIVED.
# (An empty `unassigned` is what proves the list is complete: --refresh reports it, --check fails on it.)
FILE_HINTS = {
    "AppKitDefines.h": "view-core",     # version numbers and the geometry macros the views use
    "NSGraphics.h": "graphics",         # NSRectFill, NSFrameRect, the drawing FUNCTIONS
    "NSErrors.h": "app",                # the AppKit exception names
    "NSNibDeclarations.h": "nib",       # IBOutlet/IBAction and the IB attribute macros
}


def file_clusters(parsed):
    """{filename: cluster} — a top-level constant belongs with the header that declares it.

    THE RULE FOR A ROW THAT HAS NO CONTAINER. A `case`, a `var`, a `func`, an `enum` or a `macro`
    belongs to no class, so the class-based lookup cannot place it; what places it is the FILE it was
    read from, whose cluster is the cluster of the FIRST container that file declares (`NSWindow.h`'s
    window-level constants belong with NSWindow). A file that declares no container at all falls back
    to FILE_HINTS. This is the SDK's own layout plus OUR stated table — the same shape every other
    column here has."""
    out = {}
    for fname, rows in parsed.items():
        out[fname] = None
        for rd in rows:
            if rd["kind"] in ("class", "protocol", "category"):
                c = cluster_of(rd["kind"], rd["name"], rd["owner"])
                if c != "unassigned":
                    out[fname] = c
                    break
        out[fname] = out[fname] or FILE_HINTS.get(fname) or "unassigned"
    return out


def read_corpus(directory):
    """{filename: text} for every header in the SDK directory."""
    out = {}
    for path in sorted(glob.glob(os.path.join(directory, "*.h"))):
        out[os.path.basename(path)] = open(path, encoding="utf-8", errors="replace").read()
    return out


def strip_comments(text):
    """Remove comments; string literals are left alone; NEWLINES ARE PRESERVED, as spaces.

    That last part is load-bearing rather than tidy: the parser below is line-oriented, and a
    stripper that collapsed a comment to nothing would glue the line before it to the line after it
    and invent declarations that are in no header. (Not hypothetical —
    `NSUserInterfaceValidation.h` documents its protocol with a fenced example inside `/* … */` that
    contains `- (void)update {` and `@interface`-shaped text.)"""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join("\n" if ch == "\n" else " " for ch in text[i:j]))
            i = j
        elif c == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def join_decl(lines, i):
    """A declaration from lines[i] up to and including the line that closes it: the first line where
    the brace depth is back to zero AND the accumulated text ends in `;`.

    A MULTI-LINE OBJECTIVE-C METHOD IS THE REASON THIS EXISTS — `- (void)curveToPoint:(NSPoint)endPoint
    controlPoint1:…` takes three lines in `NSBezierPath.h`, and a line-at-a-time reader would see three
    declarations: two with no selector and one fragment. Every declaration form in this corpus either
    ends in `;` at depth 0 or is the last thing in its file."""
    buf = []
    depth = 0
    n = len(lines)
    while i < n:
        buf.append(lines[i])
        depth += lines[i].count("{") - lines[i].count("}")
        text = " ".join(buf).strip()
        if text.endswith(";") and depth <= 0:
            return text, i + 1
        i += 1
    return " ".join(buf).strip(), n


def join_hash(lines, i):
    """A preprocessor line, with backslash continuations."""
    buf = []
    n = len(lines)
    while i < n:
        buf.append(lines[i].rstrip())
        if not lines[i].rstrip().endswith("\\"):
            return " ".join(x.rstrip("\\").strip() for x in buf), i + 1
        i += 1
    return " ".join(buf), n


def selector_of(decl):
    """The SELECTOR a method declaration declares, with its sign — `- setFrame:display:`.

    The FULL selector, not its first component: the live ledger reduces Apple's page titles to the
    first component because that is all its declaration test can see, and it therefore cannot tell
    `- drawInRect:` from `- drawInRect:withAttributes:`. Here the declaration is parsed, so the whole
    selector is available and keeping it costs nothing — and distinct rows are the difference between
    a work list and a name list."""
    d = decl.strip()
    if not d or d[0] not in "+-":
        return ""
    sign, d = d[0], d[1:].strip()
    if d.startswith("("):                       # drop the return type, balanced
        depth = 0
        for k, ch in enumerate(d):
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
                if depth == 0:
                    d = d[k + 1:]
                    break
    kws = re.findall(r"([A-Za-z_]\w*)\s*:", d)
    if kws:
        return sign + " " + "".join(k + ":" for k in kws)
    m = re.match(r"([A-Za-z_]\w*)", d)
    return (sign + " " + m.group(1)) if m else ""


def enum_cases(decl):
    """The enumerators of a `typedef enum … { … } Name;` or `enum { … };`."""
    start = decl.find("{")
    if start < 0:
        return []
    depth, end = 0, -1
    for k in range(start, len(decl)):
        if decl[k] == "{":
            depth += 1
        elif decl[k] == "}":
            depth -= 1
            if depth == 0:
                end = k
                break
    if end < 0:
        return []
    out, depth, cur = [], 0, ""
    for ch in decl[start + 1:end]:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur)
            cur = ""
        else:
            cur += ch
    out.append(cur)
    names = []
    for part in out:
        m = re.match(r"\s*([A-Za-z_]\w*)", part)
        if m:
            names.append(m.group(1))
    return names


def tail_name(decl):
    """The last identifier before the terminating `;` — a typedef's own name, or the declared
    variable's. `typedef enum _NSBorderType { … } NSBorderType;` → NSBorderType, and
    `APPKIT_EXTERN NSString * const NSWindowDidBecomeKeyNotification;` → the notification's name."""
    d = decl.rstrip().rstrip(";").strip()
    d = re.sub(r"\[[^\]]*\]\s*$", "", d)        # `NSFoo[4]` declares NSFoo
    m = re.findall(r"([A-Za-z_]\w*)\s*$", d)
    return m[0] if m else ""


def parse(text):
    """Every row declaration in one header, in source order.

    ONE PASS, THREE STATES: no container (top level — enums, typedefs, externs, macros), a container
    (`@interface`/`@protocol`, whose members belong to it), and INSIDE A CONTAINER'S IVAR BLOCK, which
    is skipped by brace counting. That third state matters here in a way it never did in the modern
    SDK: a 10.2 `@interface` carries a `{ … }` block of ivars, and those blocks contain nested
    `struct __VFlags2 { … }` definitions and bitfields whose `}` would otherwise read as `@end`."""
    rows = []
    lines = strip_comments(text).splitlines()
    n = len(lines)
    cur = None
    ivar_depth = 0
    ivar_pending = False
    i = 0
    while i < n:
        s = lines[i].strip()
        if not s:
            i += 1
            continue
        if ivar_depth:                                  # inside the ivar block: nothing to see
            ivar_depth += s.count("{") - s.count("}")
            i += 1
            continue
        if ivar_pending:
            if s.startswith("{"):
                ivar_depth = s.count("{") - s.count("}")
                ivar_pending = False
                i += 1
                continue
            ivar_pending = False                        # a class with no ivar block at all
        m = re.match(r"@interface\s+([A-Za-z_]\w*)\s*(.*)$", s)
        if m:
            name, rest = m.group(1), m.group(2)
            cat = re.match(r"\(\s*([A-Za-z_]\w*)\s*\)", rest)
            if cat:
                cur = {"kind": "category", "name": cat.group(1), "cls": name}
                rows.append({"kind": "category", "name": cat.group(1), "owner": name,
                             "why": "category on %s" % name, "container": cat.group(1)})
            else:
                sup = re.match(r":\s*([A-Za-z_]\w*)", rest)
                cur = {"kind": "class", "name": name, "cls": name}
                rows.append({"kind": "class", "name": name, "owner": "-",
                             "super": sup.group(1) if sup else None, "why": "-", "container": name})
                ivar_pending = True
                if "{" in rest and ";" not in rest:
                    ivar_depth = rest.count("{") - rest.count("}")
                    ivar_pending = False
            i += 1
            continue
        m = re.match(r"@protocol\s+([A-Za-z_]\w*)\s*(.*)$", s)
        if m:
            cur = {"kind": "protocol", "name": m.group(1), "cls": m.group(1)}
            rows.append({"kind": "protocol", "name": m.group(1), "owner": "-", "why": "-",
                         "container": m.group(1)})
            i += 1
            continue
        if s.startswith("@end"):
            cur = None
            ivar_pending = False
            i += 1
            continue
        if s.startswith(("@class", "#import", "#include", "#pragma")):
            i += 1
            continue
        if cur is not None:
            if s[0] in "+-":
                decl, i = join_decl(lines, i)
                sel = selector_of(decl)
                if sel:
                    # A CATEGORY'S MEMBERS BELONG TO THE CLASS IT EXTENDS, not to the category: the
                    # owner is what the status test looks the selector up under, and a delegate
                    # category's `-tableView:…` is a method on whatever implements it.
                    owner = cur["name"] if cur["kind"] in ("class", "protocol") else cur["cls"]
                    why = ("category %s" % cur["name"]) if cur["kind"] == "category" else "-"
                    rows.append({"kind": "method", "name": sel, "owner": owner, "why": why,
                                 "container": cur["name"]})
                continue
            i += 1
            continue
        if s.startswith("#"):                           # a macro or a conditional, at top level
            decl, i = join_hash(lines, i)
            mm = re.match(r"#\s*define\s+([A-Za-z_]\w*)", decl)
            if mm:
                rows.append({"kind": "macro", "name": mm.group(1), "owner": "-", "why": "-",
                             "container": mm.group(1)})
            continue
        decl, i = join_decl(lines, i)
        d = decl.strip()
        if not d:
            i += 1
            continue
        if re.match(r"(typedef\s+)?enum\b", d):
            name = tail_name(d) if d.startswith("typedef") else None
            if name:
                rows.append({"kind": "enum", "name": name, "owner": "-", "why": "-",
                             "container": name})
            for c in enum_cases(d):
                rows.append({"kind": "case", "name": c, "owner": name or "-", "why": "-",
                             "container": name or c})
            continue
        if re.match(r"(typedef\s+)?struct\b", d):
            name = tail_name(d)
            if name:
                rows.append({"kind": "struct", "name": name, "owner": "-", "why": "-",
                             "container": name})
            continue
        if d.startswith("typedef"):
            name = tail_name(d)
            if name:
                rows.append({"kind": "typealias", "name": name, "owner": "-", "why": "-",
                             "container": name})
            continue
        if re.search(r"\b(APPKIT_EXTERN|extern)\b", d) or re.match(r"[\w\s\*]+\([^;]*\)\s*;$", d):
            if "(" in d:
                fm = re.search(r"([A-Za-z_]\w*)\s*\(", d)
                name = fm.group(1) if fm else tail_name(d)
                if name:
                    rows.append({"kind": "func", "name": name, "owner": "-", "why": "-",
                                 "container": name})
            else:
                name = tail_name(d)
                if name:
                    rows.append({"kind": "var", "name": name, "owner": "-", "why": "-",
                                 "container": name})
            continue
    return rows


# ---- OUR SIDE: what userland/AppKit declares, parsed with the SAME parser so the two sides are
# ---- compared in one vocabulary. This is what makes the owner-scoped method test possible.
_DECL_FORMS = (
    re.compile(r"#\s*define\s+([A-Za-z_]\w*)"),
    re.compile(r"@\s*interface\s+([A-Za-z_]\w*)"),
    re.compile(r"@\s*protocol\s+([A-Za-z_]\w*)"),
    re.compile(r"@\s*class\s+([A-Za-z_]\w*)"),
    re.compile(r"\b([A-Za-z_]\w*)\s*[=,}]"),
    re.compile(r"struct\s+([A-Za-z_]\w*)"),
    re.compile(r"\b([A-Za-z_]\w*)\s*;"),
    re.compile(r"\b([A-Za-z_]\w*)\s*:"),
)


def our_names():
    """{identifier} — every name OUR AppKit headers declare, in ONE pass (the §62.111/§62.112
    inversion: a per-row scan of the same text is the same answer at 100× the price)."""
    out = set()
    for path in sorted(glob.glob(OURS)):
        body = open(path, encoding="utf-8", errors="replace").read()
        for rx in _DECL_FORMS:
            for m in rx.finditer(strip_comments(body)):
                out.add(m.group(1))
        for rd in parse(body):
            out.add(rd["name"])
    return out


def our_surface():
    """What our AppKit actually declares, in the SAME row vocabulary as the corpus:
    (kind, name, owner) for containers and (owner, selector) for methods."""
    containers, methods = set(), set()
    for path in sorted(glob.glob(OURS)):
        for rd in parse(open(path, encoding="utf-8", errors="replace").read()):
            if rd["kind"] in ("class", "protocol", "category"):
                containers.add((rd["kind"], rd["name"], rd["owner"]))
            elif rd["kind"] == "method":
                methods.add((rd["owner"], rd["name"]))
    return containers, methods


def cluster_of(kind, name, owner):
    """A row's cluster. Classes and protocols are looked up; a CATEGORY is placed by the longest
    class-name prefix in its own name (so `NSTableViewDelegate` lands with `NSTableView`), then by
    the class it extends, then by the Foundation bucket. `unassigned` is a FAILURE, not a bucket —
    that is how the table is kept complete as the corpus is re-read."""
    c = CLUSTERS.get(name)
    if c:
        return c
    if kind == "category":
        best = None
        for cls in CLUSTERS:
            if name.startswith(cls) and (best is None or len(cls) > len(best)):
                best = cls
        if best:
            return CLUSTERS[best]
        if name in CATEGORY_CLUSTERS:
            return CATEGORY_CLUSTERS[name]
        if owner in CLUSTERS:
            return CLUSTERS[owner]
        if owner in FOUNDATION_CLASSES:
            return "foundation-additions"
        return "unassigned"
    return "unassigned"


def status_of(row, names, containers, methods):
    """Shipped or open, decided by FORM:
      * a container is shipped when we declare it as ANYTHING (the other sweeps' rule) — and a
        category, when we declare that category on that class;
      * a method is shipped when its OWNER declares that whole selector — the owner-scoped test the
        live ledger says it cannot do;
      * every other kind is a name-in-our-headers test.
    """
    kind, name, owner = row["kind"], row["name"], row["owner"]
    if kind == "class":
        return ("class", name, "-") in containers or name in names
    if kind == "protocol":
        return ("protocol", name, "-") in containers or name in names
    if kind == "category":
        return (("category", name, owner) in containers
                or any(k == "category" and nm == name for k, nm, _ in containers))
    if kind == "method":
        return (owner, name) in methods
    return name in names


def refresh():
    corpus = read_corpus(CORPUS)
    if not corpus:
        # the path AS GIVEN when it is outside the tree, because a relative path that has to climb out
        # (`../../../../nonexistent`) hides the directory the reader actually needs to look at
        shown = os.path.relpath(CORPUS, ROOT)
        if shown.startswith(".."):
            shown = CORPUS
        print("appkit-102-sweep: no corpus at %s" % shown)
        print("  Fetch the SDK first (the recipe is in docs/design/cocoa-parity-plan.md §7a); the")
        print("  corpus is Apple's and is NOT in this tree.")
        return 1

    # COMPLETENESS, because an incomplete corpus turns our spelling into Apple's silence.
    declared = " ".join(corpus.values())
    missing = [n for n in CORPUS_MUST_DECLARE
               if not re.search(r"@(?:interface|protocol)\s+%s\b" % re.escape(n), declared)]
    missing += [n for n in CORPUS_MUST_CATEGORIZE
                if not re.search(r"@interface\s+\w+\s*\(\s*%s\s*\)" % re.escape(n), declared)]
    # and the umbrella's imports must all resolve, or a header the SDK ships is not being read
    wanted = set(re.findall(r"#import\s+<AppKit/([A-Za-z_]\w*)\.h>", corpus.get("AppKit.h", "")))
    unresolved = sorted(w for w in wanted if (w + ".h") not in corpus)

    parsed = {fname: parse(corpus[fname]) for fname in corpus}
    rows = []
    for fname in sorted(parsed):
        for rd in parsed[fname]:
            rd["file"] = "AppKit/" + fname
            rd["fname"] = fname
            rows.append(rd)

    containers, methods = our_surface()
    names = our_names()
    fclusters = file_clusters(parsed)

    # DEDUPE FIRST, and the reason is measured rather than tidy: the same declaration appears more than
    # once in a header set — a macro defined once per platform branch (`APPKIT_EXTERN` is six `#define`s
    # in `AppKitDefines.h`), a method declared in the class's own block and again in a category block.
    # A work list counts WORK, so there is one row per (kind, name, owner). Measured on this corpus: 58
    # keys were duplicated before this.
    unique, keys = [], set()
    for r in rows:
        k = (r["kind"], r["name"], r["owner"])
        if k in keys:
            continue
        keys.add(k)
        unique.append(r)
    rows = unique

    # PLUMBING MACROS ARE DROPPED, AND COUNTED. A `#define` whose name begins with `_` is an include
    # guard or a Windows DLL goop, and `APPKIT_*` is the kit's own extern marker — a CLONE would define
    # its own, so neither is API and neither is work. The count travels into the header so the
    # subtraction is visible rather than inferred from a total that does not add up.
    plumbing = [r for r in rows if r["kind"] == "macro"
                and (r["name"].startswith("_") or r["name"].startswith("APPKIT_"))]
    rows = [r for r in rows if not (r["kind"] == "macro"
                                    and (r["name"].startswith("_") or r["name"].startswith("APPKIT_")))]

    # a row's cluster: its container's for a member, its own for a container, and the FILE's for a
    # top-level constant, which belongs to no class at all. A NAME THAT BEGINS WITH `_` IS PRIVATE BY
    # THIS SDK'S OWN CONVENTION (its headers say `/*All instance variables are private*/`, and every
    # `_`-prefixed struct here is an ivar bitfield): such a row is real — it IS declared in the public
    # header — but it is not API a caller writes, so `why` says what kind of work it is.
    seen = {}
    for rd in rows:
        if rd["kind"] in ("class", "protocol", "category"):
            seen[rd["name"]] = cluster_of(rd["kind"], rd["name"], rd["owner"])
    for r in rows:
        if r["kind"] == "method":
            r["cluster"] = seen.get(r["container"]) or "unassigned"
        elif r["kind"] in ("class", "protocol", "category"):
            r["cluster"] = cluster_of(r["kind"], r["name"], r["owner"])
        else:
            r["cluster"] = fclusters.get(r["fname"], "unassigned")
        if r["name"].startswith("_") and r["why"] == "-":
            r["why"] = "private (underscore-prefixed in the header)"
        r["status"] = STATUS_SHIPPED if status_of(r, names, containers, methods) else STATUS_OPEN

    rows.sort(key=lambda r: (r["kind"], r["name"], r["owner"]))
    counts, fams = {}, {}
    for r in rows:
        counts[(r["kind"], r["status"])] = counts.get((r["kind"], r["status"]), 0) + 1
        fams[(r["cluster"], r["status"])] = fams.get((r["cluster"], r["status"]), 0) + 1
    unassigned = [r for r in rows if r["cluster"] == "unassigned"]

    out = ["\t".join((r["kind"], r["status"], r["name"], r["owner"], r["cluster"], r["why"],
                      r["file"])) for r in rows]
    total = len(rows)
    open_ = sum(c for (k, s), c in counts.items() if s == STATUS_OPEN)
    header = [
        "# The Application Kit AT 10.2, against this tree — the era-pinned work list.",
        "# GENERATED by %s --refresh — do not hand-edit the" % TOOL,
        "# status column; --check fails when it drifts from the headers.",
        "#",
        "# ground: %s, AppKit.framework/Headers — %d header(s), read as SOURCE, not as an index"
        % (SDK_NAME, len(corpus)),
        "# source: " + SDK_SOURCE,
        "# the corpus is NOT in this tree and must not be: only this artefact and its generator ship.",
        "#",
        "# kind\tstatus\tname\towner\tfamily\twhy\tsrc",
        "#",
        "# %d API rows, %d open. THE GROUND IS AN SDK, NOT A DOCUMENTATION INDEX, and that is the"
        % (total, open_),
        "# whole point of this file: tools/appkit-sweep.py ledgers the AppKit Apple documents TODAY",
        "# (12,475 rows, 10.15-era), and this one ledgers the AppKit 10.2's own headers declare.",
        "#",
        "# THE ERA, IN THE DATA (verified over this corpus, not assumed):",
        "#   property rows 0 — Objective-C 2.0's @property did not exist until 10.5. Accessors are",
        "#                     METHODS here, so every `-setX:`/`-x` pair is TWO rows.",
        "#   @optional/@required absent — a 10.2 protocol is all-required; DELEGATES ARE CATEGORIES",
        "#                     (`@interface NSObject (NSXxxDelegate)`), which is why the `category`",
        "#                     kind exists here and in no other ledger in this tree.",
        "#   struck rows %d — BY CONSTRUCTION. The era ground is the pin: nothing later than it can"
        % sum(c for (k, s), c in counts.items() if s == STATUS_STRUCK),
        "#                     deprecate a row. (`struck` stays in the vocabulary so a reader of the",
        "#                     other ledgers is not left wondering where it went.)",
        "#   %d plumbing macro(s) DROPPED before the rows — a `#define` named `_…` or `APPKIT_…` is an"
        % len(plumbing),
        "#                     include guard, a DLL goop or the kit's own extern marker, none of which a",
        "#                     clone writes: %s" % (", ".join(sorted({r["name"] for r in plumbing})) or "none"),
        "#   %d row(s) are PRIVATE BY THE SDK'S OWN CONVENTION (a `_`-prefixed struct/ivar block) and"
        % sum(1 for r in rows if r["name"].startswith("_")),
        "#                     say so in the `why` column: DECLARED in the header, not API a caller writes.",
        "#",
        "# BY KIND:",
    ]
    for (kind, st), c in sorted(counts.items()):
        header.append("#   %-10s %-8s %5d" % (kind, st, c))
    header.append("#")
    header.append("# BY CLUSTER (the family column; OURS and stated as a rule — a 10.2 SDK carries no")
    header.append("# taxonomy, unlike the live ledger's family column):")
    for cl in CLUSTER_ORDER:
        o = fams.get((cl, STATUS_OPEN), 0)
        s = fams.get((cl, STATUS_SHIPPED), 0)
        if o or s:
            header.append("#   %-20s open %5d   shipped %5d" % (cl, o, s))
    if missing or unresolved or unassigned:
        header.append("#")
    if missing:
        header.append("# ⚠⚠ CORPUS INCOMPLETE: %d name(s) that MUST exist are absent — %s"
                      % (len(missing), ", ".join(missing)))
    if unresolved:
        header.append("# ⚠⚠ THE UMBRELLA IMPORTS %d HEADER(S) THE CORPUS DOES NOT CARRY — %s"
                      % (len(unresolved), ", ".join(unresolved)))
    if unassigned:
        header.append("# ⚠⚠ %d CONTAINER(S) HAVE NO CLUSTER — add them to CLUSTERS; --check fails on this:"
                      % len(unassigned))
        for r in unassigned:
            header.append("#     unassigned: %s %s" % (r["kind"], r["name"]))

    open(SURFACE, "w", encoding="utf-8").write("\n".join(header + out) + "\n")
    for (kind, st), c in sorted(counts.items()):
        print("  %-10s %-8s %5d" % (kind, st, c))
    print("appkit-102-sweep: %d rows (%d open) from %d header(s) -> %s"
          % (total, open_, len(corpus), os.path.relpath(SURFACE, ROOT)))
    if missing:
        print("  ⚠⚠ corpus incomplete: %s" % ", ".join(missing))
    if unresolved:
        print("  ⚠⚠ umbrella imports missing from the corpus: %s" % ", ".join(unresolved))
    if unassigned:
        print("  ⚠⚠ %d unassigned container(s): %s"
              % (len(unassigned), ", ".join(r["name"] for r in unassigned)))
    return 0


def read_surface():
    rows = []
    for line in open(SURFACE, encoding="utf-8"):
        if line.startswith("#") or not line.strip():
            continue
        kind, status, name, owner, family, why, src = line.rstrip("\n").split("\t")
        rows.append((kind, status, name, owner, family, why, src))
    return rows


def check():
    """INCONSISTENCIES are facts this tree has and the artefact has out of date — they FAIL, and no
    one has to decide anything: a `shipped` row our headers no longer declare, an `open` row they now
    do, a row with no cluster, and a plan family table that has drifted. There is no POLICY class
    here: the era ground cannot strike a row, so there is nothing for `--strict` to be about (the
    other ledgers' policy class is their deprecation ground, and this one has none by construction)."""
    try:
        rows = read_surface()
    except FileNotFoundError:
        print("appkit-102-sweep: no %s yet — run --refresh" % os.path.relpath(SURFACE, ROOT))
        return 1
    containers, methods = our_surface()
    names = our_names()
    bad, counts, fams = [], {}, {}
    for kind, status, name, owner, family, why, src in rows:
        counts[(kind, status)] = counts.get((kind, status), 0) + 1
        fams[(family, status)] = fams.get((family, status), 0) + 1
        found = status_of({"kind": kind, "name": name, "owner": owner}, names, containers, methods)
        if status == STATUS_SHIPPED and not found:
            bad.append("STALE SHIPPED CLAIM    %-9s %s [%s] — the file says we ship it and our "
                       "headers do not declare it" % (kind, name, owner))
        elif status == STATUS_OPEN and found:
            bad.append("PRESENT BUT LISTED OPEN %-8s %s [%s] — our headers now declare it; flip "
                       "the row" % (kind, name, owner))
    print("appkit-102-sweep: %d symbols in the work list" % len(rows))
    for kind in sorted({k for k, _ in counts}):
        print("  %-10s shipped %4d   open %4d" % (
            kind, counts.get((kind, STATUS_SHIPPED), 0), counts.get((kind, STATUS_OPEN), 0)))
    unassigned = fams.get(("unassigned", STATUS_OPEN), 0) + fams.get(("unassigned", STATUS_SHIPPED), 0)
    if unassigned:
        print("\n%d ROW(S) HAVE NO CLUSTER — the family table has fallen behind the corpus:\n"
              % unassigned)
        for kind, status, name, owner, family, why, src in rows:
            if family == "unassigned":
                print("  %-9s %-30s %s" % (kind, name, src))
    if bad:
        print("\n%d INCONSISTENC(IES):\n" % len(bad))
        for line in bad:
            print("  " + line)
    # THE PLAN'S FAMILY BLOCK IS GENERATED TOO, so it cannot drift the way a hand-written table did.
    doc = open(PLAN, encoding="utf-8").read() if os.path.exists(PLAN) else ""
    block = families_block()
    if block.strip() not in doc:
        print("\nTHE PLAN'S FAMILY TABLE IS MISSING OR STALE — run: %s --families --write" % TOOL)
        bad.append("plan family table")
    if bad or unassigned:
        return 1
    print("appkit-102-sweep: consistent — every shipped name is declared, every open name is absent, "
          "every row has a cluster, and the plan's family table matches")
    return 0


def families_block():
    rows = read_surface()
    agg = {}
    for kind, status, name, owner, family, why, src in rows:
        a = agg.setdefault(family, {"class": 0, "protocol": 0, "category": 0, "method": 0,
                                    "open": 0, "shipped": 0})
        if kind in a:
            a[kind] += 1
        a["open" if status == STATUS_OPEN else "shipped"] += 1
    out = ["<!-- BEGIN appkit-102-families (generated by %s --families; do not hand-edit) -->" % TOOL,
           "| # | cluster | classes | protocols | categories | methods | open | shipped |",
           "|---|---|---:|---:|---:|---:|---:|---:|"]
    for i, cl in enumerate(CLUSTER_ORDER, 1):
        a = agg.get(cl)
        if not a:
            continue
        out.append("| %d | `%s` | %d | %d | %d | %d | %d | %d |"
                   % (i, cl, a["class"], a["protocol"], a["category"], a["method"], a["open"],
                      a["shipped"]))
    tot = {"class": 0, "protocol": 0, "category": 0, "method": 0, "open": 0, "shipped": 0}
    for a in agg.values():
        for k in tot:
            tot[k] += a[k]
    out.append("| | **total** | **%d** | **%d** | **%d** | **%d** | **%d** | **%d** |"
               % (tot["class"], tot["protocol"], tot["category"], tot["method"], tot["open"],
                  tot["shipped"]))
    out.append("<!-- END appkit-102-families -->")
    return "\n".join(out)


def families(write=False):
    block = families_block()
    if not write:
        print(block)
        return 0
    doc = open(PLAN, encoding="utf-8").read()
    begin = "<!-- BEGIN appkit-102-families"
    end = "<!-- END appkit-102-families -->"
    b = doc.find(begin)
    if b < 0:
        print("appkit-102-sweep: no family block in %s — add the markers first"
              % os.path.relpath(PLAN, ROOT))
        return 1
    e = doc.find(end, b)
    if e < 0:
        print("appkit-102-sweep: the family block's END marker is missing")
        return 1
    open(PLAN, "w", encoding="utf-8").write(doc[:b] + block + doc[e + len(end):])
    print("appkit-102-sweep: rewrote the family block in %s" % os.path.relpath(PLAN, ROOT))
    return 0


def work_list(want=None, cluster=None):
    rows = [r for r in read_surface()
            if r[1] == STATUS_OPEN and (want is None or r[0] == want)
            and (cluster is None or r[4] == cluster)]
    by_owner = {}
    for kind, status, name, owner, family, why, src in rows:
        by_owner.setdefault((family, owner), []).append((kind, name, why, src))
    for key in sorted(by_owner, key=lambda k: (CLUSTER_ORDER.index(k[0]) if k[0] in CLUSTER_ORDER
                                               else 99, k[1])):
        family, owner = key
        members = by_owner[key]
        print("\n## %s  [%s]  (%d)" % (owner, family, len(members)))
        for kind, name, why, src in sorted(members, key=lambda m: (m[0], m[1])):
            print("   %-9s %-42s %s" % (kind, name, src))
    print("\n%d open symbols" % len(rows))
    return 0


def order():
    """THE BUILD ORDER: classes by their distance from the root of the superclass graph the SDK
    declares. A DEPTH-1 CLASS'S SUPERCLASS IS NOT AN AppKit CLASS AT ALL (it is `NSObject`, or a
    Foundation class the corpus does not carry), so depth 1 is the base of a chain; the depth is how
    much of the AppKit has to exist before the class can, and the ledger's own status says how much of
    it already does."""
    corpus = read_corpus(CORPUS)
    if not corpus:
        print("appkit-102-sweep: --order needs the corpus at %s" % os.path.relpath(CORPUS, ROOT))
        return 1
    supers, status = {}, {}
    for kind, st, name, owner, family, why, src in read_surface():
        if kind == "class":
            status[name] = st
    for fname in corpus:
        for rd in parse(corpus[fname]):
            if rd["kind"] == "class" and rd.get("super"):
                supers[rd["name"]] = rd["super"]
    depth = {}

    def d(name, seen=()):
        if name in depth:
            return depth[name]
        if name in seen or name not in supers:
            return 0
        depth[name] = d(supers[name], seen + (name,)) + 1
        return depth[name]

    for name in status:
        d(name)
    for lvl in range(1, max(depth.values() or [1]) + 1):
        names = sorted(n for n in status if depth.get(n) == lvl)
        if not names:
            continue
        print("\n# depth %d — %d class(es)" % (lvl, len(names)))
        for n in names:
            print("  %-30s %-8s : %s" % (n, status[n], supers.get(n, "-")))
    return 0


def main(argv):
    mode = argv[1] if len(argv) > 1 else "--check"
    if mode == "--refresh":
        rc = refresh()
        return rc if rc else check()
    if mode == "--check":
        return check()
    if mode == "--work-list":
        rest = [a for a in argv[2:] if not a.startswith("-")]
        want = rest[0] if rest and rest[0] in KINDS else None
        cluster = rest[0] if rest and rest[0] in CLUSTER_ORDER else (rest[1] if len(rest) > 1 else None)
        return work_list(want, cluster)
    if mode == "--families":
        return families("--write" in argv)
    if mode == "--order":
        return order()
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
