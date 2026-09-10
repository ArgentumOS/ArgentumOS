# PDF generation — libharu (the write side)

Status: **PLAN (2026-09) — decided in direction; not scheduled.**
libharu (zlib license) is the PDF **generation** door: document
save-as-PDF and the print spooler's output format. It pairs with
PDFium (docs/design/pdfium-plan.md) — PDFium reads, libharu writes;
together they cover the format. This is the write side PDFium
deliberately is not.

## 1. Admission

- **License**: libharu is zlib — admissible. **Role**: a *writer
  only* (it has no parser and no renderer; it creates PDFs from text,
  vector, and image placement calls — page *layout* is the caller's
  job, which suits the consumers below). Pin recorded at S0 with the
  manifest row.
- **Honest caveats**: libharu is dormant upstream (2.3.0-era lineage;
  the GitHub revival tracks maintenance) — acceptable for a stable,
  small, well-understood format writer, and the first-party escape
  hatch (a house writer for the same niche) stays the fallback if
  maintenance rot ever bites. Build: CMake route exists (house
  generator, autotools-free); deps zlib + libpng — both present.
- No new build-time tools → no manifest §C/§D update at this
  decision; the admission row lands in §B at S0 with the pin
  (standing policy), like the image libraries.

## 2. Consumers

- **Viewer/Editor "Save as PDF"** — document export goes through
  libharu.
- **Print spooler output** — when the printing plan lands, the
  spooler's page description renders to PDF via libharu (the
  spooler is thin; PDF is its interchange).
- Image-bearing PDFs use libpng/zlib paths libharu already has.

## 3. Milestones

### G0 — Cross-seed build (host)
CMake build of the pin against the musl sysroot → shared
`libhpdf.so` + headers. **Acceptance**: host build links; a
text-plus-image PDF generates byte-stable output.

### G1 — Stage into FSH + in-guest generation smoke
**Acceptance (in-guest)**: a first-party probe generates a known
PDF on FNX — and the pair verifies itself: the pdfium probe
(plan P1) **reads the generated PDF back and pixel-verifies it**.
libharu cannot parse its own output; PDFium is the check. No
external tooling, no network.

### G2 — Consumers
Viewer/Editor save-as-PDF wired; spooler output when printing lands.
**Acceptance**: a document round-trips — export to PDF, open in the
Viewer (PDFium), page-accurate.

## 4. Relationship & out of scope

- libharu = `/System/Libraries/` (+ build-time headers, house rule),
  alongside `libpdfium.so`: one format, a reader and a writer.
- Out (recorded): any rendering/parsing (PDFium's half), page-layout
  engines (layout is consumer work), and form/annotation generation
  unless a consumer appears.
