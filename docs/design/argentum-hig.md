# Argentum Interface Guidelines (HIG)

Status: **DECIDED (2026-09).** These are conventions applications *should*
follow. They are **not** enforced in code, and that is deliberate — see §1.

## 1. What a HIG is here (and why the toolkit does not enforce it)

`Application::setMenuBar()` publishes exactly the menus an app declares, in
exactly the order it declares them (the model goes over the session socket and
Kestrel draws the titles it is given — `docs/design/argentum-s4-kestrel.md`
§S4.2a/§S4.2c). Nothing sorts, inserts, rejects or repairs a menu bar: an app
that gets the order wrong simply looks wrong, next to the apps that got it
right.

That is the intended division. A menubar is *content* — what an app offers —
and a toolkit that rewrote it would be guessing at the app's intent (is
"Widgets" a View menu or its own thing? should an empty File be hidden or
disabled?). The toolkit provides the machinery — menus, items, kinds, key
equivalents, the wire — and the guidelines here provide the shape that makes a
family of apps feel like one system.

Consequence to accept: **compliance is a review matter, not a test.** A gate
can check that an app's own menus are where the app says they are (see
`tests/cases/wm_dock.py`), but no gate can hold "every app follows the HIG"
for apps that do not exist yet.

## 2. The application menubar

Every app's menubar is, left to right:

| # | menu | title | what belongs in it |
|---|---|---|---|
| 1 | application | the app's display name | About, Preferences, Quit — §3 |
| 2 | File | `File` | documents: New, Open, Close, Save, Print |
| 3 | Edit | `Edit` | Undo, Redo, the clipboard, selection |
| 4 | View | `View` | what the window *shows*: toggles, zoom, columns |
| 5 | (the app's own) | app-chosen | any number, in this slot only |
| 6 | Windows | `Windows` | the window list, Minimize, Zoom |
| 7 | Help | `Help` | help, About, documentation |

Rules:

1. **The application menu is always FIRST.** It is not optional, and it is
   never merged into another menu.
2. **Its title is the app's display name**, from the bundle manifest's `name`
   (`docs/design/app-model.md` §5) — the manifest, not a string the app
   invents. The name a person sees in the launcher is the name in the menubar.
3. **The app's own menus go between View and Windows.** Never before the
   application menu, never after Help. This is the one slot that varies
   between apps, and it varies in exactly one place.
4. **A standard menu with no items is OMITTED, not shown empty.** An empty
   dropdown reads as a bug. An app with no documents shows no `File`; the
   order of the menus that *are* present still holds. (Deliberately not
   "show it disabled": a disabled menu still opens and still says nothing.)
5. **Titles are one plain capitalized word**: `File`, `Edit`, `View`,
   `Windows`, `Help`. No ellipses, no mnemonics, no abbreviations.
6. **Order is positional, not a sort key.** Skipping a menu does not move the
   others: an app with an app menu, a View item and its own menu reads
   `[App] View Custom`, not `[App] Custom View`.

## 3. The application menu

Titled with the display name, containing in order:

- `About <App>` first;
- a separator, then app preferences if the app has any (none shipped yet);
- a separator, then `Quit <App>` **last**, with the ⌘Q key equivalent.

`Quit` is the app's own exit (`Application::terminate()`), not a WM action.

## 4. Key equivalents

- `⌘Q` — Quit, in every app (the application menu).
- `⌘W` — Close, where the app has a closable window (File).
- `⌘,` — Preferences, where the app has them (application menu).
- Other equivalents belong to the item they act on, in the Edit menu for
  editing verbs.

## 5. Open items

These are unresolved, and an app should not invent an answer for them:

- **The `Windows` menu has no owner.** Its content is the window list, which
  the *window manager* knows, not the app — nothing publishes a window list
  today, and Minimize/Zoom are undesigned (`argentum-s4-kestrel.md` §6). Until
  then an app has nothing to put in it, so rule §2.4 omits it. It is listed
  above because the target shape is fixed, not because it can be filled.
- **The `Help` menu has no content yet.** The documentation plan stages UIKit
  documentation under `/System/Documentation`; no app-facing help API exists.
- **Resolved (2026-09): the desktop's half of the strip carries no app
  name.** It drew the focused client's *window title* there, which read as a
  duplicate of the application menu; the name belongs to the app's own bar.
  The strip now draws only what is the desktop's — the system mark and the
  clock — and the app zone starts right after the mark.
