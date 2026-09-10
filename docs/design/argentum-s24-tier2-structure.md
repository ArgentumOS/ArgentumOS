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
2. **ScrollView** — a clipping wrapper with CLASSIC, always-visible
   EXTERNAL scrollbars: a viewport view clips the document (translated
   by the scroll offset) and a `ScrollBar` sits in the right gutter,
   another in the bottom gutter, with the corner between them. The
   gutter is OUTSIDE the content, so the document never paints under a
   bar. Scrolling is interactive (arrows step a line, the track pages,
   the proportional scroller drags) and responds to the wheel; the
   programmatic entry points (`scrollTo`/`scrollBy`/
   `scrollRectToVisible`) remain.
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
class ScrollBar : public View {      // argentum/scrollbar.cpp
  enum class Orientation { Vertical, Horizontal };
  ScrollBar(Orientation = Orientation::Vertical);
  void setRange(double range, double page); // page/range = scroller size
  void setValue(double);  double value() const;
  void setLineStep(double pt);              // one arrow click
  void setAction(std::function<void(double)>); // requested offset (pt)
  double thickness() const;                 // cross size (gutter)
};
class ScrollView : public View {    // argentum/scroll.cpp
  void setDocumentView(View *);     // non-owning, may exceed bounds
  void scrollTo(double xPt, double yPt); // clamp: 0..content-viewport
  void scrollBy(double dxPt, double dyPt);
  void scrollRectToVisible(const Rect &);
  double contentOffsetX() const; double contentOffsetY() const;
  Size contentSize() const;         // frame minus the bar gutter
  ScrollBar *verticalScrollBar() const;
  ScrollBar *horizontalScrollBar() const;
  bool mouseWheel(const MouseEvent &) override; // buttons 4-7
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

### S2.4b — ScrollView: clip + classic EXTERNAL scrollbars
*Acceptance:* a tall banded document inside a ScrollView. Scrolling is
driven through real input and the offsets are asserted exactly: one
wheel notch = 3 lines, one arrow click = one line step, one track click
= one page, a scroller drag moves proportionally. Structurally the
gutter is OUTSIDE the content - pixels in the bar strip are bar chrome
and never document, and the content just inside the viewport is
document - and the scroller's travel matches the offset
(proportionality); the scroller is the same grey chrome as the arrow
buttons, with the darkened trough giving the contrast.
Status: **DONE (revised)** — `userland/argentum/scroll.cpp` +
`userland/argentum/scrollbar.cpp` (decls in `argentum.h`, Impls in
`argentum_p.h`). The document is a subview of an internal viewport view
clipped to the content rect (the tree clips a view's children to its
bounds), so no overlay is needed and the document cannot paint under a
bar; the old inset `ScrollChrome` overlay is gone. The bars are
`ScrollBar` controls - rounded rects in flat chrome with 1px line art,
arrow buttons that grey out at the ends, a muted track, and a
PROPORTIONAL scroller (its length is page/range of the track) filled
in the same grey as the arrow buttons, over a trough darkened from the
chrome (the theme's Disabled tone sits only ~7 levels off it and does not
read as a recess); the enabled arrows' triangles carry the accent so the
bar still reads as one control, and the tips point at the ends they
scroll toward. Boundaries are single hairlines rather than stacks
(three stacked dark rings read as a thick black band): the trough draws
no ring of its own - and it is a plain RECTANGLE, since it butts
straight against the arrow buttons and rounded corners there would
leave notches at four interior junctions - while the scroller spans the
gutter's full width like the arrow buttons, so its ring coincides with
the bar's. The scrolled
view as a whole carries the house frame - `chromeOutline` at
`smallRadius`, exactly as `Box::draw` draws one - as a 1px ring painted
LAST (a hit-transparent `ScrollFrame`, the one overlay in the subtree):
on the gutter sides its pixels fall on the bars' outer edges and
coincide with them, and it supplies the border the content side (top
and left) was missing. They call back into
`scrollTo`, so every request is clamped in one place and pushed back
with `setValue` (the scroller can never drift from the content). One
wheel notch scrolls three lines through `View::mouseWheel`, which
BUBBLES up the responder chain; the window dispatch routes X buttons
4-7 there and never as a press, so scrolling over a button cannot arm
and click it. `ScrollView::contentSize()` reports the clip size, for a
document that should not overflow the gutter (the zoo's table sizes to
it). Clamp fix kept in `View::setNeedsDisplay` (`view.cpp`): a scrolled
view partially above/left of the window must damage only the visible
rect. Gates: `.build/s24b_run.sh` + `s24b_assert.py` -> S24B-OK
(programmatic scroll + `gutter-never-document`) and
`.build/s24sb_run.sh` + `s24sb_drive.py` + `s24sb_assert.py` ->
S24SBAR-OK (15 checks: wheel/arrow/page/drag/horizontal-arrow offsets,
the external-gutter structure, scroller proportionality). The wheel is
driven end to end from QMP `input-send-event` (wheel-up/down -> QEMU
HID -> USB -> xHCI -> mousedev -> Xfb buttons 4-7 -> the toolkit) and
the pointer from the QEMU monitor on the USB mouse. Note: QEMU's
`usb-mouse` HID has no horizontal pan (`usb-mouse.c` passes hwheel 0),
so the tilt path (buttons 6/7) is plumbed but not drivable headlessly. Generic synthetic-click tool
`userland/tests/xclick.c` (motion + press + release, since Buttons
fire only while hovered). Regressions S24A-OK, S13-PIXELS-OK,
S22D-OK.

### S2.4c — SplitView: divider drag resizes panes
*Acceptance:* two/three coloured panes with a divider; an injected
press-drag-release on the divider band changes the pane widths
(probes at the old boundary flip colour; min-size clamp holds when the
drag exceeds the limit).
Status: **DONE** — `userland/argentum/split.cpp` (decl in
`argentum.h`, Impl in `argentum_p.h`). Children ARE the panes; the
SplitView owns their frames (equal split; divider drag resizes the two
adjacent panes). Gutters are the SplitView's own area, so a press on
a divider hits it directly and presses just outside bubble up from the
pane edge (View::mouseDown forwarding widens the grab); the armed drag
uses the S2.3a pressed-view motion delivery. Gate `.build/s24c_run.sh`
+ `s24c_assert.py` -> S24C-OK (9 checks: layout p0=108, drag to
p0=168, far-left drag clamped at p0=40, page->red->green at the old
boundary, blue pane stable). The drag is injected with the QEMU
monitor's REAL mouse (XSendEvent-based tools hang at XSync once the
server is under interactive load — observed, not root-caused);
`xclick.c` additionally gained an optional [dragX dragY] mode.
Regressions S24A-OK, S24B-OK.

### S2.4d — TabView: tab strip + page switching
*Acceptance:* three items with distinct page content; injected clicks
on tabs 2 and 3 switch the visible page (pixel probe of the content
area changes), the selected tab draws armed chrome, and a11y read-back
reports the TabGroup with the selected item.
Status: **DONE** — `userland/argentum/tab.cpp` (TabViewItem + TabView,
decl in `argentum.h`, Impls in `argentum_p.h`). Items borrow a title +
a page view; the pages are subviews and only the selected one is
visible below the strip (measured from the theme font). Strip clicks
select; the strip is drawn the way NSTabView draws it - a segmented
control, not folder tabs attached to the body (tabs that open into the
page below read as a row of buttons, which is the wrong idiom). The
control is drawn the way `SegmentedControl` draws itself
(`segmented.cpp`), so that a tab strip and a segmented control are
visibly the same control rather than two similar ones: ONE
chromeOutline bezel, each segment a rounded gradient inset from its
NEIGHBOUR by the bezel inset - so a separator has room between two
segments, as in `segmented.cpp` - but flush with the bezel at the two
outer ends: insetting those as well makes the end borders twice as
thick as a segmented control's, because the bezel already stands off
the first and last cell. The SELECTED segment is in the `Armed` state and the rest in `Idle`, which
is exactly a segmented control's selected and unselected colours - and
1px page-coloured lines between neighbouring segments. The bezel inset
is 1 PIXEL rather than a point measure, matching `segmented.cpp`'s `o`:
a scaled inset would make the strip a subtly different control from the
one beside it. Segment rects are adjacent and labels are centred within
them. The control is centred across the view too, and the
content's top border is drawn on the control's MIDDLE row, so the line
straddles it and re-emerges either side - the border row is taken from
the control's own rounded pixel geometry, not from parallel point maths
that could round a pixel off it. The control's top inset is ZERO and
the box is drawn with NO top border of its own: a border at the view's
own top row would be a second line above the control. Instead the
border's top edge IS the line the control is centred on, and the left
and right borders stop at that line and turn into it, so the three
meet in corners. The box is drawn BEFORE the control, so the control's
track covers the border where it crosses and the line emerges either
side - half the control sits above the box's edge, inside the view, so
nothing is clipped, and the control reads as centred on the border.
The pages begin at the control's bottom edge (they are
subviews and paint after this view, so they have to start clear of it),
and fill the pane they are given: how far in the CONTENT sits is a UI
decision, taken when the UI is built - a Box, or a frame re-applied in
the view's own draw - not something the control imposes. The pane
behind it is the chrome tone, darker than the page, so the margin
between the pane's border and the content reads as a margin instead of
as more content; a board that wants the house 5pt sets 5pt (the
structure_d board does). The panel keeps
its own rounded hairline frame, drawn
last so it sits on the strip's fill. The band's height is the segment
height plus the bezel inset and the top margin, with no gap below the
control, all measured in PIXELS (the rule used `lround()` of the point
value, so under the 2x scale it cut through the middle of the tabs).
Its vertical padding is deliberately TIGHT - 1pt a side, about three
quarters of a button's height - because a segmented control is shorter
than a button and does not need a button's breathing room; since the
border row derives from the control's own height, shortening the
control moves the line with it and the two stay centred on each other.
`setOnSelect` optional callback. A11y TabGroup
with the label = selected title, value = index (read back per
selection as TAB-A11Y). Gate `.build/s24d_run.sh` + `s24d_assert.py`
-> S24D-OK (9 checks: red->green content flip, tab chrome flips,
a11y read-back role=tab group label=Sounds value=1). The tab click
uses the QEMU monitor's real mouse (the xclick/XSync hang persists for
interactive servers). Regressions S24A-OK, S24C-OK.

### S2.4e — TableView-basic: header + rows + selection
*Acceptance:* a data source of N rows x M cols renders the header and
rows inside a ScrollView (probes on a header cell and a body cell);
injected click on row k selects it (selected-row chrome probe) and the
delegate logs `TABLE-SELECT: k`. Empty/one-row data sources do not
crash.
Status: **DONE** — `userland/argentum/table.cpp` (decl in
`argentum.h`, Impl in `argentum_p.h`). The table draws a chrome header
row (column titles) + the data rows from a
`TableViewDataSource`/`TableViewDelegate` protocol at a theme-derived
row height; even rows page-coloured, odd rows zebra, the selected row
an accent-tinted fill; row clicks select + fire the delegate; cell
text draws with a per-cell clip. Designed as a ScrollView document
(whole table scrolls in v1; header not pinned). A11y Table. Gate
`.build/s24e_run.sh` + `s24e_assert.py` -> S24E-OK (8 checks:
role=table, row click 2 -> S24E-SELECT + TABLE-SELECT: 2, header
chrome + glyphs, zebra, page -> selection-tint flip). Regressions
S24A-OK, S24D-OK. **S2.4 (Tier 2 structure, L6) is therefore
complete: S2.4a-e all green and committed.**

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

- **Scroll input**: DONE after all — the native mouse device carries
  the wheel in its record, Xfb maps notches to X buttons 4-7, and the
  window dispatch routes them to `View::mouseWheel` (which bubbles), so
  the wheel scrolls a view and the scroller drags. See the S2.4b
  section; nothing is deferred here any more.
- **TabView keyboard navigation / mnemonics** — minimal-focus traversal
  is S3.1 (unchanged).
- **TableView richness**: column resizing, sorting, editing, multiple
  selection, cell views — staged after S2 (catalog Tier 3).
- **Box autolayout semantics beyond packing**: equal-size children,
  hugging/compression resistance — not needed by the v1 boards.
