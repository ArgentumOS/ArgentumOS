# PDFium — the PDF viewing door

Status: **PLAN (2026-09) — decided in direction; not scheduled.**
PDFium (Google/Chromium's PDF engine) as the system PDF renderer —
the only permissive full PDF viewer in the field (MuPDF/Poppler are
AGPL/GPL → out). This is the **single heaviest third-party library
adoption** outside the compiler itself; the plan says so plainly and
sizes accordingly.

## 1. Admission

- **License**: PDFium is BSD-3-Clause; its bundled third_party is
  permissive by Chromium policy (AGG = MIT, lcms2 = MIT, freetype/
  libjpeg/zlib overlap the tree) — verified at S0, nothing copyleft
  expected. Pin recorded at S0 with the manifest row.
- **The minimal profile is the point**: PDFium's embedded profile
  disables V8 (JavaScript) and XFA (forms) — the C++ render core
  only, exposing the C `FPDF_*` API. No JS engine, no Chromium
  runtime; this is the profile e-readers/embedded products use.
- **Toolchain**: C++ (libc++/libc++abi proven), clang — present.

## 2. The build truth (the honest part)

- PDFium builds with **GN + ninja**. ninja (Apache-2.0) is already
  in the roster (the LLVM executor). **GN (BSD-3-Clause)** is *new*
  — a manifest update at this decision (row added to
  self-hosting-packages.md §C/D): the generator for the PDFium build
  only, not a system tool. GN itself builds with clang++.
- Bundled-dep overlap is an audit item at S0, not an assumption:
  freetype/libjpeg/zlib exist in the tree but PDFium vendors its own
  — the plan does **not** promise dedup on day one; PDFium builds
  against its vendored set (Chromium policy), and using the system
  copies is a later optimization if the audit shows it is clean.
- **Sizing**: a many-thousand-file vendored tree; host build in the
  minutes-to-tens-of-minutes class. The long pole is the **on-FNX
  self-rebuild** (GN + the full build in-guest) — deferred with a
  trigger, not promised (P4).

## 3. Placement & consumers

- `libpdfium.so` → `/System/Libraries/`; headers consumed at build
  time from the pinned checkout (house rule, no global header tree).
- PDFium renders a page to an `FPDFBitmap` (BGRA) — the consumer is
  the same door the image libraries opened: the **Viewer app** opens
  PDFs by document type (page render into a UIKit image view; scroll/
  zoom later), print preview when printing lands.
- Text extraction (`FPDFText`) is recorded, not scheduled — viewing
  comes first; selection/search is a later track.

## 4. Milestones

### P0 — Admission + cross-seed build (host)
GN row lands in the manifest; host cross-build of the 3.4.x-pinned
minimal profile (no V8/XFA) against the musl sysroot → shared
`libpdfium.so`; S0 license/dep audit recorded. **Acceptance**: host
build links; a known PDF renders to a bitmap page.

### P1 — Stage into FSH + in-guest render smoke
**Acceptance (in-guest)**: a first-party probe renders a known
one-page PDF (shipped on the root image) to pixels and
pixel-verifies against a reference — the established verification
pattern; font fallback behavior recorded (the one audit unknown:
PDFium's font fallback without a fontconfig-style store, FNX's
fontconfig exists as the answer if needed).

### P2 — Viewer integration
The Viewer app opens PDFs by document type: page → bitmap → UIKit
image view, page-turn navigation. **Acceptance**: a multi-page PDF
renders page-accurately in the Viewer from the file manager.

### P3 — Text (recorded, trigger-gated)
`FPDFText` extraction for selection/search — only when a consumer
(selection in the Viewer, or a document search) demands it.

### P4 — On-FNX self-rebuild (deferred, trigger-gated)
GN + PDFium rebuild in-guest. Honest posture: the heaviest rebuild
in the manifest after LLVM itself; **not promised** — the trigger is
the self-hosting goal reaching the point where the OS rebuilds its
own document stack. Until then the pinned cross-seed is the shipped
artifact, like the earliest compiler stages.

## 5. Relationship & out of scope

- The image-library plan's consumers (Viewer, UIKit image views) are
  the same consumers PDFium serves — one document door, codecs and
  PDF side by side.
- Out (recorded): V8/XFA, JavaScript-in-PDF, form filling — and
  printing/export *to* PDF is **not** out of scope: that is the
  write side, a decided libharu adoption (docs/design/
  pdf-generation-plan.md) — PDFium is a reader; libharu is the
  paired writer.
