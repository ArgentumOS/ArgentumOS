#!/usr/bin/env python3
# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""THE APPLICATION KIT AS IT WAS AT 10.6 — a work list pinned to an SDK, not to today.

THE THIRD ERA INSTRUMENT, cloned from `tools/appkit-105-sweep.py` (which was cloned from the 10.2 one).
They are separate files because the ground is: each is pinned to one SDK's headers, they keep the SAME
ROW SHAPE (kind, status, name, owner, family, why, src), the same three statuses, the same
`--refresh`/`--check` split and the same inconsistency classes, and each carries the branches its own era
forces. `coregraphics-sweep.py` says why sharing is not the answer: "two sweeps that disagree about what
a ledger row IS would be worse than one".

10.6 IS WHERE THE SHAPE OF THE FRAMEWORK CHANGES, AND THE CENSUS SAID SO BEFORE ANY CODE WAS WRITTEN:

  * `@property` ARRIVES — 27 declarations, in exactly four headers, all of them NEW at 10.6
    (`NSTextInputContext.h`, `NSTouch.h`, `NSRunningApplication.h`, `NSOpenGLLayer.h`). This is the
    difference from 10.5, where the language had properties and the AppKit did not use them: here a
    HANDFUL of new classes are declared in the new style and every older header stays accessor-based.
    So this ledger has a `property` KIND — the first of the three, and `parse()` reads the syntax
    properly rather than counting it — and the artefact's header records that 0 properties were skipped.
    (10.5's instrument GUARDED this shape: it counted `@property` and reported what it could not read.
    10.6 is the era where the guard would have fired, which is why the guard was worth writing.)

  * THE DELEGATE PATTERN BECOMES PROTOCOLS — THIS IS THE ERA'S HEADLINE. 10.2 and 10.5 express every
    delegate, data source and notification surface as `@interface NSObject (NSXxxDelegate)`; 10.6
    declares them as formal `@protocol`s with `@optional` members. Measured: **57 protocols against
    18** (39 new: NSApplicationDelegate, NSTableViewDelegate, NSTableViewDataSource, NSWindowDelegate,
    NSTextViewDelegate, …) and **25 `NSObject` categories against 58**. Nothing was deleted: the same
    selectors changed the shape of the thing that declares them, which is exactly the point the 10.2 and
    10.5 sections make in the other direction ("a clone that reaches for formal protocols would be
    building 10.15's AppKit") — AND 10.6 IS WHERE THAT STOPPED BEING TRUE.

  * `@optional` GOES FROM RARE TO THE NORM: 43 occurrences (10.5: 4), and `@required` appears 8 times,
    because a protocol in this era marks which half a conforming class must implement.

  * AVAILABILITY DEEPENS: 1,027 `AVAILABLE_MAC_OS_X_VERSION_…_AND_LATER` and 215
    `DEPRECATED_IN_…_AND_LATER` against 10.5's 131 and 18, with 319 of the AVAILABLE annotations
    naming 10.6 itself. The annotations stay the `why` column, and a deprecated row is OWED, not struck.

  * `NSInteger`/`CGFloat` REACH 1,268 uses (10.5: 1,184) — the signature modernization continues rather
    than completing.

  * STILL NO `NS_ENUM`/`NS_OPTIONS` (0 occurrences): the typed-enum macros arrive later, so this era's
    enumerations are read the 10.2 way.

  * BLOCKS REACH THE HEADERS BUT NOT THE PROPERTIES: 9 `(^)` occurrences, none of them a property type.

  * `#if MAC_OS_X_VERSION_MAX_ALLOWED >= …` GUARDS ARE IGNORED, DELIBERATELY — 10.6 uses them the way
    10.5 did, including the repeated `@interface … {` line that cost the 10.5 instrument a whole file's
    members (§8h of the plan). This instrument reads them correctly from the start.

`--audit` IS THIS ERA'S OTHER ADDITION, AND IT IS THE RULE §8h SAYS WAS MISSING: for every file, the raw
`@interface`/`@protocol` lines and raw method/property lines must equal what the parser produced. It is a
MODE rather than a scratch script because it caught two real defects in the earlier instruments, one of
which had already shipped a short ledger. Two accounting rules are stated in it, each of which cost a
false alarm once: a declaration with no return type is a member line, and a DUPLICATED container line is
one container.

THE CORPUS IS NOT IN THIS TREE AND MUST NOT BE (Apple's SDK is not redistributable; the same rule
`tools/foundation-sdk-subset.py` states). `.tmp/` is gitignored for exactly this reason, the fetch is
recorded in the plan document, and the artefact is regenerable from it: the generator ships, the data
does not.

USAGE

  tools/appkit-106-sweep.py --refresh            re-read the 10.6 corpus and rewrite the work list
  tools/appkit-106-sweep.py --check              verify the artefact against our headers (offline)
  tools/appkit-106-sweep.py --audit              parse-accounting over the whole corpus (needs it)
  tools/appkit-106-sweep.py --work-list [KIND]   print the open rows — the work list itself
  tools/appkit-106-sweep.py --families [--write] print (or rewrite into the plan) the family table
  tools/appkit-106-sweep.py --order              classes by superclass depth: the build order
  tools/appkit-106-sweep.py --delta              what 10.6 has that 10.5 does not, and the reverse
"""

import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SURFACE = os.path.join(ROOT, "docs/reference/appkit-106-worklist.txt")
PLAN = os.path.join(ROOT, "docs/design/cocoa-parity-plan.md")
OURS = os.path.join(ROOT, "userland/AppKit/*.h")
TOOL = "tools/appkit-106-sweep.py"

# THE PREVIOUS ERA'S ARTEFACT IS THIS FILE'S SECOND GROUND, and only `--delta` reads it: the difference
# between two eras is a row-set difference, so it is computed from the two LEDGERS rather than from the
# two corpora (one of which a reader of this file may not have). For this instrument that previous era
# is 10.5, and the name is a constant so the delta's prose cannot drift from its ground.
SURFACE_PREV = os.path.join(ROOT, "docs/reference/appkit-105-worklist.txt")
PREV_ERA = "10.5"

# The corpus: an SDK mirror checked out by hand, never committed. APPKIT106_SDK overrides the path so
# the instrument is not welded to one checkout (the .tmp/ tree is mine; someone else's may differ).
CORPUS = os.environ.get(
    "APPKIT106_SDK",
    os.path.join(ROOT, ".tmp/mac106/MacOSX10.6.sdk/System/Library/Frameworks/AppKit.framework/Headers"))
SDK_NAME = "MacOSX10.6.sdk"
SDK_SOURCE = "https://github.com/phracker/MacOSX-SDKs (published mirror; Apple's SDK is not redistributable)"

STATUS_SHIPPED = "shipped"
STATUS_OPEN = "open"
STATUS_STRUCK = "struck"   # never assigned here — see the docstring

# THE KINDS THAT ARE API. `property` IS IN THIS LIST FOR THE FIRST TIME IN THE FAMILY: 10.6 declares 27
# of them, in the four headers that are new at this era. `category` is a kind no other ledger carries.
KINDS = ("category", "class", "protocol", "method", "property", "enum", "case", "typealias", "struct",
         "func", "var", "macro")

# THE PLAN'S FAMILY BLOCK IS IDENTIFIED BY THIS MARKER, AND IT MUST BE THE CLONE'S OWN. A clone that
# forgets to change it is not a cosmetic slip: `--families --write` rewrites the FIRST marker in the
# plan, so a 10.6 instrument carrying 10.5's name OVERWRITES THE 10.5 TABLE IN §8c — measured, exactly
# once, while this file was being written. `--check` still passed, because the tool and the block it had
# just rewritten agreed with each other; the OTHER instrument's check is what caught it. Hence one named
# constant instead of two literals in two functions.
PLAN_MARKER = "appkit-106-families"

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
    # ---- 10.5's own classes, each of which has NO 10.2 COUNTERPART (the --delta mode prints the full
    # ---- set). They are clustered by the same rule as everything above; the ones that open a NEW
    # ---- cluster are bindings (the controller layer ObjC 2.0's KVC/KVO made possible), animation, and
    # ---- speech.
    # controllers and bindings: the whole layer is 10.5
    "NSController": "bindings", "NSObjectController": "bindings", "NSArrayController": "bindings",
    "NSDictionaryController": "bindings", "NSTreeController": "bindings",
    "NSUserDefaultsController": "bindings", "NSTreeNode": "bindings",
    "NSKeyValueBinding": "bindings", "NSEditor": "bindings", "NSEditorRegistration": "bindings",
    "NSKeyValueBindingCreation": "bindings", "NSDictionaryControllerKeyValuePair": "bindings",
    # animation
    "NSAnimation": "animation", "NSAnimationContext": "animation",
    "NSAnimatablePropertyContainer": "animation",
    # speech
    "NSSpeechSynthesizer": "speech", "NSSpeechRecognizer": "speech",
    # new leaf controls, views and containers
    "NSSegmentedControl": "controls", "NSSegmentedCell": "controls",
    "NSDatePicker": "controls", "NSDatePickerCell": "controls",
    "NSLevelIndicator": "controls", "NSLevelIndicatorCell": "controls",
    "NSTokenField": "controls", "NSTokenFieldCell": "controls",
    "NSSearchField": "controls", "NSSearchFieldCell": "controls",
    "NSPathControl": "controls", "NSPathCell": "controls", "NSPathComponentCell": "controls",
    "NSPathCellDelegate": "controls", "NSPathControlDelegate": "controls",
    "NSCollectionView": "containers", "NSToolbarItemGroup": "containers",
    "NSViewController": "view-core", "NSTrackingArea": "view-core",
    # new row-based editors
    "NSRuleEditor": "tables", "NSPredicateEditor": "tables",
    "NSPredicateEditorRowTemplate": "tables",
    # new drawing and colour
    "NSGradient": "graphics", "NSShadow": "graphics", "NSColorSpace": "graphics",
    # new text
    "NSATSTypesetter": "text", "NSGlyphGenerator": "text", "NSFontDescriptor": "text",
    "NSTextList": "text", "NSTextTable": "text", "NSTextInputClient": "text",
    "NSGlyphStorage": "text", "NSPrintPanelAccessorizing": "panels",
    # new panels, nib and app-level classes
    "NSAlert": "panels", "NSNib": "nib", "NSDockTile": "app", "NSPersistentDocument": "app",
    "NSCIImageRep": "images",
    # the rest of 10.5's new classes, found by `--refresh`'s unassigned report rather than guessed at
    "NSCollectionViewItem": "containers", "NSViewAnimation": "animation",
    "NSOpenGLPixelBuffer": "opengl",
    "NSTextBlock": "text", "NSTextTableBlock": "text",
    # ---- 10.6'S OWN CLASSES, the same way: its six new headers carry these, and four of the six are
    # ---- also where this era's properties live.
    "NSOpenGLLayer": "opengl", "NSPasteboardItem": "pasteboard-drag",
    "NSRunningApplication": "app", "NSTextInputContext": "text",
    "NSTouch": "responder",       # touch is INPUT, and NSTouchPhase lives in its file
}

# CLUSTER_ORDER is the BUILD ORDER of the clusters, and it is a claim: the bases have to exist before
# the leaves that subclass them, and the infrastructure (responder, view tree, drawing, text) before
# anything that is drawn with it.
CLUSTER_ORDER = ("responder", "view-core", "graphics", "text", "window", "controls", "containers",
                 "bindings", "menus", "panels", "images", "tables", "pasteboard-drag", "nib", "app",
                 "media", "animation", "opengl", "speech", "spelling", "foundation-additions")

# CATEGORIES ON FOUNDATION'S CLASSES: 10.2's AppKit extends Foundation's classes rather than its own
# (NSStringDrawing, NSAttributedString's AppKit additions, NSBundle's nib/image/sound loading, …). They
# are a cluster of their own because their work is "write a category the substrate's class can carry".
FOUNDATION_CLASSES = {"NSObject", "NSString", "NSMutableString", "NSBundle", "NSAttributedString",
                      "NSMutableAttributedString", "NSCoder", "NSURL", "NSAppleScript", "NSData",
                      "NSArray", "NSDictionary"}

# THE ODD CATEGORIES THE PREFIX RULE CANNOT PLACE, named explicitly rather than left to land in the
# Foundation bucket: NSToolTipOwner is a VIEW feature with an NSObject base, and NSAccessibility is the
# accessibility surface (a category at BOTH eras), which is view geometry by another name.
#
# NSAppKitAdditions IS 10.5'S OWN and is the one category on a NON-AppKit, NON-Foundation class:
# `@interface CIColor (NSAppKitAdditions)` and `@interface CIImage (NSAppKitAdditions)` in NSColor.h and
# NSCIImageRep.h — the AppKit side of Core Image. It lands in graphics because that is what it converts
# into and out of, and it is named here rather than left to the Foundation bucket because Core Image is
# neither ours nor Foundation's.
CATEGORY_CLUSTERS = {
    "NSToolTipOwner": "view-core",
    "NSAccessibility": "view-core",
    "NSStandardKeyBindingMethods": "responder",
    "NSUndoSupport": "responder",
    "NSDraggingDestination": "pasteboard-drag",
    "NSDraggingSource": "pasteboard-drag",
    "NSPasteboardOwner": "pasteboard-drag",
    "NSAppKitAdditions": "graphics",
    # 10.6's two odd PROTOCOLS, which the prefix rule cannot place for the same reason it could not place
    # the categories above: `NSOpenSavePanelDelegate` does not begin with the name of any class
    # (`NSOpenPanel` and `NSSavePanel` both fall short of it), and `NSUserInterfaceItemSearching` names no
    # class at all. The first is the open/save panel's delegate, the second is what NSHelpManager
    # searches — both are placed by what they SERVE, which is the rule the whole table follows.
    "NSOpenSavePanelDelegate": "panels",
    "NSUserInterfaceItemSearching": "app",
}

# THE COMPLETENESS INSTRUMENT. An incomplete corpus turns "we misspelled it" into "Apple does not have
# it" — the one way this file can lie — so every name below is API that MUST be in any 10.5 AppKit. A
# missing one is a fact about the CORPUS and is reported as such, exactly as foundation-sdk-subset.py
# does it. (Checked 2026-10-03: all present.)
#
# `NSAccessibility` IS ABSENT ON PURPOSE, and its absence is an era finding rather than an oversight: at
# BOTH eras accessibility is neither a class nor a protocol — it is
# `@interface NSObject (NSAccessibility)`, an informal category of methods a view implements. It is
# asserted in its category form below.
#
# THE 10.5 BLOCK IS THE POINT OF THIS LIST: every name in it is a class (or protocol) that 10.2's AppKit
# does not have at all, so a corpus missing one of them is not a 10.5 corpus and the rows below would be
# describing the wrong framework.
CORPUS_MUST_DECLARE = (
    # present at 10.2 as well
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
    "NSWorkspace",
    # NEW AT 10.5 — each of these is a whole subsystem the clone does not have yet
    "NSAlert", "NSAnimation", "NSAnimationContext", "NSArrayController", "NSCollectionView",
    "NSColorSpace", "NSController", "NSDatePicker", "NSDictionaryController", "NSDockTile",
    "NSFontDescriptor", "NSGradient", "NSLevelIndicator", "NSNib", "NSObjectController",
    "NSPathControl", "NSPersistentDocument", "NSPredicateEditor", "NSRuleEditor", "NSSearchField",
    "NSSegmentedControl", "NSShadow", "NSSpeechSynthesizer", "NSTextInputClient", "NSTextList",
    "NSTextTable", "NSTokenField", "NSToolbarItemGroup", "NSTrackingArea", "NSTreeController",
    "NSViewController",
    # NEW AT 10.6 — the six classes whose headers this era adds, four of which are also the four
    # headers that declare properties
    "NSOpenGLLayer", "NSPasteboardItem", "NSRunningApplication", "NSTextInputContext", "NSTouch",
    "NSUserInterfaceItemSearching")

# ...and the categories that MUST be there, checked the same way. NSAccessibility is the one that
# matters (it is the accessibility surface at every era in this range) and the two dragging ones are the
# whole drag-and-drop contract.
#
# THE LAST TWO NAMES MOVED LISTS AT 10.6, WHICH IS THE POINT: `NSApplicationDelegate` and
# `NSTableViewDelegate` are CATEGORIES at 10.2 and 10.5 — asserted there — and PROTOCOLS here. Keeping
# them in this list would make the completeness check fail on a corpus that is perfectly correct, and
# moving them is how the era's headline change is expressed in the instrument rather than in prose.
CORPUS_MUST_CATEGORIZE = ("NSAccessibility", "NSDraggingDestination", "NSDraggingSource")

# ...AND THE PROTOCOLS THAT MUST BE THERE, for the same reason: 10.6 declares the delegate surface
# formally, so a 10.6 corpus without these is not a 10.6 corpus.
CORPUS_MUST_PROTOCOLIZE = ("NSApplicationDelegate", "NSTableViewDelegate", "NSTableViewDataSource",
                           "NSWindowDelegate", "NSTextViewDelegate", "NSOutlineViewDelegate")

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
    "AppKitErrors.h": "app",            # ...and 10.5's service/text error codes, in a header of its own
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
    # ...AND THE NO-ARGUMENT FALLBACK NEEDS THE STRIP: A MEASURED DEFECT FIX, NOT TIDINESS. `d` here is
    # whatever followed the return type. 10.2 writes `- (NSArray *)subviews;` — no space, so the name is
    # at offset 0 and the anchored match below found it — and 10.5 writes `- (NSButtonCell*) searchButtonCell;`
    # WITH a space, so the same match found NOTHING and the row was dropped in silence: 18 of 177 headers
    # short, NSLayoutManager.h losing all 137 of its methods to the ivar trap above. A no-argument method
    # whose return type is parenthesised is the ONLY shape that reaches this line, which is why the
    # chained-selector corpus never showed it and why §8's audit exists.
    d = d.strip()
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


# ---- 10.5'S AVAILABILITY ANNOTATIONS, WHICH 10.2 DID NOT HAVE. Two macros, both attached to the END of
# ---- the declaration they qualify (or on the line AFTER it, which is why the parser joins a declaration
# ---- before reading it):
# ----   AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER
# ----   AVAILABLE_MAC_OS_X_VERSION_10_0_AND_LATER_BUT_DEPRECATED_IN_MAC_OS_X_VERSION_10_4
# ----   DEPRECATED_IN_MAC_OS_X_VERSION_10_4_AND_LATER
# ---- They become `why`, and THEY MUST BE STRIPPED BEFORE A NAME IS READ: `APPKIT_EXTERN NSString *NSFoo
# ---- AVAILABLE_…_AND_LATER;` ends in `_AND_LATER`, so a tail-identifier rule would name the constant
# ---- AND_LATER. (Measured: 131 AVAILABLE and 18 DEPRECATED occurrences in this corpus.)
AVAIL_RX = re.compile(
    r"\bAVAILABLE_MAC_OS_X_VERSION_(\d+)_(\d+)_AND_LATER"
    r"(?:_BUT_DEPRECATED(?:_IN_MAC_OS_X_VERSION_(\d+)_(\d+))?)?")
DEPR_RX = re.compile(r"\bDEPRECATED_IN_MAC_OS_X_VERSION_(\d+)_(\d+)_AND_LATER\b")


def availability(decl):
    """(introduced, deprecated) as "10.5"-shaped strings, or None for either.

    `AVAILABLE_…_AND_LATER_BUT_DEPRECATED` (the no-version form) means "new and deprecated in the same
    release", so its deprecation version IS its introduction version — the one case where the fallback
    has to be stated rather than left to be read as an absent datum."""
    intro = dep = None
    m = AVAIL_RX.search(decl)
    if m:
        intro = "%s.%s" % (m.group(1), m.group(2))
        if "_BUT_DEPRECATED" in m.group(0):
            dep = ("%s.%s" % (m.group(3), m.group(4))) if m.group(3) else intro
    m = DEPR_RX.search(decl)
    if m:
        dep = "%s.%s" % (m.group(1), m.group(2))
    return intro, dep


def strip_availability(decl):
    """The same declaration with the availability macros removed — see AVAIL_RX for why it is not
    optional."""
    return AVAIL_RX.sub(" ", DEPR_RX.sub(" ", decl))


def why_of(base, intro, dep, optional=False, getter=None):
    """The `why` column: WHAT KIND OF WORK a row is.

    An era note is worth a reader's attention only when it is unusual, so `introduced 10.0` (the baseline
    everything already has) is dropped, and a deprecation REPLACES its introduction — the deprecated half
    is the half that changes what a caller must do.

    `getter` IS THE ONE PROPERTY ATTRIBUTE THAT CHANGES THE ROW'S 10.6 CONTRACT: three of the four
    headers that declare properties use `getter=isTerminated`, `getter=isHidden`, `getter=isActive`,
    so the accessor a conforming class must implement is NOT the property's name. It travels in `why`
    (the row keeps the property's name, as the live ledger's `property` rows do)."""
    bits = []
    if base and base != "-":
        bits.append(base)
    if optional:
        bits.append("optional")
    if dep:
        bits.append("deprecated " + dep)
    elif intro and intro != "10.0":
        bits.append("introduced " + intro)
    if getter:
        bits.append("getter " + getter)
    return ", ".join(bits) or "-"


def property_of(decl):
    """(name, getter) for a `@property` declaration.

    THE ATTRIBUTE GROUP IS DROPPED BY BALANCE, NOT BY REGEX: `@property (readonly, getter=isTerminated)
    BOOL terminated;` has one balanced group, and a regex that removed "anything in parens" would ALSO
    eat a parenthesised TYPE (`@property (copy) void (^handler)(void);`, which this corpus does not have
    but 10.8's does). The name is then the last identifier before the `;` — the same rule every other
    declaration kind in this file uses."""
    d = decl.strip()
    if d.startswith("@property"):
        d = d[len("@property"):].strip()
    getter = None
    if d.startswith("("):
        depth = 0
        for k, ch in enumerate(d):
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
                if depth == 0:
                    gm = re.search(r"getter\s*=\s*([A-Za-z_]\w*)", d[1:k])
                    if gm:
                        getter = gm.group(1)
                    d = d[k + 1:]
                    break
    return tail_name(d), getter


def parse(text):
    """Every row declaration in one header, in source order.

    ONE PASS, THREE STATES: no container (top level — enums, typedefs, externs, macros), a container
    (`@interface`/`@protocol`, whose members belong to it), and INSIDE A CONTAINER'S IVAR BLOCK, which is
    skipped by brace counting. That third state matters here in a way it never did in the modern SDK: a
    Leopard-era `@interface` carries a `{ … }` block of ivars (now with `@private` inside it), and those
    blocks contain nested `struct … { … }` definitions and bitfields whose `}` would otherwise read as
    `@end`.

    A FOURTH PIECE OF STATE CAME WITH THE PROTOCOLS: whether a member is currently optional, set by
    `@optional`/`@required`. It matters more at 10.6 than at 10.5 — 43 `@optional` against 4 — because
    that is how a delegate protocol says which half a conforming class must implement.

    `@property` IS READ AS A ROW HERE. It could not be at 10.2 or 10.5 (both corpora have zero), so this
    is the era where the shape arrives and the parser learns it: name, owner, availability and the
    `getter=` attribute, in the same vocabulary as every other kind. `@synthesize`/`@dynamic` are
    implementation-side and appear in no header of this corpus (measured: 0)."""
    rows = []
    lines = strip_comments(text).splitlines()
    n = len(lines)
    cur = None
    ivar_depth = 0
    ivar_pending = False
    optional = False
    i = 0
    while i < n:
        s = lines[i].strip()
        if not s:
            i += 1
            continue
        if ivar_depth:                                  # inside the ivar block: nothing to see
            # A REPEATED `@interface … {` LINE IS 10.5'S OWN SHAPE AND MUST NOT BE BRACE-COUNTED.
            # 10.5 prototypes a class's ivar block ONCE PER PREPROCESSOR BRANCH:
            #     #if MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_3
            #     @interface NSLayoutManager : NSObject <NSCoding, NSGlyphStorage> {
            #     #else
            #     @interface NSLayoutManager : NSObject <NSCoding> {
            #     #endif
            # — one ivar block, two `{`. Counting the second copy opens a block that is never closed,
            # so the parser stays "inside ivars" for the REST OF THE FILE and loses every member after
            # it: measured on NSLayoutManager.h, 0 of 137 methods before this line existed, and 18 of
            # 177 headers were short by one to three members. (10.2's corpus has no such line — its own
            # audit is clean — which is why the clone needed to learn this and the original did not.)
            if not s.startswith("@interface"):
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
        if s.startswith("@optional"):
            optional = True
            i += 1
            continue
        if s.startswith("@required"):
            optional = False
            i += 1
            continue
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
            optional = False
            i += 1
            continue
        if s.startswith("@end"):
            cur = None
            ivar_pending = False
            optional = False
            i += 1
            continue
        if s.startswith(("@class", "#import", "#include", "#pragma")):
            i += 1
            continue
        if cur is not None:
            if s.startswith("@property"):
                # THE ONE SHAPE 10.6 ADDED TO THE MEMBER GRAMMAR. It is tested BEFORE the `+`/`-` check
                # because a property line starts with `@`; its owner and attribute rules are the
                # method's, and `getter=` travels in `why` (see why_of/property_of).
                decl, i = join_decl(lines, i)
                intro, dep = availability(decl)
                name, getter = property_of(strip_availability(decl))
                if name:
                    owner = cur["name"] if cur["kind"] in ("class", "protocol") else cur["cls"]
                    base = ("category %s" % cur["name"]) if cur["kind"] == "category" else "-"
                    rows.append({"kind": "property", "name": name, "owner": owner,
                                 "why": why_of(base, intro, dep, optional, getter),
                                 "getter": getter, "container": cur["name"]})
                continue
            if s[0] in "+-":
                decl, i = join_decl(lines, i)
                intro, dep = availability(decl)
                sel = selector_of(strip_availability(decl))
                if sel:
                    # A CATEGORY'S MEMBERS BELONG TO THE CLASS IT EXTENDS, not to the category: the
                    # owner is what the status test looks the selector up under, and a delegate
                    # category's `-tableView:…` is a method on whatever implements it.
                    owner = cur["name"] if cur["kind"] in ("class", "protocol") else cur["cls"]
                    base = ("category %s" % cur["name"]) if cur["kind"] == "category" else "-"
                    rows.append({"kind": "method", "name": sel, "owner": owner,
                                 "why": why_of(base, intro, dep, optional),
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
        intro, dep = availability(decl)
        d = strip_availability(decl).strip()
        if not d:
            i += 1
            continue
        why = why_of("-", intro, dep)
        if re.match(r"(typedef\s+)?enum\b", d):
            name = tail_name(d) if d.startswith("typedef") else None
            if name:
                rows.append({"kind": "enum", "name": name, "owner": "-", "why": why,
                             "container": name})
            for c in enum_cases(d):
                rows.append({"kind": "case", "name": c, "owner": name or "-", "why": why,
                             "container": name or c})
            continue
        if re.match(r"(typedef\s+)?struct\b", d):
            name = tail_name(d)
            if name:
                rows.append({"kind": "struct", "name": name, "owner": "-", "why": why,
                             "container": name})
            continue
        if d.startswith("typedef"):
            name = tail_name(d)
            if name:
                rows.append({"kind": "typealias", "name": name, "owner": "-", "why": why,
                             "container": name})
            continue
        if re.search(r"\b(APPKIT_EXTERN|extern)\b", d) or re.match(r"[\w\s\*]+\([^;]*\)\s*;$", d):
            if "(" in d:
                fm = re.search(r"([A-Za-z_]\w*)\s*\(", d)
                name = fm.group(1) if fm else tail_name(d)
                if name:
                    rows.append({"kind": "func", "name": name, "owner": "-", "why": why,
                                 "container": name})
            else:
                name = tail_name(d)
                if name:
                    rows.append({"kind": "var", "name": name, "owner": "-", "why": why,
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
    (kind, name, owner) for containers, (owner, selector) for methods, and (owner, name) for
    properties — the third is 10.6's addition, and it is what lets a property row be credited from
    either the property itself or the accessors a method-shaped surface declares."""
    containers, methods, properties = set(), set(), set()
    for path in sorted(glob.glob(OURS)):
        for rd in parse(open(path, encoding="utf-8", errors="replace").read()):
            if rd["kind"] in ("class", "protocol", "category"):
                containers.add((rd["kind"], rd["name"], rd["owner"]))
            elif rd["kind"] == "method":
                methods.add((rd["owner"], rd["name"]))
            elif rd["kind"] == "property":
                properties.add((rd["owner"], rd["name"]))
    return containers, methods, properties


def cluster_of(kind, name, owner):
    """A row's cluster. Classes and protocols are looked up by name; a CATEGORY **or a PROTOCOL** is
    placed by the longest class-name prefix in its own name (so `NSTableViewDelegate` lands with
    `NSTableView`), then by the class it extends, then by the Foundation bucket. `unassigned` is a
    FAILURE, not a bucket — that is how the table is kept complete as the corpus is re-read.

    THE PREFIX RULE WAS EXTENDED TO PROTOCOLS AT 10.6 AND THAT IS A MEASURED DECISION: 10.5 has 18
    protocols, so they could be listed by hand; 10.6 has 57, and 39 of them are new delegates whose
    names are the class they serve plus `Delegate`/`DataSource`. Hand-listing 39 entries would have to
    be redone at the next era, and the rule's justification is already written for categories: THE SDK
    NAMES A DELEGATE AFTER THE CLASS IT SERVES."""
    c = CLUSTERS.get(name)
    if c:
        return c
    if kind in ("category", "protocol"):
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


def status_of(row, names, containers, methods, properties=()):
    """Shipped or open, decided by FORM:
      * a container is shipped when we declare it as ANYTHING (the other sweeps' rule) — and a
        category, when we declare that category on that class;
      * a method is shipped when its OWNER declares that whole selector — the owner-scoped test the
        live ledger says it cannot do;
      * a PROPERTY is shipped when our surface declares the property itself, OR the accessor pair a
        method-shaped surface would carry (`-name` and `-setName:`, or the `getter=` selector);
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
    if kind == "property":
        # OUR APPKIT IS METHOD-SHAPED (it was built against the 10.2 and 10.5 pins), so a property is
        # satisfied either way — as a `@property`, or as the accessors it would synthesize.
        if (owner, name) in properties:
            return True
        if (owner, "- " + (row.get("getter") or name)) in methods:
            return True
        return (owner, "- set" + name[0].upper() + name[1:] + ":") in methods
    return name in names


def refresh():
    corpus = read_corpus(CORPUS)
    if not corpus:
        # the path AS GIVEN when it is outside the tree, because a relative path that has to climb out
        # (`../../../../nonexistent`) hides the directory the reader actually needs to look at
        shown = os.path.relpath(CORPUS, ROOT)
        if shown.startswith(".."):
            shown = CORPUS
        print("appkit-106-sweep: no corpus at %s" % shown)
        print("  Fetch the SDK first (the recipe is in docs/design/cocoa-parity-plan.md §9a); the")
        print("  corpus is Apple's and is NOT in this tree.")
        return 1

    # COMPLETENESS, because an incomplete corpus turns our spelling into Apple's silence.
    declared = " ".join(corpus.values())
    missing = [n for n in CORPUS_MUST_DECLARE
               if not re.search(r"@(?:interface|protocol)\s+%s\b" % re.escape(n), declared)]
    missing += [n for n in CORPUS_MUST_CATEGORIZE
                if not re.search(r"@interface\s+\w+\s*\(\s*%s\s*\)" % re.escape(n), declared)]
    missing += [n for n in CORPUS_MUST_PROTOCOLIZE
                if not re.search(r"@protocol\s+[^;]*\b%s\b" % re.escape(n), declared)]
    # and the umbrella's imports must all resolve, or a header the SDK ships is not being read
    wanted = set(re.findall(r"#import\s+<AppKit/([A-Za-z_]\w*)\.h>", corpus.get("AppKit.h", "")))
    unresolved = sorted(w for w in wanted if (w + ".h") not in corpus)

    # THE GUARD IS NOW A MEASUREMENT. 10.5's instrument counted `@property` occurrences it could not
    # read and said so; 10.6 is the era where the shape arrives, so this instrument READS them (27 rows,
    # in the four headers that are new at this era) and the count below is the CROSS-CHECK that the
    # parser saw every one: a declaration the reader missed would show up here as a mismatch, not as a
    # silent hole.
    props = sum(len(re.findall(r"^\s*@property\b", strip_comments(t), re.M))
                for t in corpus.values())

    parsed = {fname: parse(corpus[fname]) for fname in corpus}
    rows = []
    for fname in sorted(parsed):
        for rd in parsed[fname]:
            rd["file"] = "AppKit/" + fname
            rd["fname"] = fname
            rows.append(rd)
    prop_rows = sum(1 for r in rows if r["kind"] == "property")
    intro_n = sum(1 for r in rows if r["why"].startswith("introduced 10.6")
                  or r["why"].endswith(", introduced 10.6"))
    dep_n = sum(1 for r in rows if "deprecated " in r["why"])
    opt_n = sum(1 for r in rows if "optional" in r["why"])
    getter_n = sum(1 for r in rows if "getter " in r["why"])

    containers, methods, properties = our_surface()
    names = our_names()
    fclusters = file_clusters(parsed)

    # DEDUPE FIRST, and the reason is measured rather than tidy: the same declaration appears more than
    # once in a header set — a macro defined once per platform branch (`APPKIT_EXTERN` is six `#define`s
    # in `AppKitDefines.h`), a method declared in the class's own block and again in a category block, a
    # class declared in two headers. A work list counts WORK, so there is one row per (kind, name,
    # owner).
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
    # THIS SDK'S OWN CONVENTION (its headers say so in prose, and every `_`-prefixed struct here is an
    # ivar bitfield): such a row is real — it IS declared in a public header — but it is not API a caller
    # writes, so `why` says what kind of work it is (APPENDING, because the row may already carry an
    # availability note).
    seen = {}
    for rd in rows:
        if rd["kind"] in ("class", "protocol", "category"):
            seen[rd["name"]] = cluster_of(rd["kind"], rd["name"], rd["owner"])
    for r in rows:
        if r["kind"] in ("method", "property"):
            r["cluster"] = seen.get(r["container"]) or "unassigned"
        elif r["kind"] in ("class", "protocol", "category"):
            r["cluster"] = cluster_of(r["kind"], r["name"], r["owner"])
        else:
            r["cluster"] = fclusters.get(r["fname"], "unassigned")
        if r["name"].startswith("_"):
            r["why"] = ("private (underscore-prefixed in the header)" if r["why"] == "-"
                        else r["why"] + ", private (underscore-prefixed in the header)")
        r["status"] = (STATUS_SHIPPED
                       if status_of(r, names, containers, methods, properties) else STATUS_OPEN)

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
        "# The Application Kit AT 10.6, against this tree — the era-pinned work list.",
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
        "# (12,475 rows, 10.15-era), and this one ledgers the AppKit 10.6's own headers declare.",
        "# ITS SIBLINGS docs/reference/appkit-10{2,5}-worklist.txt are the same instrument on the",
        "# earlier SDKs; `%s --delta` prints the difference against 10.5." % TOOL,
        "#",
        "# THE ERA, IN THE DATA (verified over this corpus, not assumed):",
        "#   property rows %d — THE SHAPE ARRIVES HERE. 10.5's corpus had zero `@property` and this" % prop_rows,
        "#                     one has %d, all in FOUR HEADERS THAT ARE NEW AT THIS ERA" % props,
        "#                     (NSTextInputContext, NSTouch, NSRunningApplication, NSOpenGLLayer): a",
        "#                     handful of new classes are declared in the new style while every older",
        "#                     header stays accessor-based. %d of the rows carry a `getter=` attribute," % getter_n,
        "#                     so the accessor a conforming class implements is not the property's name.",
        "#   %d protocol row(s) DECLARE THE DELEGATE SURFACE THAT THE EARLIER ERAS EXPRESS AS" % sum(1 for r in rows if r["kind"] == "protocol"),
        "#                     `@interface NSObject (NSXxxDelegate)` CATEGORIES. This is the era's real",
        "#                     change and it is the one a clone must plan for: the same selectors, declared",
        "#                     by a formal protocol with `@optional` members instead of an informal",
        "#                     category (%d protocol member(s) carry `optional`)." % opt_n,
        "#   %d row(s) are NEW IN THIS ERA (`introduced 10.6`) and %d were already deprecated by it —" % (intro_n, dep_n),
        "#                     the availability macros, carried in `why`.",
        "#   struck rows %d — 10.6 carries a deprecation ground (the macros above) and a deprecated" % sum(c for (k, s), c in counts.items() if s == STATUS_STRUCK),
        "#                     row is still OWED rather than struck (the user's policy, 2026-09-26).",
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
    header.append("# BY CLUSTER (the family column; OURS and stated as a rule — an SDK carries no")
    header.append("# taxonomy, unlike the live ledger's family column):")
    for cl in CLUSTER_ORDER:
        o = fams.get((cl, STATUS_OPEN), 0)
        s = fams.get((cl, STATUS_SHIPPED), 0)
        if o or s:
            header.append("#   %-20s open %5d   shipped %5d" % (cl, o, s))
    if missing or unresolved or unassigned or prop_rows != props:
        header.append("#")
    if prop_rows != props:
        header.append("# ⚠⚠ %d `@property` DECLARATION(S) IN THE CORPUS BUT %d PROPERTY ROW(S) PARSED —"
                      % (props, prop_rows))
        header.append("#     the reader and the corpus disagree; the counts below are BELOW the")
        header.append("#     framework's real surface until that is fixed.")
    if missing:
        header.append("# ⚠⚠ CORPUS INCOMPLETE: %d name(s) that MUST exist are absent — %s"
                      % (len(missing), ", ".join(missing)))
    if unresolved:
        header.append("# ⚠⚠ THE UMBRELLA IMPORTS %d HEADER(S) THE CORPUS DOES NOT CARRY — %s"
                      % (len(unresolved), ", ".join(unresolved)))
    if unassigned:
        header.append("# ⚠⚠ %d ROW(S) HAVE NO CLUSTER — add them to CLUSTERS; --check fails on this:"
                      % len(unassigned))
        for r in unassigned:
            header.append("#     unassigned: %s %s" % (r["kind"], r["name"]))

    open(SURFACE, "w", encoding="utf-8").write("\n".join(header + out) + "\n")
    for (kind, st), c in sorted(counts.items()):
        print("  %-10s %-8s %5d" % (kind, st, c))
    print("appkit-106-sweep: %d rows (%d open) from %d header(s) -> %s"
          % (total, open_, len(corpus), os.path.relpath(SURFACE, ROOT)))
    print("  property rows %d of %d @property declaration(s); introduced-10.6 rows %d, "
          "deprecated rows %d, optional members %d" % (prop_rows, props, intro_n, dep_n, opt_n))
    if prop_rows != props:
        print("  ⚠⚠ %d @property declaration(s) in the corpus, %d parsed — see the header"
              % (props, prop_rows))
    if missing:
        print("  ⚠⚠ corpus incomplete: %s" % ", ".join(missing))
    if unresolved:
        print("  ⚠⚠ umbrella imports missing from the corpus: %s" % ", ".join(unresolved))
    if unassigned:
        print("  ⚠⚠ %d unassigned row(s): %s"
              % (len(unassigned), ", ".join("%s %s" % (r["kind"], r["name"]) for r in unassigned)))
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
    here: this instrument's `struck` is never assigned, so there is nothing for `--strict` to be about
    (the other ledgers' policy class is their deprecation ground, which for the era ledgers is OWED)."""
    try:
        rows = read_surface()
    except FileNotFoundError:
        print("appkit-106-sweep: no %s yet — run --refresh" % os.path.relpath(SURFACE, ROOT))
        return 1
    containers, methods, properties = our_surface()
    names = our_names()
    bad, counts, fams = [], {}, {}
    for kind, status, name, owner, family, why, src in rows:
        counts[(kind, status)] = counts.get((kind, status), 0) + 1
        fams[(family, status)] = fams.get((family, status), 0) + 1
        # the ledger does not carry `getter`, and it does not need to: a property whose getter is not
        # the property's name is credited by the property or by `-setName:` as well, and the row's
        # memory of the attribute lives in `why`.
        found = status_of({"kind": kind, "name": name, "owner": owner}, names, containers, methods,
                          properties)
        if status == STATUS_SHIPPED and not found:
            bad.append("STALE SHIPPED CLAIM    %-9s %s [%s] — the file says we ship it and our "
                       "headers do not declare it" % (kind, name, owner))
        elif status == STATUS_OPEN and found:
            bad.append("PRESENT BUT LISTED OPEN %-8s %s [%s] — our headers now declare it; flip "
                       "the row" % (kind, name, owner))
    print("appkit-106-sweep: %d symbols in the work list" % len(rows))
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
    print("appkit-106-sweep: consistent — every shipped name is declared, every open name is absent, "
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
    out = ["<!-- BEGIN %s (generated by %s --families; do not hand-edit) -->" % (PLAN_MARKER, TOOL),
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
    out.append("<!-- END %s -->" % PLAN_MARKER)
    return "\n".join(out)


def families(write=False):
    block = families_block()
    if not write:
        print(block)
        return 0
    doc = open(PLAN, encoding="utf-8").read()
    begin = "<!-- BEGIN %s" % PLAN_MARKER
    end = "<!-- END %s -->" % PLAN_MARKER
    b = doc.find(begin)
    if b < 0:
        print("appkit-106-sweep: no family block in %s — add the markers first"
              % os.path.relpath(PLAN, ROOT))
        return 1
    e = doc.find(end, b)
    if e < 0:
        print("appkit-106-sweep: the family block's END marker is missing")
        return 1
    open(PLAN, "w", encoding="utf-8").write(doc[:b] + block + doc[e + len(end):])
    print("appkit-106-sweep: rewrote the family block in %s" % os.path.relpath(PLAN, ROOT))
    return 0


def read_other_surface(path=None):
    """The 10.2 artefact, in the same row vocabulary — `--delta`'s other side."""
    path = path or SURFACE_PREV
    rows = []
    for line in open(path, encoding="utf-8"):
        if line.startswith("#") or not line.strip():
            continue
        rows.append(tuple(line.rstrip("\n").split("\t")))
    return rows


def delta(other=None):
    """WHAT 10.6 HAS THAT 10.5 DOES NOT, AND THE REVERSE — computed from the two LEDGERS.

    A ROW'S IDENTITY IS (kind, name, owner) FOR THE KINDS WHOSE OWNER IS A CLASS (class, protocol,
    category, method, property), and (kind, name) FOR THE KINDS THAT HAVE NO CLASS OWNER AT ALL (case,
    var, func, struct, typealias, macro) — MEASURED, not tidiness: 10.5 rewrote the era's enumerations
    from `typedef enum _NSBorderType { … } NSBorderType;` to `typedef NSUInteger NSBorderType;` plus a
    separate ANONYMOUS `enum { … };`, so the SAME enumerator's owner column changes from the enum's name
    to `-` between the ledgers. Comparing those on the owner would report ~600 enumerators as
    simultaneously removed and added, which is the instrument lying about the framework.

    THE STATUS COLUMN IS NOT PART OF A ROW'S IDENTITY here either: the question is what the FRAMEWORK
    declares, and what this tree implements is what the two files' status columns are for.

    THE SECOND GROUND IS A FILE THIS TREE COMMITS (docs/reference/appkit-105-worklist.txt), so this mode
    needs no SDK and no network: it is offline like `--check`, and when the file is missing it says so
    rather than reporting an empty difference."""
    other = other or SURFACE_PREV
    try:
        theirs = read_other_surface(other)
    except FileNotFoundError:
        print("appkit-106-sweep: no %s — the %s ledger is this mode's other ground"
              % (os.path.relpath(other, ROOT), PREV_ERA))
        print("  It is committed; if it is gone, restore it before asking for a delta.")
        return 1
    mine = read_surface()

    OWNED = ("class", "protocol", "category", "method", "property")

    def key(r):
        return (r[0], r[2], r[3]) if r[0] in OWNED else (r[0], r[2])

    a = {key(r): r for r in theirs}
    b = {key(r): r for r in mine}
    added = [b[k] for k in b if k not in a]
    gone = [a[k] for k in a if k not in b]

    def by_kind(rows):
        out = {}
        for r in rows:
            out[r[0]] = out.get(r[0], 0) + 1
        return out

    print("appkit-106-sweep --delta: 10.6 (%d rows) against %s (%d rows)" % (len(b), PREV_ERA, len(a)))
    for label, rows in (("ONLY IN 10.6 — new work", added),
                        ("ONLY IN %s — gone by 10.6" % PREV_ERA, gone)):
        print("\n# %s: %d row(s)" % (label, len(rows)))
        for kind, n in sorted(by_kind(rows).items()):
            print("   %-10s %5d" % (kind, n))
    new_classes = sorted(r[2] for r in added if r[0] == "class")
    gone_classes = sorted(r[2] for r in gone if r[0] == "class")
    print("\n# CLASSES ADDED BY 10.6 (%d)" % len(new_classes))
    for n in new_classes:
        print("   " + n)
    print("\n# CLASSES GONE BY 10.6 (%d)" % len(gone_classes))
    for n in gone_classes:
        print("   " + n)
    print("\n# ADDED ROWS BY CLUSTER:")
    agg = {}
    for r in added:
        agg[r[4]] = agg.get(r[4], 0) + 1
    for cl in CLUSTER_ORDER:
        if cl in agg:
            print("   %-20s %5d" % (cl, agg[cl]))
    print("\n# ADDED ROWS BY `why` (the era notes: what KIND of new work it is):")
    note = {}
    for r in added:
        note[r[5]] = note.get(r[5], 0) + 1
    for why, n in sorted(note.items(), key=lambda kv: -kv[1])[:8]:
        print("   %-30s %5d" % (why[:30], n))

    # REPRESENTATION DRIFT — THE RECONCILIATION THAT KEEPS THE TWO COUNTS ABOVE HONEST. A name can be in
    # BOTH ledgers and still appear in both the added and the gone lists, because its KIND changed: 10.5
    # rewrote every enumeration from `typedef enum _NSBorderType { … } NSBorderType;` to
    # `typedef NSUInteger NSBorderType;` plus an anonymous `enum { … };`, so `NSBorderType` is an `enum`
    # row at 10.2 and a `typealias` row at 10.5. Without this section the delta would read as "Apple
    # removed 73 enums and added 121 typedefs", which is a true pair of counts and a false story.
    kprev, khere = {}, {}
    for r in theirs:
        kprev.setdefault(r[2], set()).add(r[0])
    for r in mine:
        khere.setdefault(r[2], set()).add(r[0])
    shapes = {}
    for name in kprev:
        if name in khere and kprev[name] != khere[name]:
            shapes.setdefault((tuple(sorted(kprev[name])), tuple(sorted(khere[name]))), []).append(name)
    print("\n# REPRESENTATION DRIFT — names in BOTH ledgers whose KIND changed (%d name(s)):"
          % sum(len(v) for v in shapes.values()))
    for (old, new), names in sorted(shapes.items(), key=lambda kv: -len(kv[1])):
        print("   %-28s -> %-28s %5d   e.g. %s"
              % ("/".join(old), "/".join(new), len(names), ", ".join(sorted(names)[:3])))

    # RE-HOMING — THE SECOND RECONCILIATION, AND IT IS THIS ERA'S OWN. A row can leave one owner and
    # arrive at another WITHOUT being new API, and 10.6 does that to the whole delegate surface: 10.5
    # declares `@interface NSObject (NSTableViewDelegate)` and 10.6 declares `@protocol
    # NSTableViewDelegate`, so the same selector leaves `NSObject` and arrives at the protocol. The
    # pairing below is exact rather than heuristic (one GONE row and one ADDED row with the same
    # (kind, name)), which is what makes it reportable while the looser "the selector exists somewhere"
    # test in §8d stayed a scratch measurement: THAT one could not tell a re-home from a common name.
    gone_by_name, added_by_name = {}, {}
    for r in gone:
        gone_by_name.setdefault((r[0], r[2]), []).append(r[3])
    for r in added:
        added_by_name.setdefault((r[0], r[2]), []).append(r[3])
    moves = {}
    pairs = 0
    for k in gone_by_name:
        if k in added_by_name:
            for old in gone_by_name[k]:
                for new in added_by_name[k]:
                    moves[(old, new)] = moves.get((old, new), 0) + 1
                    pairs += 1
    print("\n# RE-HOMED — rows whose (kind, name) left one owner and arrived at another (%d pair(s)):"
          % pairs)
    for (old, new), n in sorted(moves.items(), key=lambda kv: -kv[1])[:8]:
        print("   %-28s -> %-28s %5d" % (old, new, n))
    return 0


def audit():
    """THE PARSE-ACCOUNTING RULE §8h OF THE PLAN SAYS WAS MISSING, AS A MODE.

    For every header: the raw `@interface`/`@protocol` lines and the raw member lines must equal what
    `parse()` produced. It is the check that found two real defects in the earlier instruments — one of
    which had already shipped a short ledger — and it is a mode here rather than a scratch script
    because it is the instrument's own proof that it read the corpus it claims to have read.

    TWO ACCOUNTING RULES, EACH OF WHICH COST A FALSE ALARM ONCE, AND BOTH ARE IN THE CODE:
      * a member line is `[-+]` followed by `(` OR an identifier — `- initWithDelegate:name:` declares a
        method with NO return type, and a rule that required `(` reported 10.2's NSInputServer.h as
        having one method too many;
      * a DUPLICATED container line is ONE container — 10.5/10.6 repeat `@interface X : Y {` inside
        `#if`/`#else`, and comparing raw LINES against parsed ROWS reported every such file as short."""
    corpus = read_corpus(CORPUS)
    if not corpus:
        print("appkit-106-sweep: --audit needs the corpus at %s" % CORPUS)
        return 1
    print("appkit-106-sweep --audit: %d header(s)" % len(corpus))
    diff = 0
    for fname in sorted(corpus):
        text = strip_comments(corpus[fname])
        raw_c = set()
        for line in text.splitlines():
            m = re.match(r"\s*@interface\s+[A-Za-z_]\w*\s*\(\s*([A-Za-z_]\w*)\s*\)", line)
            if m:
                raw_c.add(m.group(1))
                continue
            m = re.match(r"\s*@(?:interface|protocol)\s+([A-Za-z_]\w*)", line)
            if m:
                raw_c.add(m.group(1))
        raw_m = len(re.findall(r"^\s*[-+]\s*[\(A-Za-z_]", text, re.M))
        raw_p = len(re.findall(r"^\s*@property\b", text, re.M))
        rows = parse(corpus[fname])
        got_c = {r["name"] for r in rows if r["kind"] in ("class", "protocol", "category")}
        got_m = len([r for r in rows if r["kind"] == "method"])
        got_p = len([r for r in rows if r["kind"] == "property"])
        if raw_c != got_c or raw_m != got_m or raw_p != got_p:
            diff += 1
            print("  DIFF %-28s containers %d/%d %s  members %d/%d  properties %d/%d"
                  % (fname, len(got_c), len(raw_c), sorted(got_c ^ raw_c), got_m, raw_m,
                     got_p, raw_p))
    bad = [(f, r["kind"], r["name"]) for f in sorted(corpus) for r in parse(corpus[f])
           if re.search(r"AND_LATER|AVAILABLE_MAC_OS|DEPRECATED_IN_MAC", r["name"])]
    print("files whose parsed counts differ from the raw lines: %d" % diff)
    print("rows whose NAME is an availability-macro remnant: %d %s" % (len(bad), bad[:3]))
    return 1 if (diff or bad) else 0


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
        print("appkit-106-sweep: --order needs the corpus at %s" % os.path.relpath(CORPUS, ROOT))
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
    if mode == "--delta":
        rest = [a for a in argv[2:] if not a.startswith("-")]
        return delta(rest[0] if rest else None)
    if mode == "--audit":
        return audit()
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
