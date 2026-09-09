# Argentum S2.4 — Tier 2 structure (plan L6)

Status: **DRAFT (2026-09).** Design + split for S2.4 of
`docs/design/argentum-milestone-split.md` ("Tier 2 structure", plan L6)
per `argentum-uikit-plan.md` §7 + `argentum-uikit-catalog.md` Tier 2.
Read alongside the S2.3 doc (`argentum-s23-tier1-rest.md`) whose stack
(Control/Button/TextField/Label, GC text, pointer tracking, drag
delivery, minimal focus, transient popups) these containers build on.
Same working pattern: design doc first, then one observable slice at a
time, each green before the next, committed separately.

## 1. Scope (L6, grouped by new machinery)

1. **Box** — a titled group view + the row/column arrangement role
   (NSBox-lite / NSStackView-lite): chrome panel (optional title cap),
   and a layout mode that packs subviews along an axis with a spacing.
   This is the only *arrangement* widget needed; boxes nest like any
   view and coexist with per-view springs/struts (S2.1d) for boards
   that place frames by hand (`layout = none`).
2. **ScrollView** — a clipping wrapper: a content view larger than the
   viewport, translated by a scroll offset, with drawn scrollbar
   chrome (thumb tracks the offset). v1 scroll is PROGRAMMATIC
   (`scrollTo`/`scrollBy`) — no wheel/touch/scroll-event machinery
   exists yet; scrollbars are indicators, not drag targets.
3. **SplitView** — N panes along an axis separated by dividers; the
   divider is draggable (drag delivery from S2.3a) and resizes the
   neighbouring panes between minimum sizes.
4. **TabView** — a tab strip + a content area; each item has a title
   and a page view; clicking a tab makes its page visible (a plain
   View with an internal hit-test — no Control base needed).
5. **TableView-basic** — the first data view, living inside a
   ScrollView: a column header (titles from the model), rows drawn
   from a data source (`rowCount`, `cellText(row, col)`), single-row
   selection by click with a delegate callback.

## 2. New machinery each group introduces

- **Layout arrangement (Box):** an explicit packing pass that rewrites
  child frames in pt along the box axis. Runs at the top of the Box's
  own draw (parent draws before the child recursion, so children
  render at their arranged frames). No new event machinery; changing a
  child or the spacing marks the box for redisplay.
- **Scrolling (ScrollView):** the view tree already clips each view to
  its bounds (S2.1a) and translates children, so scrolling is a
  content subview translated by `-offset`; the ScrollView draws its
  frame chrome + thumb(s) on top. No GC changes.
- **Divider drag (SplitView):** the S2.3a drag delivery (motion goes
  to the pressed view) carries the divider; the SplitView arms itself
  when a press lands within a divider band, translates the divider
  with motion, clamps by the minimum pane sizes, and redraws on
  release. Frames of the panes are rewritten in pt.
- **Data model (TableView):** C++ virtual protocol on a
  `TableViewDataSource` (row count + cell text) and a
  `TableViewDelegate` (selection callback) — plain virtuals, no ObjC
  runtime. Row/column geometry derives from the model + theme fonts.

## 3. API surface (appended to the widget stack)

```cpp
enum class BoxLayout { None, Row, Column };
class Box : public View {           // argentum/box.cpp
  void setTitle(const char *);      // "" hides the title cap
  void setLayout(BoxLayout);        // None keeps manual child frames
  void setSpacing(double pt);
};
class ScrollView : public View {    // argentum/scroll.cpp
  void setDocumentView(View *);     // non-owning, may exceed bounds
  void scrollTo(double xPt, double yPt); // clamp: 0..content-frame
  void scrollBy(double dxPt, double dyPt);
  double contentOffsetX() const; double contentOffsetY() const;
};
class SplitView : public View {     // argentum/split.cpp
  void setVertical(bool);           // divider axis
  void setDividerThickness(double pt);
  void setMinPane(double pt);       // applied to every pane
};
class TabViewItem {                 // argentum/tab.cpp
  TabViewItem(const char *title, View *page);
  const char *title() const; View *page() const;
};
class TabView : public View {       // TabGroup a11y role
  void addItem(TabViewItem *);      // non-owning, like Menu items
  void selectItem(int);             // + on tab click
  int selectedIndex() const;
};
class TableViewDataSource {         // argentum/table.cpp
  virtual int rowCount() const = 0;
  virtual const char *cellText(int row, int col) const = 0;
};
class TableViewDelegate {
  virtual void tableSelectionDidChange(TableView *, int row) {}
};
class TableView : public View {     // Table a11y role; sits in a ScrollView
  TableView(TableViewDataSource *ds, TableViewDelegate *del = nullptr);
  void setColumns(const char *const *titles, int count);
  void selectRow(int);              // + on row click
  int selectedRow() const;
};
```

A11y roles: Box->Group (title mirrors the title), ScrollView->ScrollArea,
SplitView->Splitter, TabView->TabGroup, TableView->Table (rows read back
as row/col cells). Role enum already carries all of these (S2.1).

## 4. Split (one observable acceptance per slice)

### S2.4a — Box: title chrome + row/column arrangement
*Acceptance:* a board builds a titled Column Box (chrome panel + title
cap) containing three buttons and a nested Row Box; the Box rewrites
the children's frames (no manual frames), so the buttons sit at the
packed positions and the nested row is centred inside its parent cell.
Pixel probes hit the box chrome border + the title text at the cap,
and the buttons at their ARRANGED (not author-given) origins.
Status: **DONE** — `userland/argentum/box.cpp` (BoxLayout enum in
`argentum.h`; Box::Impl in `argentum_p.h`). Box draw paints the chrome
panel (outline + page body + gradient cap + centred title) and runs
the arrangement pass at the top of draw (children pack along the axis
at their own sizes with `spacing`, cross-axis centred inside the
content area below the cap; untitled Boxes are invisible arrangement
containers). `Box::capPx()` derives the cap from the theme title
metrics. One-time BOX-A logs carry the arranged frames for the gate
(probe `structure_a`, gate `.build/s24a_run.sh` + `s24a_assert.py` ->
S24A-OK: 15 checks incl. the packed pitch 36 pt, nested row centring,
cap glyphs, border, buttons at arranged origins).

### S2.4b — ScrollView: clip + programmatic scroll + thumb
*Acceptance:* a tall content view (a marker grid) inside a ScrollView;
`scrollBy` steps change which grid band is visible (pixel probes at a
fixed viewport point return different grid colours per offset) and the
scrollbar thumb moves with the offset (thumb-band pixel differs before
/ after). Content outside the viewport never draws (probe above the
content top = box page colour, not content).
Status: **DONE** — `userland/argentum/scroll.cpp` (decl in
`argentum.h`, Impl in `argentum_p.h`). The document view is a subview
whose frame origin carries the offset (the tree clips it); a
non-interactive ScrollChrome overlay (added after the document,
`hitTest -> nullptr`) paints the 1px border ring + the thumb indicator
ON TOP, since the tree paints parents under their children. Clamp fix
in `View::setNeedsDisplay` (`view.cpp`): a scrolled view partially
above/left of the window must damage only the visible rect — the raw
negative window-px rect reached the damage machinery and corrupted
memory (page fault). Gate `.build/s24b_run.sh` + `s24b_assert.py` ->
S24B-OK (8 checks: offsets 60..240, band0->band3 flip, thumb top->
bottom, doc showing through the gutter). Generic synthetic-click tool
`userland/tests/xclick.c` (motion + press + release, since Buttons
fire only while hovered). Regressions S24A-OK, S13-PIXELS-OK,
S22D-OK.

### S2.4c — SplitView: divider drag resizes panes
*Acceptance:* two/three coloured panes with a divider; an injected
press-drag-release on the divider band changes the pane widths
(probes at the old boundary flip colour; min-size clamp holds when the
drag exceeds the limit).

### S2.4d — TabView: tab strip + page switching
*Acceptance:* three items with distinct page content; injected clicks
on tabs 2 and 3 switch the visible page (pixel probe of the content
area changes), the selected tab draws armed chrome, and a11y read-back
reports the TabGroup with the selected item.

### S2.4e — TableView-basic: header + rows + selection
*Acceptance:* a data source of N rows x M cols renders the header and
rows inside a ScrollView (probes on a header cell and a body cell);
injected click on row k selects it (selected-row chrome probe) and the
delegate logs `TABLE-SELECT: k`. Empty/one-row data sources do not
crash.

## 5. Files

- `userland/argentum/{box,scroll,split,tab,table}.cpp` + decls in
  `argentum.h`, Impl structs in `argentum_p.h`; `ARGENTUM_SRCS` in
  `mk/00-base.mk`.
- Probes `userland/tests/structure_{a..e}.cpp` staged by
  `mk/20-userland.mk`; gates `.build/s24{a..e}_run.sh` + asserts
  (screendump pixel probes + console markers), mirroring S2.2/S2.3.
- Board evolution: the interim `make zoo` board gains one S2.4 control
  per slice once green (S2.5's reference-app germ keeps growing).

## 6. Deferred (noted so they are decisions)

- **Scroll input**: no mouse wheel exists in the input stack (psaux is
  3-byte; the serial seam is pointer-only); wheel scrolling + scroll
  event machinery land with a real wheel source. Scrollbar dragging is
  likewise deferred (indicators only).
- **TabView keyboard navigation / mnemonics** — minimal-focus traversal
  is S3.1 (unchanged).
- **TableView richness**: column resizing, sorting, editing, multiple
  selection, cell views — staged after S2 (catalog Tier 3).
- **Box autolayout semantics beyond packing**: equal-size children,
  hugging/compression resistance — not needed by the v1 boards.
