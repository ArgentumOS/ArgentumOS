# CDE fork plan — the FNX desktop from the Common Desktop Environment

Status: **SUPERSEDED / REJECTED as a fork base (2026-09).** The direction
moved to forking **Motif (libXm) itself** — see `docs/motif-fork-plan.md`.
This document is kept as the captured reasoning: CDE's desktop shell
architecture (front panel, session manager, file-manager integration)
remains the *reference* for the future FNX-native desktop shell, but CDE
is not the base. Rejection rationale, in brief: with unlimited time, the
CDE fork's only real virtue — "the desktop arrives whole, borrowed and
restyled" — buys nothing, while its costs (≈2M lines of inherited 1990s
desktop machinery, the amputation list, shaping around legacy) are pure
downside. The toolkit (Motif) is the keystone the one-toolkit doctrine
actually needs; the desktop shell is built natively on it later.

Original status and content follow, unchanged, for reference.

## 0. Why this document exists

The FNX GUI direction has pivoted twice and now lands here:

1. The compositor + libgui custom window-server protocol is **dumped**;
   the display architecture is **Xfb** — a real X11 server rendering to
   `/dev/fb0` (docs/x11-xvfb-fb-plan.md, implemented past M0/M2/M3).
2. GUI-toolkit doctrine: FNX ships **only native-toolkit apps**. Users
   may run whatever they like on the X server, but we ship none of it.
3. Direction decided in conversation: **fork CDE and shape it in place**
   — the whole classic-UNIX desktop as one coherent product (the (b)
   and (c) motivation: desktop-as-a-whole + the classic-UNIX identity),
   not a widget library.

This document supersedes the toolkit-only framing of
docs/gui-e-toolkit.md (E′/E′-b was "own toolkit on the compositor" — the
compositor is gone) and the candidate survey's assumptions in
docs/gui-candidates.md. The toolkit question did not disappear — it is
now **inside** the CDE fork: CDE needs the Motif toolkit (libXm), so the
"modern Motif" discussion becomes a shaping milestone of this fork.

## 1. What CDE is (the fork target)

The Common Desktop Environment — created by Sun/HP/IBM/DEC/SCO/Fujitsu/
Hitachi, the UNIX desktop standard of the 1990s — was open-sourced in
2012 (LGPL-2.0) and is maintained today by the **CDE revival** project
(SourceForge `cdesktopenv`, active 2025-2026, C/C++, Motif/LessTif, X11).
It is a *complete desktop*, not a toolkit:

- **dtsession** — session manager (restores the desktop)
- **dtwm** — the window manager, including the **front panel**
- **dtfile** — the file manager (spatial desktop, actions, drag & drop)
- **dtterm** — terminal emulator
- **dtpad** — simple text editor
- **dtstyle / dtlogin / dtmail / dtcm / dtinfo / dtprintinfo /
  dtappbuilder / dtcreate / dtsearch / dtksh** — the wider suite
- **ToolTalk (ttsession)** — the IPC/action scripting substrate the
  desktop is built on (dtfile actions, drop zones, drag & drop)
- **Libraries**: libDtSvc, libDtHelp, libDtWidget, libDtTerm, libDtSearch,
  libDtPrint — on top of **libXm (Motif)** on **libXt (X intrinsics)**.

The appeal is the *integration*: front panel + file manager + session +
one coherent look, all designed to fit. That is the product unit FNX
wants — not a widget set.

> Component names above are from the public CDE layout; exact internal
> dependencies (especially how deeply ToolTalk is woven into dtfile/dtwm)
> must be **verified against the vendored source** in M0 before the
> amputation list is final.

## 2. Strategy

**Fork the proven whole; make it ours.** The house pattern (XBFS from
BFS, Xfb from Xvfb): vendor the upstream, mechanical copy, rename, shape
in place. CDE arrives whole — the hard parts (session, file manager,
Text, window manager, front panel) come pre-built and coherent — and we
modernize by shaping, not by inventing.

Two tracks inside the one fork:

- **Bring-up first (make it run), shape as we go (make it FNX).**
  Bring-up with *stock* Open Motif 2.3.x + stock Xt built in our X
  prefix (same class of work as the Xfb X-stack bring-up). Shaping —
  rename, colors/theme, `.conf` integration, and later the "modern
  Motif" agenda (UTF-8 text, theming, HiDPI) — comes after it runs.
- **Mine CDE revival's build fixes; never touch imake.** CDE's original
  build was imake; the revival project already modernized that for
  Linux/glibc. We take their *approach* (and where usable, their
  patches), expressed as **plain Makefiles** against our prefix, per the
  Xfb pattern. Their fixes target glibc/Linux — ours must be re-derived
  for musl/FNX, not copied verbatim.

## 3. The FNX substrate (what CDE will sit on)

- **Server**: Xfb (userland/xfb) on `/dev/fb0` — real X11, driven by
  real FNX input. X clients are unmodified X11 clients.
- **X stack** (.build/x11-prefix): libX11/libxcb/Xau/Xdmcp/pixman/xkb/
  fonts already vendored. **Missing for CDE: libXt, libXmu, libXtst/
  libXext (verify), and Open Motif (libXm) 2.3.x** — all addable to the
  same prefix with the same build recipes.
- **Config**: Xfb already takes config from `system.xfb` via libconfig
  (decided in x11-xvfb-fb-plan.md §Update). CDE's X resources
  (Xdefaults, dtwmrc, app-defaults) are the shaping target: over time,
  `.conf`-driven theming replaces the X-resource maze (the FNX-native
  answer to Motif's stringly-typed resources).
- **Paths**: FSH (/System/..., /Applications, /Users/Admin) and the `@`
  device shorthand — CDE's /usr/dt assumptions get remapped at the
  rename milestone.

## 4. Bring-up slice (what runs first)

The *minimum coherent desktop* on Xfb:

- dtsession (session) → dtwm + **front panel** → dtfile → dtterm →
  dtpad, plus the minimal libDtSvc/libDtWidget/libDtHelp surface they
  need.

**Deferred / amputated (initially not built):** dtlogin (FNX boots to a
logged-in session; no display manager needed), dtmail, dtcm, dtinfo,
dtprintinfo, dtappbuilder, dtcreate, dtsearch, dtksh, dtstyle-as-shipped
(theming moves to `.conf`).

**ToolTalk: investigate, don't assume.** If dtfile/dtwm actions and
drag-and-drop genuinely need ttsession, the options are (a) port
ttsession as-is, (b) stub it with an FNX-native action service behind
the same API, or (c) cut actions from v1 and add later. This is a
**verify-in-M0** item, not a day-one decision.

## 5. Identity milestones (the XBFS treatment)

1. **Run** — CDE desktop boots on Xfb in QEMU, classic look, unmodified
   upstream identity. Proof the build/stack works.
2. **Rename** — the fork gets an FNX name (decided later, like XBFS/
   FNX/Xfb naming), product strings/comments re-identified, attribution
   kept (LGPL-2.0 + upstream notices — the Fiwix→FNX license-attribution
   precedent applies).
3. **Theme** — FNX colors/bevels driven by `.conf` profiles (light/dark/
   high-contrast), replacing hardcoded X resources incrementally.
4. **Deep shaping** — FSH paths, `@` devices, FNX-native apps
   (Terminal/Editor/Settings/Installer/Disks from the desktop plan)
   built against the fork; then the "modern Motif" agenda (UTF-8 text
   pipeline, modern widgets, HiDPI) as the toolkit shaping milestones.

## 6. Doctrine guardrails

- FNX ships only apps built against this desktop/toolkit. The fork is
  the native toolkit; nothing else ships.
- No compatibility obligation: we do not preserve IRIX/HP-UX/CDE app
  compatibility (unlike MaXXdesktop, which must). XmString, ToolTalk
  semantics, and legacy APIs can die or change as shaping demands —
  nothing external depends on them.
- Users may run other X11/Motif things on Xfb; we neither ship nor
  support them.

## 7. Milestones (sketch — refined after M0 verifies the build)

- **M0 — Vendor + build recon.** Vendor CDE revival + Open Motif 2.3.x
  + libXt/libXmu into third_party; mine the revival build fixes; stand
  up plain-Makefile builds in the X prefix; map real internal deps
  (ToolTalk coupling, imake residue, musl friction). Acceptance: the
  CDE libraries compile for FNX; a component inventory with verified
  dependency edges replaces this sketch's assumptions.
- **M1 — Bring-up slice runs.** dtsession/dtwm/front panel/dtfile/
  dtterm/dtpad run on Xfb in QEMU, classic look, interactive
  (keyboard/mouse). Acceptance: boot → desktop → open dtterm → run a
  command; dtfile browses the FSH tree.
- **M2 — Rename + restyle.** FNX name/branding, FNX theme as default,
  attribution kept. Acceptance: boots as the FNX desktop; screenshot-
  verifiable identity change.
- **M3 — .conf theming + FSH.** Resources → `.conf` profiles; paths
  remapped to FSH; `@` devices usable from desktop apps. Acceptance:
  theme switch at runtime via `config`; file dialogs see FSH.
- **M4+ — Native apps + toolkit shaping.** FNX's own Terminal/Editor/
  Settings/Installer/Disks on the fork; "modern Motif" milestones
  (UTF-8 text first — the iceberg) proceed inside the fork.

## 8. Risks / de-risks

- **Build archaeology (imake→Make, glibc→musl)** — highest early risk;
  de-risked by mining the revival's Linux fixes and by M0's recon.
- **ToolTalk coupling** — could block the bring-up slice; verify in M0
  (see §4).
- **X-resource maze** — CDE config is pervasive; `.conf` migration is
  incremental by design, not a big-bang.
- **Upstream divergence** — fork stops tracking the revival once we
  shape; mitigation is the house norm: clean fork boundary, documented
  upstream, re-sync possible until divergence.
- **Scale** — CDE is ~2M lines; bring-up slice + amputation list keep
  "less work" honest; the whole-desktop iceberg is pre-built, which is
  the point of forking.

## 9. Open decisions (for the next conversation round)

- The FNX name for the fork (naming precedent: XBFS "the ex-Be").
- Bring-up slice boundaries (dtpad? dtstyle? how much of dtfile?).
- ToolTalk: port / stub-native / cut-from-v1 (after M0 evidence).
- Where the fork lives in the tree (third_party/cde? userland/cde?
  the Xfb/userland pattern).
- Stock-Open-Motif-first vs forking libXm at the same time (proposal:
  stock first, fork libXm as a shaping milestone — matches A-first in
  the Motif discussion).

## 10. Non-goals

- Not a widget-library project (the desktop is the product).
- No dtlogin/display-manager story (boot lands in a session).
- No compatibility with legacy CDE/IRIX apps or the wider X11 app
  bazaar (see §6).
- Not a from-scratch desktop: the fork is the point.
