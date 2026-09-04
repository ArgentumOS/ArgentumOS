# GUI candidate E′ — OS-native windowing API + a full widget toolkit

**SUPERSEDED (2026-09) — the FNX-native widget toolkit direction was
abandoned and its implementation (libwidgets, widgets_demo, the text/font
engine) was removed from the tree.** The GUI will instead port an existing
toolkit onto the compositor, or replace the compositor entirely; the C++
toolchain preparation for that is tracked in `docs/cpp-toolchain-plan.md`.
This document is kept as the historical decision record for the E′
direction.

Status: **CHOSEN GUI DIRECTION — fully decided** (Q-L1..Q-L8, §10): E′
with the E′-b object model — the OS-native windowing API (candidate E)
plus a full widget toolkit on an FNX-native view object model, **all in
plain C** (no Objective-C/C++). Variant of candidate E
(`docs/gui-e-native-api.md`); the wider candidate survey is
`docs/gui-candidates.md`.

---

## 1. Concept

Layered, all in userland:

```
apps (C, link libwidgets)
    └── libwidgets   — the widget toolkit (full catalog)
          └── libgui — windows, backing buffers, events (candidate E)
                └── compositor — owns /dev/fb0, stacking, input
```

The compositor is exactly as in E. Everything above it is client-side:
views draw into the window's backing buffer and process events in the
app's main loop. A decisive simplification: since every widget lives in
one process, there is no distinction between window-owning widgets and
lightweight ones — **all widgets are equally lightweight**.

The design openly draws on the classic workstation precedents — an
OPENSTEP-style view model, Motif's manager-widget catalog, Mac OS 9's
Platinum widget look, the classic Mac global menubar — re-imagined in
plain C as FNX's own (the catalog and layout below are FNX's; the
lineage is acknowledged in passing).

## 2. Widget catalog

Tiered, matching what apps actually need:

- **Basic views**: Label, Separator, Canvas (arbitrary drawing).
- **Controls**: PushButton, ToggleButton, ArrowButton, ScrollBar,
  Scale.
- **Text/list**: Text (multi-line editor), TextField (single-line),
  List, ComboBox.
- **Containers** (layout views): Frame (bevel border), RowColumn
  (menus, radio/check groups), Form (attachment layout),
  SpringsAndStruts (springs-and-struts layout), PanedWindow,
  ScrolledWindow, MainWindow, BulletinBoard. (DrawingArea is folded
  into the view model — every view draws; `canvas` is the ready-made
  custom-drawing view.)
- **Menus**: MenuBar, PulldownMenu, PopupMenu, CascadeButton.
- **Dialogs**: MessageDialog, SelectionDialog, FileSelectionDialog,
  PromptDialog, Command.
- Deferred (optional): Notebook, TabStack, Tree.

### Widget hierarchy (class tree)

The catalog as a class tree under the E′-b object model. `view` is
the base (frame/bounds, `draw(view, renderer)`, subviews, hit-testing,
responder chain); `control`s are interactive views, `container`s are
views that arrange their subviews (see §3):

```
view                                    (base: frame/bounds, draw(view, renderer),
 │                                        subviews, hit-testing, responder chain)
 ├── control                            (interactive views)
 │   ├── button
 │   │   ├── push_button
 │   │   ├── toggle_button
 │   │   └── arrow_button
 │   ├── cascade_button                 (menu title, renders in the global bar)
 │   ├── scroll_bar
 │   ├── scale
 │   ├── text
 │   │   └── text_field
 │   ├── list
 │   └── combo_box
 ├── label                              (static text/icon)
 ├── separator
 ├── canvas                             (plain custom-drawing view; every view
 │                                        draws itself, this is the ready-made one)
 ├── container                          (views that arrange their subviews)
 │   ├── springs_and_struts             (struts fixed, springs flexible,
 │   │                                    both axes)
 │   ├── frame                          (one child; bevel border)
 │   ├── form                           (attachment layout)
 │   ├── row_column                     (menus + radio/check groups)
 │   │   ├── menu_bar                   → published to the shell's global bar
 │   │   │                                (global-bar layout: system menu,
 │   │   │                                 app menu, File/Edit/View/Window/Help,
 │   │   │                                 right-side extras)
 │   │   ├── pulldown_menu
 │   │   └── popup_menu
 │   ├── paned_window
 │   ├── scrolled_window
 │   ├── main_window
 │   └── bulletin_board                 (dialog base)
 │       ├── message_dialog
 │       ├── selection_dialog
 │       │   └── file_selection_dialog
 │       ├── prompt_dialog
 │       └── command
 └── (deferred: notebook, tab_stack, tree)
```

Notes:

- **`view` is the base**: every view draws itself (draw method via the
  renderer), can own subviews, and handles events through hit-testing
  and the responder chain. `control` and `container` are *roles*, not a
  hard split — composition is universal (see §3).
- **Menus are containers but render globally**: `menu_bar`/
  `pulldown_menu`/`popup_menu` are `row_column` children in the app's
  object model, yet the `menu_bar` is *published* to the shell, which
  renders it in the screen-top bar (detached from windows, per the
  global-menubar design below); `cascade_button` (a button subclass) is
  the menu title.
- **Dialogs hang off `bulletin_board`** — a container, so a dialog is
  a container view with a window; `file_selection_dialog` inherits
  from `selection_dialog`.
- **`canvas` is the ready-made custom-drawing view** — a plain view
  with a full draw API through the renderer vtable (fill rect, line,
  blit, text, bevel edge) plus damage tracking. Because every view
  draws itself, `drawing_area` is folded into the model (there is no
  separate one-child drawing widget).
- **`springs_and_struts` is the native layout rule** — NeXTSTEP's
  autoresizing model: children pin to the container's edges with
  *struts*
  (fixed margins) and *springs* (flexible spans); on resize the springs
  stretch and the struts hold. `form` (attachment layout) and
  `row_column` are alternative layout rules a container view can apply
  — apps pick per container.

### Global menubar (decided)

Per the GUI direction, **no per-window menubars**: each application has
**one menubar at the top of the screen**, rendered by the shell — the
classic Mac OS layout — not
inside its windows.

- **Menu model**: the app owns a menu tree — the MenuBar/PulldownMenu/
  PopupMenu/CascadeButton widgets of the catalog, but **detached from
  windows**. The app publishes its menubar to the shell
  (`menubar_set(menubar)`); when focus moves between apps (or between
  an app's windows, if the app defines per-window menu variants), the
  shell swaps the top bar to the focused app's menus.
- **Menu validation on click (decided)**: before a menu drops down, the
  shell asks the *application* to validate — the app enables/disables
  items, updates labels, checkmarks, and radio state, and can add or
  remove items as needed (the validation-on-open pattern). API: the
  app registers a per-menu validation callback
  (`menu_set_validate(menu, fn)`); clicking a title delivers a
  `menu_needs_update` request, and the shell renders the dropdown only
  after the app replies.
- **Rendering**: the shell draws the menubar and the dropdowns from the
  app-provided menu data — one renderer, consistent look, the shell
  owns the screen top; dropdowns composite like surfaces. The toolkit
  builds and stores the menu model and runs validation; it does not
  render menus into the app's window.
- **API additions** (bare names, per the no-prefix decision):
  `menubar_set`, `menu_create/add_item/remove_item`,
  `menu_item_set_enabled/checked/label`, `menu_set_validate`, and the
  `menu_needs_update` event; focus changes drive the bar swap.

Widget-catalog consequence: MenuBar/CascadeButton exist in the
toolkit's object model but render into the *global* bar, not
per-window — a deliberate design choice: one bar per application, at
the top of the screen.

### Menubar layout (decided)

One always-visible bar at the top of the screen; its contents, left to
right:

1. **The system menu**: a fixed, system-owned menu — About FNX OS,
   System Configuration, Sleep, Restart, Shut Down, Log Out — that
   does not change with the focused app.
2. **The application menu**: the bold app name (e.g. `Terminal`),
   then About App, Preferences (⌘,), Services, Hide / Hide Others /
   Show All, Quit App (⌘Q). Always the second item; the bar never
   changes shape — app menus replace the *contents* to the right of
   the app menu, not the structure.
3. **The standard menu order** for every app: File, Edit, View,
   **Window**, **Help** — Help is the **rightmost** menu. The Window
   menu is app-owned but follows the convention: Minimize, Zoom,
   Bring All to Front, then the app's open windows.
4. **Menu bar extras** (the right side of the bar): status items such
   as the clock, input menu, and system toggles — shell-owned, never
   app-owned, aligned right.
5. **Item conventions**: key equivalents right-aligned with the system
   glyphs (⌘⌥⇧⌃), disabled items dimmed, single checkmark state
   (`menu_item_set_checked`), separators, and submenu triangles —
   all rendered by the shell from app menu data.
6. **Validation on click** (already decided, above) is what makes the
   dim/enable behavior live — apps update their menus on open via
   `menu_set_validate`.

The visual style of the bar follows the Platinum look decision (Q-L2).

## 3. Object model

Two designs were weighed for the widget machinery; **E′-b is chosen**:

- **E′-a — heavyweight class/resource machinery** (rejected): widget
  classes with inheritance, a resource database, a
  geometry-management hierarchy. Powerful, but ~100k lines of dated
  machinery and a lot of API surface to commit to.
- **E′-b — the FNX-native view model (chosen).** The widget *set* and
  *look* (catalog §2, Q-L2) with a lean view hierarchy:

  - **`view` is the base class** — a rectangular region with
    frame/bounds, a `draw(view, renderer)` method (every view draws
    itself), arbitrary **subviews**, hit-testing, and a **responder
    chain** for events.
  - **Composition is universal**: any view can contain subviews — no
    hard primitive/manager split. `control`s are interactive views,
    `container`s are views that arrange their subviews via a layout
    rule.
  - **Every view can draw arbitrary content** — the `canvas` is the
    ready-made custom-drawing view; `drawing_area` is folded into the
    model.
  - **Layout**: `springs_and_struts` (autoresizing) is the native
    rule; `form`/`row_column`/etc. are alternative rules a container
    applies.

Same widget set, fraction of the machinery, and a drawing model where
a canvas view is not a special case — it is what every view is.

### Implementation language: plain C (decided)

The entire GUI — compositor, `libgui`, `libwidgets`, and apps — is
**plain C** (ANSI C, matching the kernel). The view model is a
*conceptual* model, not an object runtime:

- `view` is a C struct; its vtable is a plain struct of function
  pointers (`view_ops`: draw, hit_test, resize, destroy, ...).
- Subclassing is struct embedding — the base `view` is the first
  member of every derived struct, giving C-level inheritance with no
  runtime.
- `control`/`container` roles are fields and ops in the base view, not
  classes; the responder chain is a `next_responder` pointer walked
  by the event router.
- Callbacks are C function pointers; resources are config-backed
  (Q-L4); strings are config-backed (Q-L7).
- No Objective-C (no messages, no runtime, no ARC), no C++ (no
  classes, no RTTI, no STL). Libraries are static C (`libgui`,
  `libwidgets`); apps compile with the FNX musl toolchain like today.

## 4. Resources → the `config` utility

App defaults and per-view resources become **config domains**: view and
app resources live in `system.config.gui.conf` (and per app, e.g.
`com.example.App.conf`) at the three scopes, exactly per
`docs/config-design.md`. A view reads `view_get_resource(view,
"font")`-style names backed by libconfig — the toolkit is the first
big consumer of the config system.

## 5. Look & feel

The visual language — a **Mac OS 9-like Platinum look**: soft
silver-gray surfaces, subtly rounded corners, gently raised/sunken
bevels with soft highlights rather than hard chiseled edges — is
distinctive and cheap to render with a blitter (fill + a couple of
edge lines per bevel). Choices (Q-L2): faithful Platinum gray vs FNX's
own palette/styling on the same relief primitives. Either way the
relief drawing primitives belong in the toolkit's renderer, not the
compositor.

## 6. Internationalization & accessibility

### i18n — constrained by the UTF-8-only decision

`docs/utf8-only.md` already fixes the encoding story: one encoding, one
locale, no iconv, no per-locale collation/formatting. What that leaves
the GUI:

1. **UTF-8 text pipeline.** All widget text is UTF-8; the text renderer
decodes UTF-8 and maps to glyphs. v1 ships the Latin-9 bitmap fonts
(adequate for Western European); the glyph story beyond that (larger
bitmap sets, or a font format + rasterizer) is the deferred
"full Unicode" item. The renderer takes a **glyph source** interface,
so a font backend can be added without touching widgets.
2. **Keyboard layouts.** The kernel already has Linux-keymap support
(the keyboard driver); the GUI input path selects a layout per
user/session — and the bar already includes an **input menu** among
the bar extras. A layout is a keymap *resource*, config-backed per
Q-L4.
3. **UI strings.** The single-locale decision means no locale framework;
the open question is whether strings stay code-embedded English
(simplest, one-locale) or are externalized as string resources via a
config domain (`system.config.gui.strings`) so translations can be added
later without recompiling — a design choice (Q-L7). Either way: no
ICU, no plural/collation machinery.
4. **Explicitly deferred:** bidirectional/RTL text, complex scripts,
input methods (CJK IME) — noted as out of scope for the first GUI.

### a11y — designed in, not bolted on

1. **Keyboard accessibility is part of the view model.** Every
   view/control participates in a focus/tab-order chain; full
   keyboard access (Tab/Shift-Tab, arrows within groups, Space/Enter
   activate) is core event routing, not an add-on. The global menubar
   has full keyboard navigation (meta to the bar, arrows, Return) and
   every item carries a key equivalent — the primary shortcut system.
2. **Mouse keys**: a keypad-driven pointer path — small, included.
3. **Theming / high contrast / large text via `config`**: colors, bevel
   styles, and font size are widget *resources* (Q-L4), so a
   high-contrast theme or larger-text profile is just a config profile
   (`system.config.gui.conf`, per-user scope). This is the a11y lever that
   costs almost nothing given the resource design.
4. **Reduced motion**: v1 has no animations; a config "reduce motion"
   flag gates any future ones.
5. **Assistive technology API (in the first GUI, per Q-L8)**: a screen
   reader needs the view tree with roles/labels/state plus
   focus/value events. Because E′ has no wire protocol, this is an
   *additive* toolkit API (`view_get_role/label/state`, a11y events)
   consumed by an assistive app through the same transport. The view
   hierarchy provides the introspection hooks from M1; the full
   role/label/state + event API lands by M3.

## 7. Milestones (with feasibility-effort notes, per `docs/gui-feasibility.md`)

All M0–M4 require **zero kernel changes**; each milestone is
independently testable in QEMU on the current tree. Effort estimates
are from the feasibility evaluation.

- **P0 — Prerequisite: implement `libconfig`.** The header
  (`include/libconfig.h`) is designed; no implementation exists, and
  Q-L4 (widget resources) depends on it. ~1–2k lines, independently
  useful.
- **M0 — API + compositor + windows** (~2–4k lines): per
  `docs/gui-e-native-api.md` — compositor owns `/dev/fb0`, window tree,
  stacking, focus; `libgui` over the socket+shm transport. Input v1:
  console raw mode (keys) + `/dev/psaux` (mouse); a dedicated input
  event node is a later, optional kernel addition.
- **M1 — Toolkit core** (~3–6k lines): view object model (frame/
  bounds, draw, subviews, responder chain), layout (springs-and-struts
  native, Form/RowColumn rules), drawing + bevel primitives; Label,
  PushButton, ToggleButton, Frame, Separator, Canvas.
- **M2 — Text/list** (~4–8k lines): Text, TextField, List, ComboBox,
  ScrollBar, ScrolledWindow; keyboard focus + editing. **The single
  hardest chunk — attack it early, it de-risks M4.** v1 text uses the
  shipped Latin-9 fonts; full UTF-8 glyph coverage is deferred
  (consistent with `docs/utf8-only.md`).
- **M3 — Menus + dialogs** (~2–4k lines): the **global menubar with
  click-time validation** (MenuBar/Pulldown/Popup per the §2 design),
  MessageDialog, FileSelectionDialog; a preferences dialog bound to
  `config` (now implemented, P0).
- **M4 — The smoke-test app** (~1–2k lines): a **text-editor/terminal
  built from the toolkit** (text-view based) — the GUI's first real
  app, launched from `/Applications`, config via `config`.
- **M5 — Deferred**: remaining widgets (Notebook, Tree), theming.
- **M6 — Acceleration (later, by design)**: DRM/KMS page flips and a
  GPU renderer behind the seams of §8 — the model is built so this
  slots in without redesign (see `docs/wayland-eval.md` §7 for the
  KMS scope).

Total M0–M4: ~12–25k lines of C userland, no kernel changes. The
biggest single risk is the Text widget (M2); everything else is
straightforward systems plumbing. The FSH dependency is non-blocking —
the GUI develops on the current rootfs first and moves to the FSH when
it lands (the porting linter gate applies to GUI binaries too).

## 8. Acceleration headroom (design principle)

The graphics model keeps **three seams** so an accelerated path (a GPU
driver later + DRM/KMS) can replace the software path piece by piece,
without redesigning windows, widgets, or the API:

1. **Compositor presentation backend.** The compositor presents its
   composited back buffer through a small `present(damage)` interface.
   v1: software blit to `/dev/fb0`. Later: KMS page flip / GPU scanout
   of the same buffer. Nothing in the window or toolkit model depends
   on the backend.
2. **Window buffers as opaque `buffer` objects.** The API returns a
   buffer handle, with a CPU map only on demand
   (`buffer_map/unmap`). v1: plain shm memory. Later: GEM/dumb
   buffers or DMA-BUF with zero-copy import. Toolkit hot paths never
   assume CPU-accessible pixels.
3. **Pluggable renderer in the toolkit.** Widgets draw through a small
   renderer vtable (fill rect, blit, text, bevel edge) — v1: the CPU
   blitter into the window buffer; later: a GPU command stream. The
   bevel primitives are defined as fill+edge ops, so a GPU backend
   implements them natively.

The model already helps: **damage tracking** is the redraw mechanism
(which is exactly what page flips consume), and a **fullscreen opaque
window is a natural future hardware-plane candidate** — nothing in the
window model blocks handing it to a plane.

These are *seams, not machinery*: three small interfaces (~tens of
lines of indirection), not a pluggable graphics framework. The depth of
abstraction now is a design choice (Q-L6).

## 9. Fit notes — the honest trade

- **Cost**: this is the **largest GUI candidate by far**. The compositor
  is still ~2–4k lines, but the toolkit is ~10–25k lines of C — the
  widget set, layout, text editing, and menus are real engineering.
  That is the price of "the OS ships the whole GUI".
- **Originality**: highest — the look and API are FNX's own.
- **Ecosystem**: none external; apps are FNX-native C against
  `libwidgets`; porting GTK/Qt apps = rewrite (no standard to aim
  at). In exchange, every FNX app gets a consistent, complete GUI for
  free.
- **Integration**: the best of all candidates — the toolkit consumes
  the `config` utility, speaks the FSH end to end, and the widget API
  becomes part of the OS (same lasting-commitment risk as E).
- **Kernel surface**: unchanged — fbdev + shm + sockets, all present.

## 10. Design choices

- **Q-L1 — Object model: full catalog on an FNX-native view model
  (E′-b).** The widget *set* and *look*, with the **view hierarchy**:
  `view` base (frame/bounds, `draw(view, renderer)`, subviews,
  hit-testing, responder chain), `control`/`container` roles rather
  than a primitive/manager split, universal composition,
  springs-and-struts native layout. The heavyweight
  class/resource/geometry machinery (E′-a) is rejected: the same
  widgets at a fraction of the code and API surface.
- **Q-L2 — Look: Mac OS 9 Platinum style.** The widgets wear a
  Mac OS 9-like Platinum look — soft silver-gray surfaces, subtly
  rounded corners, gently raised/sunken bevels with soft highlights —
  rather than Motif's chiseled gray. The relief primitives (fill +
  edge ops) stay in the renderer; FNX applies its own palette and
  styling on top, so the widgets have an FNX identity.
- **Q-L3 — Catalog scope: full catalog in v1.** The complete tiered
  widget set of §2 (basic views, controls, text/list, containers,
  menus, dialogs) ships together; no core-subset two-pass.
- **Q-L4 — Resources: config-backed.** Widget/app resources live in
  config domains (`system.config.gui.conf`, three scopes, dot-nested keys,
  user → shared → system precedence) per `docs/config-design.md` — the
  toolkit is libconfig's first big consumer. Hardcoded defaults are
  only fallbacks.
- **Q-L5 — ABI: freeze later.** The widget API does not freeze at M4;
  it stays fluid until the toolkit has settled (when the second real
  app ships, or at M6). Early apps may need porting — accepted.
- **Q-L6 — Acceleration seams: minimal seams now.** The three §8
  interfaces (compositor `present()` backend, opaque `buffer`
  handles, toolkit renderer vtable — ~tens of lines) are in from v1;
  a full pluggable-graphics framework is rejected as premature.
- **Q-L7 — UI strings: config-backed string resources (decided).**
  Strings live in `system.config.gui.strings` (+ per-app domains), three
  scopes, looked up like any widget resource — translatable later
  without recompiling, no locale/ICU machinery. Consistent with Q-L4.
- **Q-L8 — a11y: AT tree API in the first GUI (decided).**
  Keyboard access, tab focus, menu nav, mouse keys, and config theming
  ship with M1–M3; the assistive-technology tree API
  (`view_get_role/label/state`, a11y events) is built in the same
  milestones (introspection in M1, full API by M3), not deferred.

The E′/E′-b GUI plan is fully decided (Q-L1..Q-L8).
