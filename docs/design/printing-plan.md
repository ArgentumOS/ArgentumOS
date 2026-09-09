# Printing — the thin spooler shape (CUPS rejected)

Status: **PLAN (2026-09) — decided in direction; no code.** FNX
printing = transport nodes (USB printer class / parallel — plans
exist) + a **thin lpr-flavored spooler** + apps that emit the
printer's language. **CUPS is considered and rejected** despite being
Apache-2.0: it is a print-*server* (cupsd scheduler, IPP over HTTP,
filter-chain, web UI, network/multi-user machinery) solving problems a
single-user personal OS structurally doesn't have — the privileged-
daemon-owns-everything shape this project rejects. `/dev/dsp` is the
model, not CUPS: the node, a tiny queue, apps that speak the device's
language.

## 1. What printing actually needs (and doesn't)

- **Needs**: (1) a transport — the USB (class 0x07) and parallel
  printer nodes, already planned, language-agnostic bulk/lpt writes;
  (2) job serialization — two apps must not interleave on one
  printer; (3) a producer story — getting app content into the
  printer's language.
- **The genuinely hard part is #3**, and it is *not* CUPS-shaped:
  printers speak PostScript / PCL / PDF-direct / PWG, and producing
  those needs renderers FNX doesn't have. The v1 boundary is "what
  does the producer emit?", not "what spools it."
- CUPS's real value (filter chain, PDF-as-interchange) is exactly the
  renderer machinery this plan does not adopt — a printer-language
  emitter is a later producer-side component, not a spooling
  problem.

## 2. Design

- **Transport**: printer nodes (USB class 0x07 `lp`-style node;
  parallel) — language-agnostic write of the printer's language.
- **Printer records**: a `.conf` domain — each printer names its
  node + language (e.g. `PCL`, `PostScript`, `raw`); the config tool
  edits it.
- **The spooler (thin, lpr-flavored)**: a `print`-analog command
  enqueues a file to a printer's spool (`/System/Variable Data/
  Printing/<printer>/`); a small dequeuer writes queued jobs to the
  node serially. Files, not a server — crash between enqueue and
  dequeue leaves the job in the spool (restart-safe, matching the
  journal culture).
- **A `file` printer target** — a first-class device kind that
  "prints" to a file — is the test vehicle: end-to-end jobs run with
  no hardware, and it doubles as PDF-to-file-style export later.
- **Producer side**: today, direct-format apps emit the printer's
  language to the spool themselves; a shared print-emit library (an
  API the toolkit's print dialog targets) is the later boundary.

## 3. Milestones

### P0 — Records + spools
Printer records domain (name, node, language); per-printer spool
directories; `print <file> <printer>` enqueues.
**Acceptance**: a printer record + the file target exist; enqueuing
places an intact job file in the spool; listing shows the queue.

### P1 — Dequeue + file target
The dequeuer serializes jobs: writes each to its target (the file
printer first), marks done, cleans up.
**Acceptance**: two jobs enqueued back-to-back print (to files)
serially and intact; kill-cycle mid-dequeue leaves the in-flight job
restartable without loss or duplication of the completed one.

### P2 — Real transports
USB class 0x07 and parallel nodes behind the spooler.
**Acceptance**: jobs reach a real/fixture printer node without
interleaving; a mid-job disconnect fails that job cleanly and the
queue continues; the USB/parallel plans' nodes are unchanged.

### P3 — Producer boundary
A shared print-emit API (or documented direct-emit contract) apps and
the toolkit's print dialog target.
**Acceptance**: a demo app emits a PCL/raw job through the API to the
spool and the file target receives it; language selection from the
printer record flows through.

## 3a. Producer endgame: any view emits PDF (desktop-era, folded 2026-09)

The toolkit's native answer to "how does app content become
printable": **any UIKit view can emit itself as a PDF**, Cocoa-style —
the drawing is *replayable*, so the toolkit renders it into a
PDF-backed GraphicsContext instead of pixman. This fits FNX unusually
well:

- The design-language chrome (rounded rects, linear/radial gradients,
  bevels, theme parameterized) **maps natively onto PDF primitives** —
  the drawing model *is* the document model; nothing rasterizes.
- **Text** is the one real cost: v1 draws glyphs as **vector paths**
  through the existing FreeType outline path (no font embedding);
  embedded subsets are the later step.
- **Images/offscreen content** embed as XObjects (Flate).
- The **PDF writer is a house component**: PDF 1.7 (objects, content
  streams, xref) is a spec, not code — a minimal writer is a small
  permissive first-party component (self-hosting manifest records the
  requirement); no CUPS/filter-chain machinery.

It resolves much of §1's "hard part": a view emitting PDF is exactly
what **PDF-direct/PWG printers** speak — emit → spool → the printer,
with no intermediate renderer. (PS/PCL-only printers still need a
later emitter — honest caveat.) Dependency chain: a backend-swappable
GraphicsContext (the S1.x GC design already is), FreeType outlines,
then the house PDF writer — a **desktop-era** feature and the natural
successor to §P3, not something to build before the toolkit work
reaches the print dialog.

## 4. Out of scope (recorded)

- A PS interpreter / PDF renderer / RIP (the producer-side hard part,
  later and separate — FNX has no document engine today); CUPS and
  its filter chain; IPP/LPD network printing *servers* (a network
  print client can be added to this spooler shape when real
  network printers appear — the dequeuer's target is just another
  transport).
