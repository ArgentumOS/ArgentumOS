# Argentum catalog — Snow Leopard-parallel view inventory

Status: **DECIDED (2026-09).** The Argentum view catalog *target* parallels
what AppKit offered circa macOS 10.6 (Snow Leopard) — in available
classes and functionality, **not visual style** (FNX look is Argentum's
own: pixman vector chrome, real-point units, `.conf` themes). Supersedes
`docs/archive/momo-v1-widgets.md` (the minimal GTK-era v1 catalog) as
the catalog reference. Naming follows the Cocoa-resemblant principle
(docs/design/argentum-uikit-plan.md §2): class roles and semantics mirror
AppKit; names are C++-idiomatic.

Everything is a `argentum::View` (single view tree, §4 of the plan). The
SL-era NSCell split is an *implementation detail we do not copy* — no
cell model in v1; controls are views that draw their own chrome. Data
views use a view-based model (SL gained these for tables in 10.7; the
user-facing class surface is the same).

Accessibility is Tier 1: **every catalog view carries a11y metadata**
(role, label, value, enabled/focused, help) from the v1 cut —
parallel to AppKit's NSAccessibility protocol surface (argentum-plan §4).

## Tier 1 — core controls (v1 cut)

| AppKit (SL) | Argentum | Notes |
|---|---|---|
| `NSButton` (push) | `Button` | push type |
| checkbox / radio (`NSButton` types) | `Button::Type { Push, Checkbox, Radio }` | mirrors NSButton type semantics; radio grouping by parent Box (Cocoa's NSMatrix role) |
| `NSPopUpButton` | `PopUpButton` | presents a `Menu` |
| `NSSlider` | `Slider` | |
| `NSStepper` | `Stepper` | |
| `NSTextField` (+ label role) | `TextField` | single-line; label variant |
| `NSSecureTextField` | `SecureTextField` | TextField subclass |
| `NSImageView` | `ImageView` | |
| `NSProgressIndicator` | `ProgressIndicator` | bar + spinning styles |
| `NSSegmentedControl` | `SegmentedControl` | |
| `NSSearchField` | `SearchField` | styled TextField subclass |
| `NSColorWell` | `ColorWell` | minimal in v1; the color *panel* stages later |
| `NSLevelIndicator` | `LevelIndicator` | capacity/discrete/continuous/relevancy |

## Tier 2 — structure (v1 cut)

| AppKit (SL) | Argentum | Notes |
|---|---|---|
| `NSScrollView` | `ScrollView` | the wrapper data/rich views live in |
| `NSSplitView` | `SplitView` | |
| `NSTabView` (+ `NSTabViewItem`) | `TabView` | |
| `NSBox` | `Box` | titled group + the row/column arrangement role (plan §4) |
| `NSMenu` / `NSMenuItem` | `Menu` / `MenuItem` | NOT views; the config-framed menu model — used by the global menubar (Kestrel) and `PopUpButton` |

Staged after v1: `NSToolbar` (window accessory), `NSPanel` (utility
window), `NSMatrix` (if radio grids are ever wanted). `NSDrawer` is
excluded (deprecated in later macOS; no FNX counterpart needed).

## Tier 3 — data & rich views (staged, not v1)

| AppKit (SL) | Argentum | Phase | Notes |
|---|---|---|---|
| `NSTableView` | `TableView` | first data view (v1-basic or immediately after) | columns, rows, selection, data-source/delegate; editing + sorting within its own milestone |
| `NSTextView` | `TextView` | after TableView | multi-line rich text — SL's text system is large; v1 has single-line `TextField` only |
| `NSOutlineView` | `OutlineView` | staged | tree; view-based |
| `NSCollectionView` | `CollectionView` | staged | grid of items |
| `NSBrowser` | `Browser` | staged | column browser |
| `NSComboBox` | `ComboBox` | staged | |
| `NSTokenField` | `TokenField` | staged | |
| `NSDatePicker` | `DatePicker` | staged | needs a calendar model |
| `NSRuleEditor` | `RuleEditor` | staged | |

## Windows, menus, and exclusions

- Windows parallel `NSWindow` (content view + chrome), with `Panel`
  (utility) staged; the app main menu is the global bar owned by
  Kestrel (plan §5–6), paralleling `NSApp` `mainMenu`.
- `NSOpenGLView` is **deliberately not paralleled** — GPU is a
  non-goal; the display is X11/Xfb and chrome/text are software.
- Cell/`NSCell` internals, `NSFormatter`, and the SL *look* are not
  paralleled (FNX's own chrome/theme model replaces them).

## Milestone mapping

- **v1 (S2)**: Tier 1 + Tier 2 structure essentials (ScrollView,
  SplitView, TabView, Box, Menu) + TableView-basic as the first data
  view.
- **After S2**: TextView, TableView richness (editing/sorting),
  Toolbar/Panel.
- **Post-S5 or later**: OutlineView, CollectionView, Browser,
  ComboBox, TokenField, DatePicker, RuleEditor.
