# Argentum TextView (L7 rich view)

Status: **DRAFT (2026-09).** Design + split for the `TextView` —
the L7 rich view of `docs/design/argentum-uikit-plan.md` and the
catalog's first Tier-3 staged class (`docs/design/argentum-uikit-catalog.md`
"after TableView"). S4/S5 (Kestrel + desktop gate) are parked while
the uikit catalog proceeds; TextView is the designated next staged
class — S3.2 built TextField's edit engine explicitly *"shared with
TextView later"*, and multi-line layout is the one new machinery the
v1 catalog never needed. Same working pattern: design doc first, one
observable slice at a time, each gated in-guest and committed.

## 1. Scope

1. **TXT-a — layout + read-only draw.** A `TextView` (a plain `View`
   subclass, like Label — no `Control` yet) that owns a multi-line
   UTF-8 document and draws it laid out: hard breaks on `\n`, greedy
   word wrap at the content width, a fixed line box. New a11y role
   `TextArea` (role name "text area"); the a11y value mirrors the
   document. *Acceptance:* a board shows a paragraph with a forced
   break wrapping over several lines inside a fixed-width view; pixel
   probes land on glyphs of wrapped lines 2+; the role reads back.
2. **TXT-b — document editing.** The full TextField edit surface over
   the multi-line document: click-to-position (x,y → line+char),
   typing at the caret (Return inserts `\n` — no end-edit commit for
   a document editor), BackSpace/Delete, Left/Right within a line,
   **Up/Down across lines at a remembered goal column**, Home/End at
   line edges, Shift extends a **cross-line selection**, typing or
   Delete removes the selection, `valueChanged()` after every edit.
   *Acceptance:* a self-injected key sequence (widgets_d pattern)
   builds a two-line document, selects across the line break, deletes
   and re-types; the logged `value()`/`caretIndex()`/selection match
   exactly and a screendump shows the cross-line selection fill.
3. **TXT-c — scroll integration.** TextViews live inside the existing
   `ScrollView` (S2.4b): the TextView IS the document view (its frame
   = content size; the ScrollView pans + clips). A new
   `ScrollView::scrollRectToVisible(Rect)` clamps the offset so the
   rect shows; the TextView auto-scrolls when an edit or caret motion
   leaves the visible area (nearest ScrollView ancestor). *Acceptance:*
   a tall document; typing at the bottom pans the viewport (offset
   logged + the caret line's pixels land in the viewport).

## 2. What is reused vs new

- **Reused (no changes):** the S2.6 text pipeline — per-line runs are
  plain `textRunPrepare`/`drawText` calls over the shared face + shape
  cache, so per-draw layout is cheap; `textMetrics()` gives the line
  box; the GC draw honors the view translate/clip (overflow clips for
  free); ScrollView, Control focus/key routing, the `KeyEvent` shape,
  and the `widgets_d`-style self-injected key gates.
- **Shared with TextField:** the *semantics* of the byte-based UTF-8
  caret/selection/edit ops (insert-over-selection, delete-range,
  BackSpace at a char boundary, arrows collapse a selection without
  Shift). TextField's fixed `char[256]` buffer and its `charStart/
  charEnd` API are load-bearing for S22D and stay untouched; the utf8
  boundary helpers (`prevCharStart`/`nextCharEnd`/`isCont`) move to a
  shared internal header (`text_utf8.h`, static) so both files use one
  copy. TextView's document is a growable `std::string` with the same
  byte ops implemented over it. A full internal-editor-object refactor
  of TextField is NOT done here (S22D regression risk); the shared
  engine is the documented op set + the utf8 helpers.
- **New machinery:** line layout (split + greedy wrap + line box) and
  the two mappings it enables — byte↔(line, x) for hit-testing, and
  byte↔(line, offset) for vertical cursor motion with a goal column.

## 3. Design decisions

- **Document model.** One plain UTF-8 string; `\n` = a hard line
  break (a `\r` immediately before a `\n` is stripped on input);
  selection/typing = exactly TextField's byte semantics over the whole
  document (the caret/anchor pair already spans lines naturally — a
  line break is just a character). v1 has no attributes/runs/rich
  formatting; the class is named for the future, not for today's
  scope.
- **Layout.** Computed on demand from (text, wrap width px, font px)
  and memoized with a dirty flag (invalidated by setValue/edits and by
  frame width changes). Hard lines split the text at `\n`; each hard
  line is then greedy word-wrapped at the content width (a space is
  the only break opportunity; a word longer than the width
  character-wraps as a fallback). Layout output per visual line:
  `{startByte, endByte, pixelY, pixelWidth}`. A public read-only
  `lineCount()`/`lineText(i)` exposes the visual lines for gates and
  future a11y/scroll.
- **Line box.** `textMetrics(family, sizePt, "Ag")` → ascent/descent;
  the line box height = ascent + descent + a fixed 4 pt leading
  (leading is a plain constant in v1, not a theme key). Baseline
  y = lineTop + ascent.
- **Inset.** Text draws inset by the theme's `radius`-ish padding
  (TextField uses 3 px; TextView insets 4 px left and 2 px top for
  the caret/selection to breathe at the edges).
- **Caret/selection rendering.** TextField's draw pattern
  (pre/selection/post per line, accent fill + white glyphs for the
  selection) is extended line-wise: each visual line is split at its
  selection intersection. The caret is a 1 px bar at the caret's
  (line, x) while focused, exactly like TextField.
- **Editing surface.** `setValue(const char*)` replaces the document
  and puts the caret at the END (TextField parity); `value()` returns
  the document; `caretIndex()/selectionStart()/selectionEnd()/
  setSelection()` mirror TextField's; `valueChanged()` fires after
  every edit. Return inserts `\n` (a document editor has no Return
  commit; there is no endEditing/onEndEdit on TextView in v1).
- **Focus/keyboard.** TextView is a plain View in TXT-a; TXT-b makes
  it a `Control` (click-to-focus, `acceptsFirstResponder` = enabled,
  focused chrome not drawn — a text view has no chrome) so traversal
  and key routing work. Printable keys (no Ctrl/Alt, `>= 0x20`,
  `!= 0x7f`) insert; arrows/Home/End/BackSpace/Delete as in §1;
  Up/Down preserve a **goal column** (the x of the last horizontal
  motion; vertical moves land on the char whose advance is closest).
- **A11y.** New `AccessibilityRole::TextArea` + `accessibilityRoleName`
  → "text area". The role is "text area" not "text view": the
  platform role vocabulary (catalog §) uses the TextArea naming for
  the editable multi-line text primitive, and read-back stays
  unambiguous with StaticText.
- **Scrolling (TXT-c).** The TextView is its own document view: the
  board sizes its frame to the content (content width for wrap,
  `lineCount() × lineBox` tall) inside a ScrollView whose bounds are
  the viewport. `ScrollView::scrollRectToVisible(const Rect &)` (pt,
  in document coordinates) clamps `scrollTo` so the rect is visible.
  After every caret move/insert/delete, the TextView finds the nearest
  ScrollView ancestor (`superview()` walk + `dynamic_cast`; RTTI is
  adopted per plan §8) and calls `scrollRectToVisible` on the caret's
  line rect. PageUp/PageDown and wheel scrolling stay out of v1 (no
  wheel source exists; noted for later).

## 4. Slices (split for gating)

Per-slice recipe, as everywhere: add `textview.cpp` (+ slice APIs) to
`ARGENTUM_SRCS`, a `userland/tests/textview_X.cpp` board staged as
`System/Shared/tests/textview_X`, a `.build/s_txtX_run.sh` +
`_assert.py` gate (boot rootagfs.img image with the staged board;
self-inject keys over a second X connection before `run()`; screendump
pixel probes + guest-log assertions; `halt -f`), then regressions
(S13-PIXELS, S22D, S31A/B, S32, zoo-adjacent S24X) and a commit.

### TXT-a — layout + read-only draw
Status: **DONE** — `TextView` (a Control-shaped View) owns a
`std::string` document (`
`→`
` on setValue), memoizes the layout
(byte-range visual lines; recomputed on text or width change), draws
each visual line in the theme font at a fixed line box (ascent +
descent + 4 pt leading). Wrap = greedy at spaces; a single word wider
than the content width character-wraps; a hard break's segment can be
empty (blank line). Public `lineCount()`/`lineText(i)` expose the
visual lines. A11y `TextArea` role ("text area"), value mirrors the
document. Gate `.build/s_txta_run.sh` + `s_txta_assert.py` ->
S-TXTA-OK (10 checks: the logged wrap = 5 lines — paragraph 1 wrapped
to 3, `

` blank preserved, paragraph 2 after the break; role reads
back; screendump ink probes land on each non-blank line's band, the
blank band is clean, page below). Regressions S22D-OK, S31A-OK,
S13-PIXELS-OK (6). Board `userland/tests/textview_a.cpp`.
*Acceptance:* `textview_a` shows, in one window at a fixed content
width, a document with a forced `\n` and a long paragraph wrapping to
≥ 3 visual lines. The board logs `TXT-A:` geometry lines (line count,
each line's start byte + pixel y); `TXT-A-A11Y: role=text area`
reads back. Pixel probes hit glyphs on wrapped lines 2/3 (below the
`\n` line); page background where the wrap ends. No editing yet.
Commit: `textview.cpp` layout+draw, role TextArea, `lineCount/
lineText`, board, gate.

### TXT-b — document editing
*Acceptance:* `textview_b` self-injects: click positions the caret on
line 1; type "ab", Return, type "cd"; Left/Left (to "cd" start),
Shift+Left×2 selects "cd"; typing "Z" replaces it; Down moves to line
1 at the goal column; End + Shift+Up selects line 1's tail; the final
logged `value()` = `"ab\nZ"`-shaped exactly with the caret/selection
at the logged byte offsets, and the screendump shows a cross-line
selection fill (accent between the two selection endpoints across the
line break). Regressions after TXT-b: S22D (TextField untouched but
the shared utf8 helpers move) + S13.
Commit: TextView Control + keyDown/mouseDown edit surface, board, gate.

### TXT-c — scroll integration
*Acceptance:* `textview_c` = a ScrollView viewport (e.g. 320×110 pt)
holding a TextView document tall enough to overflow (≥ 20 lines).
The board types at the caret while it sits below the viewport bottom;
after each edit the viewport pans so the caret line is visible (the
board logs `TXT-C: y=…` = contentOffsetY after each scroll-follow; a
final screendump shows the caret line's glyphs inside the viewport and
the scrolled-away top clipped). `scrollRectToVisible` clamp verified
by a `scrollTo` past the end (offset = doc − viewport).
Commit: ScrollView::scrollRectToVisible + TextView auto-scroll, board,
gate.

### TXT-d (not split) — reference notes
No TXT-d: the three slices above are the whole staged TextView v1
(plain multi-line editable text in a scroll view). Rich runs,
find/replace, undo, text views as a table cell's editor, and
TextView-inside-Kestrel all land later with their own consumers.

## 5. Regressions and risks

- S22D (`widgets_d`) exercises TextField end-to-end and is the
  canary for the shared-helper move in TXT-b — run it before and
  after.
- The S2.6 damage/composite path already clips every view to its
  bounds: a TextView taller than its frame clips for free, which is
  what makes "TextView as a document view" work without new clip code.
- Layout cost: one `textRunPrepare` per visual line per draw; the
  S2.6 shape cache keys on the string, so steady-state draws are cache
  hits. TXT-a's gate doubles as the layout-cost sanity check (the
  board draws on every Expose without stalling).
