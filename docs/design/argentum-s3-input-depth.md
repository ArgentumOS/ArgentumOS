# Argentum S3 — Input & text depth

Status: **DRAFT (2026-09).** Design + split for S3 of
`docs/design/argentum-milestone-split.md` ("input & text depth").
S2 delivered the whole v1 widget catalog on one board (S2.5); S3 makes
that board **operable without a mouse**. Read alongside the S2.2d text
edit engine (caret/selection/typing) and the S2.2c minimal focus model
(click-to-focus, focused chrome, Space/Return activation on Buttons).
Same working pattern: design doc first, one observable slice at a time.

## 1. Scope

1. **S3.1 — focus traversal + keyboard equivalents.** Tab / Shift-Tab
   walks focus across the window's focusable controls in document
   order (wrapping); every v1-cut *interactive* widget implements its
   keyboard equivalent when focused (Space/Return activate; arrows
   adjust Slider/Stepper/SegmentedControl; the focused chrome is the
   existing ControlState::Focused accent ring). This makes the S2.5
   zoo board navigable AND operable by keyboard alone.
2. **S3.2 — edit-widget depth.** TextField gains what the "fully
   operable without a mouse (including typing in edit fields)"
   acceptance needs beyond S2.2d: Tab moves INTO the field (S3.1),
   typing/selection already work, so S3.2 adds a **secure entry**
   mode (bullets, no clipboard echo) + Return ends editing with a
   commit (valueChanged on an explicit end-edit), and the caret
   stays visible while the field has focus.

## 2. New machinery each group introduces

- **Traversal (S3.1):** a document-order walk of the content tree
  (depth-first pre-order, hidden views skipped, only views with
  `acceptsFirstResponder()` — i.e. enabled Controls). The WINDOW owns
  traversal: `dispatchKeyToContent` intercepts Tab/Shift-Tab before
  the responder chain, moves the first responder ±1 (wrap), and
  logs a `FOCUS-TAB` line for the gate. No per-widget code; the
  existing become/resign + focused chrome do the rest.
- **Equivalents (S3.1):** each interactive leaf implements
  `keyDown` equivalents when focused: Slider `←/→` step the value,
  Stepper `←/→` step, SegmentedControl `←/→` move the selection,
  Space/Return activate (Button/Checkbox/Radio/PopUpButton already do
  via Control::keyDown — the doc generalizes the convention).
- **Secure entry (S3.2):** a `setSecure(bool)` mode on TextField
  (bullets replace glyphs in the buffer AND the drawn caret/selection
  math stays byte-based). Return commits: the toolkit fires
  valueChanged on Return (end-edit) exactly once per editing session.

## 3. API surface

```cpp
class Window {				/* already public */
  /* S3.1: move first responder by +/-1 over the focusable controls
   * (document order, wrap). Returns the new responder (null = none
   * focusable). */
  View *moveFocus(int direction);	/* +1 Tab, -1 Shift-Tab */
  std::vector<View *> focusables();	/* for the a11y layer later */
};
class Control {				/* already public */
  /* S3.1: keyboard equivalent helpers for the leaves */
  bool handleEquivalents(const KeyEvent &e); /* arrows/space/return */
};
class TextField {
  /* S3.2 */
  void setSecure(bool secure);		/* bullets when true */
  bool isSecure() const;
};
```

The leaves wire equivalents in their own `keyDown` overrides
(Slider/Stepper/SegmentedControl gain one); TextField's keyDown
already handles editing — Return now commits + resigns? (Return ends
editing; Tab moves focus per S3.1 — both visible behaviours).

## 4. Split (one observable acceptance per slice)

### S3.1a — Tab traversal + FOCUS-TAB
*Acceptance:* the zoo board (or a compact board) gets Tab/Shift-Tab
focus movement across its enabled controls in document order: the
focused chrome moves (pixel probe on the accent ring region), and the
window logs the movement (`FOCUS-TAB: k <role> '<label>'`). Disabled
controls are skipped; the wrap is observable (Tab past the last
returns to the first).
Status: **DONE** — `Window::focusables()`/`moveFocus()` in
`window.cpp`: a document-order (depth-first pre-order) walk of the
content tree; `dispatchKeyToContent` intercepts Tab (keysym 0xff09)
before the responder chain and moves the first responder +/-1 (wrap;
Tab from no-focus lands on the first, Shift-Tab on the last), logging
`FOCUS-TAB: k role=… label=…`. `Control::acceptsFirstResponder` is now
enabled-gated so disabled controls are skipped. Probe `structure_f`
(self-injected Tab x4 + Shift-Tab, widgets_d pattern), gate
`.build/s31a_run.sh` + `s31a_assert.py` -> S31A-OK (5 checks:
0,1,2,3 then back to 2; disabled button skipped; the focused Two's
ring is the accent colour while One's is not).

### S3.1b — keyboard equivalents on the leaves
*Acceptance:* with a Slider focused, `←`/`→` move the knob by a step
(value + ZOO-ACT-style log); Stepper `←`/`→` step; SegmentedControl
`←`/`→` moves the selection and fires the action; Space on the
focused Checkbox toggles it (keyboard-only interaction, no mouse).
Status: **DONE** — Slider/SegmentedControl already had arrow keyDowns
(S2.3-era); Stepper gained one (`stepper.cpp`: Up/Right increment,
Down/Left decrement + sendAction). Probe `structure_g` self-injects
Tab then arrows/Space across Slider/Stepper/SegmentedControl/Checkbox
(no mouse); gate `.build/s31b_run.sh` + `s31b_assert.py` -> S31B-OK
(11 checks: 0.25 -> 0.45 by four Rights, 3 -> 5 by two Rights, segment
0 -> 1 with its action, Space checks the box; pixels confirm the
0.45 knob, the B segment fill and the checked box).

### S3.2 — secure field + commit + whole-S3 keyboard acceptance
*Acceptance:* the TextField in secure mode draws bullets instead of
glyphs (pixel region no longer matches normal glyphs); Return commits
once (single `valueChanged`, logged); and the ORIGINAL S3 acceptance
runs on the zoo: a keyboard-only script (Tab to each control, arrows/
Space to operate, type into the field) exercises the whole board with
no mouse — the S2.5 battery of ZOO-ACT/interaction logs fires.

## 5. Files

- `userland/argentum/window.cpp` (traversal + `focusables`/`moveFocus`),
  `control.cpp`/leaf `keyDown`s (slider.cpp/stepper.cpp/segmented.cpp),
  `textfield.cpp` (`setSecure`, Return-commit).
- Probe/board: extend `widget_zoo` (the S2.5 reference app) with the
  traversal logs + a secure field; gates `.build/s31{a,b}_run.sh` +
  `.build/s32_run.sh` with key injection (XSendEvent KeyPress — the
  S22D path — or the monitor's real keyboard).
- Markup: `docs/design/argentum-milestone-split.md` S3 bullets.

## 6. Deferred (noted so they are decisions)

- **Menus & mnemonics**: the global menubar + menu-key equivalents
  land with S4 (Kestrel + menu IPC); menu mnemonics deferred with
  them.
- **Full a11y keyboard protocol**: focusables() feeds a future a11y
  socket (S5+) rather than a dedicated public API now.
- **Undo / rich text / IME**: out of scope; secure mode is a display
  concern only in v1 (no clipboard paste exists to guard).
- **TextView**: the doc's "shared with TextView later" stays later —
  TextField remains the only text editor.
