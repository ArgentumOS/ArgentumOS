# EMWM window manager — FNX's X11 window manager (fork decision)

Status: **SUPERSEDED (2026-09)** by `docs/shrike-plan.md` §5 — the window
manager is now a from-scratch C++ component built on Shrike (the EMWM
rationale — "it is itself a Motif app" — died with the Motif fork).
Kept as a record of the fork evaluation. (Was: DECIDED — nothing vendored
or implemented.

Decision: FNX will **fork and extend EMWM** as its X11 window manager.

## 1. What EMWM is

**EMWM — the Enhanced Motif Window Manager** (fastestcode.org, maintained
by Alex / alx210; v2.1, Aug 2026) is a fork of the **Motif Window
Manager (mwm)** — the CDE-era window manager — that preserves the Motif
look and behavior while fixing and extending it: multi-monitor via
Xinerama/Xrandr, UTF-8 and Xft fonts, multiple workspaces, EWMH
compatibility. A separate utilities package adds XmToolbox (a
toolchest-like launcher reading a plain-text `~/.toolboxrc`) and XmSm (a
simple session manager with session config, locking, shutdown).

## 2. Why this fits

- **Lineage coherence**: EMWM is itself a Motif/libXm application, and
  its own site recommends `thentenaar/motif` as the actively maintained
  Motif ("Tim's fork … long-term maintainership") — the exact upstream
  the Momo toolkit forks (docs/motif-fork-plan.md). The window manager
  and the toolkit share a bloodline.
- **It is a Motif app**: EMWM runs against the stock Motif built in
  Momo M0/M1 — and after Momo's M4 API replacement it becomes a pure
  `momo_*` app like any other first-party app.
- **Replaces the dtwm/CDE reference**: the CDE fork was rejected; its
  dtwm role is filled by forking mwm's maintained descendant instead of
  importing CDE.

## 3. Place in the stack

Xfb (server, /dev/fb0) → EMWM fork (window manager) → first-party apps
on Momo. The CDE-fork-plan doc remains the *reference* for the future
desktop-shell integration (front panel / launcher / session concepts —
XmToolbox/XmSm are the immediate precursors to evaluate), but nothing
CDE is forked.

## 4. Sequencing

- Earliest practical start: after **Momo M1** (stock Motif on Xfb) —
  EMWM needs libXm, which M0/M1 provide.
- Styling/identity/config work belongs to the **desktop-shell phase**
  (post-Momo, per docs/momo-coding-plan.md M7+).

## 5. Known extension/conversion work (recorded, not scheduled)

- **Configuration → `.conf`**: EMWM uses `~/.emwmrc` and X resources /
  app-defaults; per the house rule (docs/motif-fork-plan.md item 2,
  "X resources leave in favour of libconfig") the fork migrates these to
  `.conf` domains.
- **Identity**: product rename at its own milestone (XBFS/FNX naming
  precedent), attribution kept (EMWM's license/notices).
- **Utilities**: decide later whether XmToolbox (toolchest launcher)
  and/or XmSm (session manager) are forked alongside or replaced by
  FNX-native equivalents.
- **EWMH/Xinerama/workspaces** are already in EMWM v2 — inherited, not
  re-derived.

## 6. Open items

- FNX name for the fork.
- Vendored home (third_party/emwm + fork in userland/?).
- Which utilities (XmToolbox/XmSm) are adopted.
- Feature-extension list beyond EMWM v2 (defined at the fork milestone).

## 7. Non-goals

- No CDE/IRIX window-manager compatibility obligations.
- No multiple WMs: this is the one first-party window manager
  (consistent with the one-toolkit doctrine — users may run other WMs
  on Xfb unhelped).
