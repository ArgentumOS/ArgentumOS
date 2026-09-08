# Argentum S2.3 — rest of Tier 1 (plan L5)

Status: **DRAFT (2026-09).** Design + split for S2.3 of
`docs/design/argentum-milestone-split.md` ("rest of Tier 1", plan-§469
L5). Read alongside `argentum-uikit-plan.md` §7 L5 + the class-hierarchy
order, `argentum-uikit-catalog.md` Tier 1, and the S2.2 doc
(`argentum-s22-control-first-leaves.md`) whose widget stack (Control,
Button, TextField, Label, GC text, pointer tracking, minimal focus) the
new views extend. Mirrors the S2.2 working pattern: design doc first,
then small observable slices, each green before the next, committed
separately.

## 1. Scope (L5, grouped by new machinery)

1. **Menu / MenuItem model** (NOT views — Cocoa's NSMenu model):
   title, enabled, an action, submenu items; the *wire* (config-framed
   menu tree for the session socket) stays later (Kestrel); S2.3 builds
   the in-process model + the PopUpButton that presents it.
2. **Slider / Stepper** (drag + value): Slider = track + knob,
   min/max/value, click + DRAG sets the value (needs drag delivery),
   action fires on release; Stepper = up/down click zones with an
   increment, action per click.
3. **ProgressIndicator** (input-free View, bar style): determinate
   value 0..1, theme chrome. (Spinning/indeterminate + animation are
   deferred — no timer machinery until a consumer needs it.)
4. **SegmentedControl** (Control): N titled segments, a selected
   index, per-segment chrome states, action fires with the index.
5. **ImageView** (input-free View + content policy): draws a
   view-supplied image (a BitmapImage the app paints) into its frame
   with a content mode (center / scale-to-fit / stretch) — the first
   image-composite primitive in the GC.
6. **LevelIndicator** (input-free View): a capacity gauge
   (continuous/discrete display) — display-only, no interaction.

## 2. New machinery each group introduces

- **Drag delivery (Slider/Stepper, and needed by every drag control):**
  while a ButtonPress target is armed, pointer motion goes to the
  PRESSED view (not the hit-test target), so a Slider can track the
  knob across the window. The window already routes release to the
  pressed view (S2.2c). Add `mouseMoved` delivery to `pressed` during
  a press (enter/exit tracking suspended while dragging).
- **Menu presentation (PopUpButton):** a transient popup window that
  lists the Menu's items, tracks hover, closes on click/outside; item
  click fires the item action. The popup is a plain X11 window owned
  by the session (no WM) positioned under the PopUpButton.
- **Image compositing (ImageView):** `GraphicsContext::drawImage`
  composites a source BitmapImage (pixman x8r8g8b8) into the current
  surface at a destination rect with a content mode — pixman
  bilinear scaling for scale-to-fit/stretch, 1:1 at the source origin
  for center.

## 3. API surface (appended to the widget stack)

```cpp
/* Menu model (NOT a view) */
class Menu;
class MenuItem {
public:
    MenuItem(const char *title);
    void setTitle(const char *);   const char *title() const;
    void setEnabled(bool);         bool isEnabled() const;
    void setAction(std::function<void()>);
    void setSubmenu(Menu *);       Menu *submenu() const;
};

/* Slider (Control): value in [minValue, maxValue]; drag + click set */
class Slider : public Control {
public:
    void setRange(double minV, double maxV);
    double value() const;   void setValue(double);
    /* knob position for drawing/measurement */
    double knobFraction() const;
    void draw(GraphicsContext&) override;
    void mouseDown(const MouseEvent&) override;   /* jump + grab */
    void mouseMoved(const MouseEvent&) override;  /* drag */
    void mouseUp(const MouseEvent&) override;     /* fire action */
};

/* Stepper (Control): + / - zones, increment */
class Stepper : public Control {
public:
    void setIncrement(double);  double value() const; void setValue(double);
    void mouseDown(const MouseEvent&) override;      /* detect zone */
    void mouseUp(const MouseEvent&) override;        /* fire */
};

/* SegmentedControl (Control) */
class SegmentedControl : public Control {
public:
    void setSegments(const char *const *titles, int n);  /* copied */
    int segmentCount() const;
    void setSelectedIndex(int);  int selectedIndex() const;
    void draw(...) override;
    void mouseDown(...) override;  /* select + arm */
    void mouseUp(...) override;    /* fire action(segment) */
};

/* input-free displays */
class ProgressIndicator : public View {   /* bar; determinate */
public:  void setProgress(double 0..1);  double progress() const; draw...;
};
class LevelIndicator : public View {      /* capacity gauge */
public:  void setLevel(double 0..1);  double level() const; draw...;
};
class ImageView : public View {
public:
    enum class ContentMode { Center, ScaleToFit, Stretch };
    void setImage(BitmapImage *img);   /* borrowed, may be null */
    void setContentMode(ContentMode);  draw...;
};

/* PopUpButton (Control): a button that presents its Menu */
class PopUpButton : public Control {
public:
    void setMenu(Menu *menu);          /* borrowed */
    void setTitleFromSelectedItem();
    void mouseDown(...) override;      /* open the popup */
};
```

A11y: Slider/Stepper/SegmentedControl/PopUpButton carry their roles +
a value ("0.5") where meaningful; ProgressIndicator/LevelIndicator/
ImageView are plain Views with roles `progress indicator` / `level
indicator` / `image`.

## 4. Split (one observable acceptance per slice)

### S2.3a — drag + Slider + Stepper + Menu model

*Scope:* drag delivery (motion to the pressed view); `Slider`
(click/drag sets the value); `Stepper` (+/− zones); the `Menu`/
`MenuItem` model.
*Acceptance:* a board with a Slider + a Stepper: injected drag moves
the slider to a fraction and its value reads back; stepper clicks
step the value; actions fire; a11y role/value read-back; Menu built
in-process with items + a submenu, read back (titles/enabled), no
popup yet (that is S2.3c's PopUpButton).

### S2.3b — SegmentedControl + ProgressIndicator + LevelIndicator

*Scope:* the two interactive/display controls: SegmentedControl (N
titles, selected index, action-with-index), ProgressIndicator
(determinate bar), LevelIndicator (capacity gauge).
*Acceptance:* injected clicks change the segmented selection and fire
its action; progress/level set values are drawn and pixel-probed;
a11y role/value read-back.

### S2.3c — ImageView + PopUpButton

*Scope:* `GC::drawImage` (image composite with content modes) +
ImageView; PopUpButton that presents a Menu in a transient popup
window (hover highlight, click fires, closes on outside click).
*Acceptance:* a painted image is drawn through the ImageView at each
content mode (pixel probes); clicking the PopUpButton opens the menu,
hover + item click fires the item action; a11y read-back.

## 5. Files

- `userland/argentum/slider.cpp`, `stepper.cpp`,
  `segmented.cpp`, `progress.cpp`, `level.cpp`, `imageview.cpp`,
  `popup.cpp`, `menu.cpp` (new) + `argentum.h` decls +
  `argentum_p.h` Impls; `graphics.cpp` (drawImage + drag helper in
  window.cpp); `mk/00-base.mk` ARGENTUM_SRCS.
- probes `userland/tests/widgets_e.cpp` (S2.3a), `widgets_f.cpp`
  (S2.3b), `widgets_g.cpp` (S2.3c) staged System/Shared/tests via
  `mk/20-userland.mk`; `.build/s23*_run.sh` + asserts. The boards are
  the germ of the S2.5 Settings reference app.
- `docs/design/argentum-uikit-plan.md` §7 L5 + catalog wording updated
  per slice (rule 5).

## 6. Deferred (noted so they are decisions)

- **Menu wire / session socket + global menubar** — Kestrel (the
  .conf-framed wire format is decided; S2.3 is the in-process model +
  PopUpButton presentation).
- **ProgressIndicator spinning / indeterminate + animation** — no
  timer machinery until a consumer needs it.
- **PopUpButton keyboard interaction / menu navigation keys** — S3.1
  traversal; S2.3c menu is mouse-driven.
- **ImageView image-file decoding (PNG/JPEG)** — a later media
  milestone; S2.3c images are app-painted BitmapImages.
- **ColorWell / SearchField / SecureTextField** — SearchField +
  SecureTextField are TextField subclasses (S3.2); ColorWell minimal
  in a later pass.