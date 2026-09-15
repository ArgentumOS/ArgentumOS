# Weaver IDE — growing the interface editor into the Argentum app IDE

Status: **DRAFT (2026-09), awaiting review.** Weaver today is a complete
interface editor (weaver-plan.md: IB0–IB7 plus chrome, OutlineView,
functional inspector, dirty title, multi-document, guides/snapping,
marquee multi-select, group move, crash journal, app-owned menubar, dock
pin). This plan grows it into the IDE for building Argentum applications,
one verifiable milestone at a time, under the same rules weaver-plan.md
used: every milestone gates on the editor's OWN logs and, where pixels
matter, on the committed pixel-gate pattern; nothing speculative ships.

The working order below is deliberate: a project model first, then code,
then build, then run — an IDE is those four verbs before it is anything
fancy, and each milestone is useful by itself.

## 1. Scope (what "full IDE" means here, and what it does not)

In scope:

- One application window with panes: **project navigator**, **editor**
  (source + the existing interface canvas), **inspector/outline**, and a
  **log/console** pane. Weaver's window already has three of those panes;
  the project navigator and console are the new ones.
- **Projects**: a directory with a libconfig manifest, sources,
  resources, and interface documents.
- **Code editing**: the existing TextView as the source editor, tabs for
  open files, dirty markers, save.
- **Build**: compile a project's sources with the LLVM-family toolchain
  wrappers the tree already ships (`tools/musl-clang++64.sh`), into the
  project's build directory, with the output captured in the console.
- **Run**: launch the built payload, stream stdout/stderr into the
  console pane, kill it, report the exit status.
- **Interface editing inside a project**: the existing Weaver canvas and
  property-table inspector, pointed at the project's interface
  documents, with outlets resolved the way Wren.app resolves them today.
- **Bundle + install**: produce `<App>.app` (manifest + bin + Resources)
  from a project, install it to `/Applications`, and export the AGFS
  package form the packaging plan already defines.
- **Crash/debug-lite**: the honest v1 is run + console + exit codes + a
  crash report pane (signal, faulting address, `addr2line` against the
  build), NOT a kernel or process debugger.

Out of scope (explicitly, so nobody re-derives them):

- A new text stack or a new widget toolkit — the IDE uses the existing
  Argentum UIKit widgets (TextView, OutlineView, TableView, TextField,
  Box, TabGroup as it lands).
- A kernel debugger (parked: kernel-debugger-eval) and any GDB/LLDB port.
- A scripting layer. Argentum app logic is C++ under the LLVM-family
  doctrine (toolchain-clang-doctrine.md); Wren.app is a C++ sample with
  outlets, not a runtime to embed.
- Git/SCM integration, refactoring, completion, or LSP in v1. Those are
  later milestones once the four verbs above are real.

## 2. Milestones

Each milestone is a gate: the acceptance column is what the new
`tests/cases/weaver_ideN.py` asserts, driven through the same scripted
`WEAVER: ...` log discipline (and real clicks only where the menubar or
a pane needs them, as weaver_menubar.py does).

### IDE0 — the project model (manifest + navigator)
- `ArgentumProject`: a libconfig manifest `<name>.argproj` (policy:
  libconfig everywhere for on-disk settings) carrying `name`,
  `identifier`, `type = window | console`, `sources[]`, `resources[]`,
  `interfaces[]`, `bundle` (name/identifier/version/executable).
- `--new-project <name>` creates the directory + manifest + a main.cpp
  template; `--open-project <path>` loads it.
- The window gains a **project navigator** pane (OutlineView of the
  tree, FSH-clean: Sources/, Resources/, Interfaces/).
- Gate: create, open, list, roundtrip the manifest (load/emit/load is
  byte-identical), and the navigator pane is drawn (pixel check).

### IDE1 — the source editor
- Open/save source files through the project navigator; tabs for open
  files; dirty title markers (reuse D14).
- TextView as the editor surface; no syntax features in v1 beyond "it is
  a real editor with selection and cursor" (TextView's existing
  behaviour).
- Gate: open main.cpp, edit it, save, md5 changes; dirty marker clears
  on save; two files = two tabs.

### IDE2 — build
- Build backend = a first-party runner inside Weaver that invokes
  `tools/musl-clang++64.sh` per the manifest's `sources[]` and links
  against `libargentum`/`libconfig`/X11, writing the payload to
  `<project>/.build/`. No dependency on `make` being present in the
  guest; the runner owns the file list and the ordering.
- Output (compiler stdout/stderr, success/failure) streams into the
  console pane; failures name file:line so a later click can jump.
- Gate: a console template builds clean; a deliberately broken source
  fails with the compiler's error in the log and the IDE stays alive.

### IDE3 — run + console
- Run the built payload as a child process; stream stdout/stderr into
  the console pane; Stop sends SIGTERM; exit status is reported.
- The console is a first-party line buffer (the Kestrel session codec
  discipline), not a terminal emulator — the terminal is libvterm's
  TerminalView and out of scope for v1.
- Gate: a hello template runs, prints, exits 0 (asserted from the
  console log); a sleeping template is stopped and the log says so.

### IDE4 — interface editing inside the project
- Opening an interface document from the navigator puts the EXISTING
  canvas + property inspector in the editor pane; `interfaceBuild`'s
  `onBuilt` binds outlets exactly as Wren.app does (identifier
  resolution), so the project's app resolves them at runtime.
- Resources resolve from the project's Resources/ (bundleResourcesPath
  discipline).
- Gate: edit a project interface, save, roundtrip; the built app logs
  its outlets resolved.

### IDE5 — bundle + install
- `--build-bundle` assembles `<App>.app` (manifest generated from the
  project's `bundle` record + built payload + Resources + interface
  documents), installs to `/Applications`, and exports the AGFS `.pkg`
  form (package-format-agfs-image.md: modes-only, digest + detached
  sig, refuse-when-lossy).
- Gate: a window template builds, bundles, installs, and launches from
  the dock's bundle resolution (the wm_dock pattern).

### IDE6 — crash/debug-lite
- The console pane reports signals, exit codes, and a crash report:
  signal name, faulting address, and `addr2line` output against the
  project build when the payload dies abnormally.
- No breakpoints/stepping in v1 (out of scope above).
- Gate: a template that dereferences null reports SIGSEGV + a resolved
  source line; the IDE survives and stays responsive.

### IDE7 — templates + the self-host gate
- `--new-project` ships the two templates (console, window-with-
  interface) plus a bundle template; the templates are the same ones
  the earlier milestones built and ran.
- Self-host acceptance (the project's version of a whole-S gate): from
  an empty project, create → edit → build → run → bundle → install,
  all in one scripted run with no manual steps.
- Gate: the full chain green, one run.

## 3. Decisions (made here; land with the milestone that needs them)

- **D1 — one app.** Weaver grows in place; no new application, no
  rebrand. The dock pin and menubar already exist.
- **D2 — the manifest is libconfig.** All shipped software uses
  libconfig (libconfig-all-config-policy.md); the project manifest is a
  config domain file, read with the system→user→shared precedence the
  rest of the tree uses.
- **D3 — C++ only.** App logic is C++ under the LLVM-family doctrine;
  no scripting layer is introduced by this plan.
- **D4 — the code editor is TextView.** No new text stack; syntax
  highlighting/completion are later milestones, not prerequisites.
- **D5 — build is a first-party runner.** The runner invokes the
  existing musl-clang wrappers; it does not require `make` in the guest
  and owns sources/ordering/output capture.
- **D6 — debug-lite, not a debugger.** Run/console/exit/crash-report
  only; a kernel debugger or an LLDB port is explicitly out of scope
  for this plan.
- **D7 — packaging reuses the AGFS package plan.** `.pkg` export follows
  package-format-agfs-image.md as-is, including digest + detached sig
  and refuse-when-lossy semantics.

## 4. Open questions (to answer before the milestone that needs them)

- **Q-IDE2 — compiler backend in the guest.** Confirm which toolchain
  wrapper is the guest-canonical one for project builds (musl-clang
  vs musl-g++ and libc++ availability in the root image), and pin it in
  IDE2 so the runner and the gate share one path.
- **Q-IDE3 — process lifecycle.** Confirm the child-process/pipe story
  for a GUI parent (Weaver) running a GUI child (the payload): does the
  child inherit the display socket from Weaver's environment, and is
  stdout capture line-buffered enough for the console pane. Decided at
  IDE3; the fallback is a pty via the existing devpts.
- **Q-IDE5 — signing/blessing on install.** Installing to /Applications
  touches the bundle-signing/blessing rules (bundle-signing-supported-
  not-required.md). Confirm whether an IDE-built bundle needs the same
  one-time run blessing as a shipped one; the gate must assert the
  answer, not skip it.

## 5. Status bookkeeping

- IDE0–IDE7: not started.
- Resume here after review; each milestone marks itself DONE in this
  file the same way weaver-plan.md does, with the commit and the gate
  count.
