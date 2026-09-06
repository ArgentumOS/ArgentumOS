# Terminal emulator — fork decision (rxvt-unicode)

Status: **DECIDED (note, 2026-09) — nothing vendored or implemented.**

Decision: FNX's first-party terminal emulator will be a **fork of
`https://github.com/yusiwen/rxvt-unicode`** (rxvt-unicode / urxvt).

## 1. What it is

**rxvt-unicode (urxvt)** — Marc Lehmann's X11 terminal emulator
(software.schmorp.de), the Unicode-capable continuation of rxvt:
full Unicode incl. CJK, Xft anti-aliased fonts, XIM input, ISO 14755
entry, 256-color/truecolor modes, multiple scrollbar styles,
transparency (via RENDER), rich xterm escape coverage, optional embedded
perl extensions. Plain C, GPL-3.0.

`yusiwen/rxvt-unicode` is a **daily-synced git mirror of the official
CVS repository** (15,247 commits, autotools build, no imake) — the
practical git upstream to fork, since upstream's home is CVS.

## 2. Why this fits

- **Plain C** — matches the language doctrine.
- **A slim X11 client**: needs only Xlib + Xft (gdk-pixbuf/perl are
  optional configure features, not requirements) — it runs on Xfb with
  no toolkit dependency.
- **Custom-drawn by nature**: a terminal renders its own glyph grid,
  scrollback and scrollbars. The fork ships standalone (Xlib, no
  toolkit chrome) until Momo exists — an interim, doctrine-excepted
  state; long term the app chrome is Momo (see §5).
- **VT100-class scope**: matches the Terminal app in the desktop/
  initial-release plan.
- **Configure-driven trimming**: `./configure` has precise
  `--enable/--disable` switches, so the fork starts from a minimal
  configuration and adds back only what FNX wants.

## 3. Place in the stack

Xfb (server) → EMWM fork (window manager, docs/emwm-window-manager.md)
→ first-party apps: this terminal is an X11 client like any other; it
runs before Momo exists and becomes a Momo-chromed app later if
desired.

## 4. Sequencing

- Earliest practical start: whenever an interactive Xfb session exists
  (it does today) — no toolkit dependency.
- Terminal-window polish (tabs? chrome/menus, pref dialogs) belongs
  with the Momo desktop-app phase.

## 5. Long term: wrapped in Momo UI chrome (decided)

Long term the terminal is **wrapped in Momo UI chrome** — the window's
app chrome (menus, preference dialogs, and any tabs/decoration) is
Momo; the urxvt core (pty, escape-sequence engine, glyph grid,
scrollback) is what urxvt contributes.

**Shape (decided): (b) engine extraction.** The urxvt core is ported
into Momo as a proper terminal widget (a sibling of the Text widget
from the Momo M5 text work) that hosts pty/escape/grid/scrollback; the
standalone fork then becomes a thin Momo app around that widget — the
the terminal is a Momo citizen like any other app. The battle-tested
vt100/pty engine is the contribution; the (a) in-process wrap is set
aside.

Until Momo exists, the standalone fork is the interim terminal (runs on
Xfb today; §4).

## 6. Known fork work (recorded, not scheduled)

- **Product rename** at its own milestone (FNX naming precedent),
  attribution kept (GPL-3.0 + Marc Lehmann/upstream notices).
- **Configuration → `.conf`**: urxvt is configured through X resources
  (`URxvt.*` keys — a large surface: fonts, colors, scrollbar,
  keybindings). Per the house rule (docs/motif-fork-plan.md item 2) the
  fork migrates this to `.conf` domains. This is the largest conversion
  item.
- **Drop the embedded perl interpreter** (plain-C doctrine): extensions
  are re-derived in C only if needed; disable at configure.
- **Trim optional deps**: gdk-pixbuf backgrounds, XIM depth, RENDER
  transparency — decide per feature, not inherited.
- **TERM/terminfo**: `TERM=rxvt-unicode` and the terminfo story need an
  FSH decision (terminfo location under /System?) — a real open item.

## 7. Open items

- FNX name for the fork.
- Vendored home (third_party/rxvt-unicode + fork in userland/?).
- Configure baseline (which feature switches FNX starts from).
- Tabs/multiplexing: in-fork feature or separate (toybox `sh` sessions
  per window) decision.
- terminfo placement under FSH.
- **Momo-chrome shape: (b) engine extraction — DECIDED.** The urxvt
  core (pty/escape/grid/scrollback) is ported into Momo as a proper
  terminal widget (sibling of the Text widget from the M5 text work);
  the standalone fork becomes a thin Momo app around that widget. The
  in-process wrap (a) is set aside.

## 8. Non-goals

- No xterm/other-terminal compatibility obligations.
- No perl extension ecosystem (dropped; not ported).
- One first-party terminal (users may run other X11 terminals unhelped).
