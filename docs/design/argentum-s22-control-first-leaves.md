# Argentum S2.2 — Control + first leaves (plan L4)

Status: **DRAFT (2026-09).** Design + split for S2.2 of
`docs/design/argentum-milestone-split.md` ("Control + first leaves",
plan-§469 L4). Read alongside `argentum-uikit-plan.md` §7 L4,
`argentum-uikit-catalog.md` Tier 1, and the S2.1 View-tree doc
(`argentum-s21-view-tree.md`) whose View surface every widget builds
on. This doc is the design record: S2.2's split into small observable
slices, each green before the next and committed separately, exactly
like S2.1a–d.

## 1. Scope (L4, in order)

1. **Control** — base for user-interactive views: `enabled`, a
   `std::function` action, the 5-state chrome machine
   (`ControlState { Idle, Hover, Armed, Disabled, Focused }`) driven by
   events; fires the action on a click. No widget yet (verified by
   calling the action programmatically).
2. **Label** — the catalog's hello world: text + theme + draw + a11y
   in one view. Validates the text-in-view pipeline.
3. **Button** with `Type { Push, Checkbox, Radio }` — Control +
   focus/hover/chrome; push fires the action, checkbox/radio toggle
   state.
4. **TextField + the edit engine** — caret / selection / input (the
   first new subsystem). TextField is single-line; a label variant is
   the same class flag. SecureTextField/SearchField follow later (S3).

S2.2's milestone acceptance (milestone-split): *an interactive
reference-app slice showing label/button/textfield with a11y
role/label asserted* — delivered by S2.2d's gate board.

### 1a. User decisions (2026-09)

- **TextField depth: FULL edit engine in S2.2** — caret/selection/
  typing land here (S3.2 later deepens editing, secure field, …).
- **Focus model: minimal now** — click-to-focus, key events to the
  focused view, visible focus ring. S3.1 later adds traversal +
  keyboard equivalents on top of the same model.
- **Text into the tree: GC-level text** — a
  `GraphicsContext::drawText` composites glyphs into the offscreen
  BitmapImage (pixman OP_OVER through an A8 coverage mask), honoring
  the per-view translate + clip exactly like every other shape.
  `Window::drawText` is re-based on the same core so the S0.4/S0.6
  text acceptance (log lines + screendump) stays byte-identical.

## 2. Prerequisite gaps the slices must close

- **Text is window-level today.** `Window::drawText` (S0.4,
  `text.cpp`) fontconfig-matches, HarfBuzz-shapes and FreeType-rasterizes
  one run into an opaque box, then XPutImages it straight into the
  window. Widgets draw INSIDE the view-tree composite (per-view
  translate+clip into one pixman surface, one flush), so text must be
  drawable through `GraphicsContext`. S2.2a refactors `text.cpp` into
  a shared core (match→shape→coverage+advances) with two sinks:
  GC mask composite and the existing window blit.
- **No hover.** Motion is not selected or routed; Button's Hover state
  needs enter/exit/move. S2.2c adds `mouseMoved`/`mouseEntered`/
  `mouseExited` responder virtuals + `PointerMotionMask` +
  enter/exit tracking in the dispatch.
- **No focus.** Keys route to the content view and bubble up only.
  S2.2c adds a minimal first-responder model (Window tracks the
  focused view; keys route to it; focus ring drawn from the theme's
  Focused state). S3.1 extends, never replaces it.

## 3. API surface (Cocoa-resemblant; appended to the View/GC surface)

```cpp
/* Control (the NSControl analog) */
class Control : public View {
public:
    using Action = std::function<void(Control *)>;
    void setEnabled(bool);   bool isEnabled() const;
    void setAction(Action);                    /* replaces target/action */
    /* fires the action; buttons/fields override sendAction on clicks */
    void sendAction();
    /* current chrome state, recomputed on events */
    ControlState state() const;
    /* track whether the pointer is inside (enter/exit) */
protected:
    virtual void updateState();                /* recompute from flags */
    bool hovered_ = false, armed_ = false, focused_ = false;
};
```

`Label` — draws the theme font text in its bounds; a11y role `label`,
label = the text, optional `textColor`/`alignment`. Text is drawn
vertically centered on the frame's centerline (pt math, then px).

`Button` — `enum class Type { Push, Checkbox, Radio }`; draws
Push chrome (theme `state()` fill/outline/label + baseRadius + bevel)
or Checkbox/Radio markers; action fires on click (push) / state
toggle (check/radio). A11y role per type, `value` = "1"/"0".

`TextField` — single-line; `value()`/`setValue()`, caret
(visible at the insertion index, blink optional), click-to-position,
typing/backspace/delete, left/right arrows, Shift+arrow selection,
deletion of a selection. A11y role `text field`, value = the text,
enabled. (The S0.3 responder key events already carry translated
`chars` — printing chars type, others act.)

**Minimal focus model** — `View::becomeFirstResponder()`/
`acceptsFirstResponder()` (default true for Control, false for plain
views); `Window` tracks the first responder; key events route
focused-view-first then bubble the chain; clicking a Control makes it
first responder; focus ring drawn from theme `Focused` state.
Visible-focus-only paths draw the Focused chrome (no traversal yet).

**Radio grouping** — the catalog defers real grouping to Box (L6/S2.4).
Until Box exists, S2.2c implements the same-superview group: turning a
Radio on clears the other Radio siblings under the same superview.
Box later re-parents groups without changing Button.

## 4. Split (one observable acceptance per slice, committed separately)

### S2.2a — GC text draw + metrics

*Scope:* refactor `text.cpp` to a shared core; add
`GraphicsContext::drawText(family, sizePt, xPx, yPx, utf8, fg)` that
composites alpha glyphs into the current surface through the frame
translate + clip; add session text metrics (width in pt) for widget
layout; re-base `Window::drawText` on the core (S0.4 log-line parity:
`ARGENTUM-TEXT: matched … shaped … blitted N glyph(s) box WxH at x,y`).
*Acceptance (gate .build/s22a_*):* a probe draws a string through a
translated+clipped GC in a view tree; pixel probes assert glyph
pixels at the expected local px and that the text is clipped at the
view bounds; the S0.6-era window text still renders (Window::drawText
parity via the existing demo/text path).

### S2.2b — Control + Label

*Scope:* `Control` base (enabled/action/state machinery);
`Label` (theme text draw + a11y). Programmatic action firing.
*Acceptance:* board shows labels (theme font, distinct colors);
a11y read-back (role/label); a Control subclass fires its action when
`sendAction()` is called.

### S2.2c — Button + hover + minimal focus

*Scope:* `Button::Type { Push, Checkbox, Radio }` + chrome draw for
all 5 states; motion routing (`mouseMoved`/`mouseEntered`/
`mouseExited`, PointerMotionMask); the minimal focus model
(click-to-focus, key routing, focus ring); same-superview radio
grouping.
*Acceptance:* pixel probes on a push button in Idle/Hover/Armed
(hover via injected MotionNotify, armed via press), checkbox toggles,
radio exclusivity; action fired; a11y role/label/value; a focused
button shows the Focused chrome.

### S2.2d — TextField + edit engine + S2.2 gate board

*Scope:* the TextField edit engine; the interactive board
(`widgets_board`, staged System/Shared/tests) = the milestone's
reference-app slice: a label, a push button, a checkbox, a radio
pair, and a text field, wired so interactions log.
*Acceptance (gate .build/s22d_*, the S2.2 milestone gate):* injected
clicks + keys exercise the board: button action fires, checkbox and
radio toggle, the field accepts typed text with caret movement and
selection, values + a11y role/label/value read back; S1.3 / S2.1
regressions stay green.

## 5. Files

- `userland/argentum/text.cpp` — split into core (shape/coverage/
  metrics) + sinks (GC composite, window blit).
- `userland/argentum/graphics.cpp` + `argentum.h` — GC::drawText,
  text metrics, Control/Label/Button/TextField, responder additions
  (mouseMoved/Entered/Exited), focus API.
- `userland/argentum/control.cpp`, `label.cpp`, `button.cpp`,
  `textfield.cpp` (new), `view.cpp`/`window.cpp`/`application.cpp`
  (focus + motion dispatch).
- `userland/tests/s22[a-d]*.cpp` — probes; `.build/s22*_run.sh` +
  pixel/assert gates; `mk/20-userland.mk` staging (libargentum
  unchanged in name; widget sources join ARGENTUM_SRCS).
- `docs/design/argentum-uikit-plan.md` §7 L4 and
  `argentum-uikit-catalog.md` wording updated with the slices (rule 5).

## 6. Deferred (noted so they are decisions)

- **Focus traversal / keyboard equivalents** — S3.1 (focus model is in
  place; traversal = Tab order + mnemonics on top).
- **Edit depth** — S3.2: secure field, IME/unicode input polish,
  copy/paste, drag selection is optional-in-S2.2d-if-cheap else S3.2.
- **Radio groups via Box** — S2.4 (same-superview grouping until then).
- **SearchField / SecureTextField** — S3.2 (styled TextField
  subclasses).
- **Per-rect damage / blinking caret timer** — cosmetic later.
