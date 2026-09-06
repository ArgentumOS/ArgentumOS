# Shrike v1 widget catalog + reference app (was: Momo v1)

Status: **CATALOG SPEC (2026-09)** — the v1 widget set and the
reference-app sketch. Originally DECIDED under the Motif-fork direction
(docs/archive/motif-fork-plan.md); it survives the pivot to Shrike
(docs/design/shrike-plan.md) as the v1 catalog. The flat `momo_*` API shape is
superseded by the C++ class model (`shrike::Widget` hierarchy); the
widget set and reference-app intent carry over unchanged.

## 1. v1 widget catalog

Grouped by role; entries with `*` carry a known dependency. Deliberately
small — everything in v1 is load-bearing for the first desktop apps
(Settings, Editor, Disks, Installer, Viewer).

**Top-level**
- `momo_window` — toplevel container (title, size, close-veto).

**Leaves**
- `momo_label` — text (UTF-8) or icon.
- `momo_button` — push button.
- `momo_toggle` — check/toggle (radio semantics via a group option).
- `momo_text_field` — single-line editable text (M5 text engine *).
- `momo_text` — multi-line editor (the M5 iceberg *).
- `momo_canvas` — custom drawing surface.
- `momo_slider` — value slider.
- `momo_separator`.

**Containers (the two-container model, docs/archive/motif-fork-plan.md)**
- `momo_box` — row/column, packed/padded.
- `momo_layout` — springs-and-struts container (working name; see open
  items).
- `momo_scroller` — viewport + scrollbars around one child (Text, lists,
  Viewer, Terminal later).

**Menus** (global model, docs/archive/motif-fork-plan.md)
- `momo_menu`, `momo_menu_item`, separator/check items — app-side menu
  *model*. There is **no per-window menu bar widget**: the menubar is a
  single global bar **owned by the window manager** (the EMWM fork
  hosts it as its own borderless window at the top of the screen and
  swaps the focused app's published menu model). Menu publishing/
  ownership is the later app-model topic.

**Dialogs**
- `momo_message` — info/warn/error/question with buttons.

**Future (not v1)** — catalog-extension items: `momo_tree`,
`momo_toolbar`, `momo_terminal` (urxvt core, post-M5), tabs/notebook;
`momo_progress`, `momo_file_chooser` are v1.1 candidates, not v1.
`momo_browser` — see below.

## 2. Reference app — Settings (decided)

The Settings window is the canonical call-site sketch: it exercises
springs *and* box, typed descriptors, typed callbacks, a veto, and user
data in one compact window.

```c
#include <momo/momo.h>

/* typed callback; momo_button_event_t has .pressed, .repeat, ... */
static void on_apply(momo_button_t *b, const momo_button_event_t *ev, void *user)
{
    const char *host = momo_text_field_text(user);   /* user = the field */
    momo_config_set("System/Configuration/network.conf", "hostname", host);
    momo_message_show(b, &(momo_message_config_t){
        .kind = MOMO_INFO, .text = "Settings saved." });
}

/* veto: returns false to refuse the close (typed payload, no casts) */
static bool on_close_request(momo_window_t *w, const momo_window_event_t *ev, void *user)
{
    return momo_config_dirty(user) ? /* unsaved edits? */
        momo_message_ask(w, &(momo_message_config_t){
            .kind = MOMO_QUESTION,
            .text = "Discard unsaved settings?" })
        : true;
}

int main(int argc, char **argv)
{
    momo_init(&argc, &argv);

    momo_window_t *win = momo_window_create(&(momo_window_config_t){
        .title = "Settings",
        .default_size = { .w = 440, .h = 300 },
        .on_close_request = on_close_request,   /* bool veto */
    });
    momo_set_user_data(win, config_state);      /* closure on the widget */

    /* outer page: springs-and-struts (content hugs, edges spring) */
    momo_layout_t *page = momo_layout_create(win, NULL);
    momo_layout_add(page, page_header, &(momo_attach_t){ MOMO_SPRING_H, MOMO_STRUT_V });

    /* inner rows: packed column */
    momo_box_t *rows = momo_box_create(page, &(momo_box_config_t){
        .orientation = MOMO_COLUMN, .spacing = 6, .padding = 10 });

    momo_label_create(rows, &(momo_label_config_t){ .text = "Hostname" });
    momo_text_field_t *host = momo_text_field_create(rows, NULL);

    momo_toggle_t *net = momo_toggle_create(rows, &(momo_toggle_config_t){
        .label = "Start networking at boot", .checked = true });

    momo_slider_t *vol = momo_slider_create(rows, &(momo_slider_config_t){
        .min = 0, .max = 100, .value = 70 });

    /* button row: a box inside the spring page, strut to bottom */
    momo_box_t *btns = momo_box_create(page, &(momo_box_config_t){
        .orientation = MOMO_ROW, .spacing = 6 });
    momo_button_create(btns, &(momo_button_config_t){
        .label = "Apply", .on_activate = on_apply, /* user = host field */
        .user = host });
    momo_button_create(btns, &(momo_button_config_t){
        .label = "Cancel" });

    momo_main();
    return 0;
}
```

The sketch validated the shape: typed callbacks and the veto read
clearly; boxes/springs compose; no casts, no strings-as-events. It also
surfaced the open micro-decisions below.

## 3. Open micro-decisions (surfaced by the sketch)

1. **User-data placement**: config-time `.user` everywhere (the sketch
   uses it for the button) vs `momo_set_user_data()` (used for the
   window). Lean: config-time `.user` consistently; revisit the
   setter's existence at M4.
2. **Convenience args vs descriptors**: `momo_label_create(rows, NULL)`
   vs a convenience form for trivial widgets. Lean: keep descriptors
   everywhere (uniform); convenience macros only if real call sites
   demand them.
3. **Spring attachment syntax**: `MOMO_SPRING_H/MOMO_STRUT_V` macros vs
   a per-edge `{top,left,bottom,right}` struct with weights. Needs one
   design pass at M4.
4. **Dialog parentage**: `momo_message_show(widget, ...)` — the dialog
   model connects to the later app-model topic (momo_application,
   menus, clipboard, EMWM seam).

## 4. Decisions this pins

- Catalog as listed above is the v1 set; future entries are named and
  deferred, not omitted by accident.
- Settings is the reference call-site app for the API-shape milestone.
- The sketch's mechanisms (typed descriptors, typed callbacks, veto
  bools, two-container layout) are the shape the M4 API must deliver.
- **Fonts: Xft** (FreeType/fontconfig added to the X stack; no core
  fonts, no bitmap stack).
- **Menus: one global menubar** (NeXT/macOS model); no per-window menu
  bars; the bar is shell-provided, apps publish their menu model.

## 5. momo_browser — the browser widget (decision)

A first-party HTML/viewer widget is needed long term (local content,
docs/help, light web). Decision on the engine:

- **CEF / Chromium: evaluated and rejected.** ~30M lines of C++, needs
  glibc (FNX is musl), wants GPU/compositing/sandbox plumbing FNX
  deliberately lacks, and an unsandboxed Chromium is a security hole
  against the OS profile. The embedding seam was never the problem —
  Chromium-on-FNX is.
- **Candidate: NetSurf core** — the browser engine whose architecture
  fits the house pattern: the core is C (HTML/CSS/layout, no toolkit
  deps) and it talks to a *front end* through a defined content/UI
  API — the urxvt engine-extraction shape repeated. A Momo front end
  would wrap NetSurf core as `momo_browser` (sibling of the Terminal
  widget). Realistic scope: HTML4/CSS2.1-class + optional small JS —
  the honest ceiling for a built-in viewer, not a modern-Web engine.
- Licensing (verify at evaluation time): NetSurf core is GPL-2.0
  (some components MIT).
- Framing: FNX needs a first-party HTML/viewer *widget*, not a
  Chromium-class browser; if a modern-Web engine is ever a hard
  requirement that is a "reconsider the OS profile" conversation, not a
  widget decision.

Recorded as future work; a proper evaluation (license, core API fit,
front-end scope) happens when the first-party Viewer/Help needs it.
