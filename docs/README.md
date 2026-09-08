# docs — design, evaluation, reference, archive

Documentation for FNX is split by purpose. Every document records its own
status in its header (`DECIDED`, `PLAN`, `EVALUATION`, `DONE`, ...); the
directories below are only the first cut.

- `design/` — chosen directions, design records, and active plans
  (including the milestone-tracking plans for the toolchain, shared
  libraries, system config, X11, the Argentum GUI toolkit, and the
  Argentum Shell).

Branding follows the monobrand (docs/reference/os-profile.md):
**Argentum** names the layers — the Argentum OS, the Argentum Desktop,
the Argentum Design Language, the Argentum kernel, the Argentum UIKit,
the Argentum Shell — while the identifiers name the machinery and are
unchanged (FNX, FSH, AGFS, Xfb, `finch`).
- `eval/` — feasibility evaluations and candidate surveys that did not
  (yet) become decisions: SSE/FPU, SMP, GPU accel, kernel debugger, and
  the GUI-candidate documents.
- `reference/` — factual records of what FNX is / how it is built today:
  hardware + device allocation tables, kernel parameters, boot records,
  implemented specs (AGFS journal, partition support, devfs topology),
  the OS profile, how to build and run the OS (`reference/building.md`),
  and notes that are historical but still consulted.
- `archive/` — superseded, rejected, failed, or fully-completed plans
  kept for the record (the GUI-toolkit lineage before Argentum, the FLTK
  failure, the pre-rename TODO list, ...).
- `history/` — root-level project history moved here in the tree
  reorganization: `Changes` (the changelog), `CODING`, `THANKS`.

## The active design set (short version)

| doc | status |
|---|---|
| `design/argentum-uikit-plan.md` | DECIDED — the GUI toolkit direction |
| `design/x11-xvfb-fb-plan.md` | active X11-on-FNX plan (Xfb implemented to M3) |
| `design/llvm-clang-toolchain-plan.md` | one compiler = clang; M0-M4 DONE |
| `design/shared-libraries-plan.md` | dynamic-linking doctrine; M0-M4 DONE |
| `design/fsh-proposal.md` | the filesystem hierarchy (FSH) |
| `design/system-config-files-plan.md` | libconfig `.conf` domains (M0-M7 DONE) |
| `design/permissions-acl.md` | POSIX ACLs as the canonical model |
| `design/agfs-enhancements.md` | AGFS home doc (SSD suitability, live directories, integrity, ...) |
| `design/finch-shell-plan.md` | PLAN — the Argentum Shell (code name `finch`; from-scratch; dash stays `/bin/sh`) |

New design work goes under `design/`; feasibility studies under `eval/`;
when a plan is superseded or abandoned it moves to `archive/` (git mv, so
history is preserved).
