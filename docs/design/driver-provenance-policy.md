# Driver provenance and third-party notices — policy

Status: **DECIDED (2026-09, user).** Applies to every driver that reaches the
kernel from a BSD source, whether ported verbatim or written from one as a
template.

## The rule

Any BSD-derived driver — copied directly, **or** written as a reimplementation
informed by reading one — is treated as **obliging the notice**. Attribution is
carried in both cases, and the second case is the one this policy exists for.

## Why, and how this is stricter than the licences

BSD-2-Clause conditions the notice on *redistribution of the code*. A driver
written from a BSD driver as a template is arguably not a redistribution, so the
notice is arguably not required. The project applies it anyway, deliberately:

- **It removes an unprovable claim.** "We only read it" cannot be checked by
  anyone, including us. A carried notice is a fact; a clean-room assertion is an
  assurance that gets less credible as more ports land.
- **It keeps the derivation auditable** — the same reason the inherited
  Xfb/X11 files carry their notices today, and the same reason the Limine notice
  sits in the root `LICENSE`.
- **The cost is small and bounded**: a header block per file, and one notice text
  recorded once.

## The form — which is already this tree's convention

- **Per file:** the upstream copyright line, the full licence notice, and an
  `SPDX-License-Identifier:` naming the **upstream** identifier
  (`BSD-2-Clause`, `BSD-3-Clause`, `ISC`) — *not* `MIT` — with the file's own
  description of our changes. The in-tree example is
  `userland/xfb/os/strndup.c`; the Fiwix-heritage files show the other,
  thinner form (holder plus licence name, no text).
- **Once, centrally:** the full notice text in the root `LICENSE`, in the same
  block form as the notice already carried there for the Limine Project.
- **Binary redistribution:** clause 2 of the notice requires the notice in the
  documentation and/or other materials provided with the distribution. That
  means the notice must reach the **shipped image's documentation**
  (`/System/Documentation/`), not only the source tree — a source-tree notice
  alone does not satisfy it.

## What this does not change

- **Kernel heritage is untouched.** Changes to the kernel keep their existing
  copyright and licence status, in the root `LICENSE`'s own words. This policy
  adds attribution for *new* borrowings; it does not relabel Fiwix-derived files.
- **GPL sources stay reading material.** A Linux/GPL driver may be read for
  hardware facts — register layouts, init order, errata workarounds — but never
  copied, consistent with `docs/eval/drm-shim-eval.md`.
- **No gate is implied.** The rule is a review obligation, not a linter: this
  tree's rule is no new test tooling, and provenance is not mechanically
  checkable from the artifact anyway (that is the whole problem it solves).

## State

Nothing to apply yet. Verified 2026-09-28: **no BSD-derived driver exists in the
tree** — `drivers/` is FNX's own work plus the Fiwix heritage. The first ported
driver is what materialises the central notice; this record exists so that port
cannot land without one by accident.
