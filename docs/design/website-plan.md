# Argentum website

Status: **PLAN (2026-09) — decided in direction; not scheduled.**
Milestones U0–U4. A static site for the project: browsable
documentation plus the usual hobby-OS content. No server-side runtime,
no JavaScript requirement, no GPL tools, no CDNs or trackers.

## 1. Principles

- **Static output** — HTML files plus assets, deployable anywhere.
- **Readable without JavaScript.** Scripts are progressive enhancement
  only; the build enforces it (§5).
- **Permissive tooling only** — the same roof as the OS. That is what
  rules out pandoc (GPL) and the GNU highlighters (GPL).
- **No CDNs, no analytics, no trackers** — the site works offline and
  tells nobody who read it.
- **Reproducible from the repo**: `make site`, same inputs → same
  output, no "generated at" timestamps.
- **Single source of truth for documentation**: the site renders the
  *same Markdown* that feeds the in-OS `/System/Documentation` volume
  (`uikit-documentation-plan.md`). It never carries a copy of a doc.
- **Published scope: user-facing documentation only.** The
  `docs/design`, `docs/eval`, `docs/reference`, `archive` and `history`
  corpora stay in the repository. Pages may *derive* summaries from them
  (the roadmap, §3.7) but never republish their content.

## 2. Stack (decided)

| Role | Choice | Licence |
|---|---|---|
| Markdown engine | **lowdown** — zero dependencies, **built-in templating** and metadata | **ISC** |
| (fallback) | **cmark-gfm** (GH-flavoured tables/task-lists/autolinks) if lowdown's templating proves limiting | BSD-2 |
| Generator | **first-party thin generator** in-repo: walk sources → template → HTML; emits index pages, `sitemap.xml`, `robots.txt`, `atom.xml` | ours |
| Syntax highlighting | **tree-sitter** + C/C++/shell grammars at **build time** → static token spans, zero runtime JS | MIT |
| Search | build-time **alphabetical/full-text index page** in plain HTML (no JS); optional `lunr.js` enhancement later | MIT |
| Theme | first-party CSS implementing the **Argentum Design Language** tokens (rounded-rect chrome, accent outline) | ours |

**Rejected, with reasons recorded**: pandoc (GPL), GNU
source-highlight and highlight (GPL), pygments (needs Python —
excluded by the roster), hugo (permissive but a foreign Go toolchain and
templating language), zola/mdBook (Rust; mdBook is also MPL-2.0).
Tooling rows are recorded in `self-hosting-packages.md` per the standing
policy, marked **host-side** — the OS itself reads Markdown directly and
needs none of it.

## 3. Information architecture

Ordered by what hobby-OS sites most consistently carry (surveyed
2026-09: SerenityOS, Haiku, ToaruOS, Redox, MenuetOS), mapped to
content this project can actually fill.

1. **Home** — one paragraph on what Argentum is, one screenshot, a
   download call to action, and the architecture in a line.
2. **Download / Get Argentum** — the build-from-source path (toolchain
   prerequisites, `make` targets), a **QEMU quickstart** (the dev loop
   everyone actually uses), **checksums and the honest signing story**
   (self-signed keys; "signing is supported, never required" per
   `bundle-signing-plan.md`), hardware requirements, and an explicit
   statement about prebuilt images (present or not — no dead links).
3. **Documentation** (browsable, and the point of the site):
   Getting Started; Guides (custom views, layout, drawing and damage,
   theming, text, accessibility); the **generated UIKit API reference**
   from the `clang-doc` pipeline; the **Glossary**; the **FAQ**.
4. **Hardware support** — a **generated matrix** from an in-repo table
   source: storage (ATA/AHCI/NVMe/pvscsi/floppy), NICs (the full QEMU
   set plus XHCI USB-Net), audio (sb16/gus/es1370/ac97/HDA/virtio-snd),
   USB HCDs (UHCI/EHCI/OHCI/XHCI), input (PS/2 + USB), display
   (GOP-only today — stated plainly, with the plan linked).
5. **Screenshots / Gallery** — captured with QEMU `screendump` (the
   project's own verification pattern), each image stamped with the
   commit it came from, **regenerated per release** (§5).
6. **News / Devlog** — hand-written Markdown posts in-repo, with a
   generated **Atom feed**.
7. **Roadmap / Status** — **derived at build time** from the plan
   corpus: title + `Status:` line + a one-line summary per plan. This
   publishes *nothing* of the design docs' content, only a summary table
   — and it is the page that keeps the site honest about what exists
   versus what is planned.
8. **Architecture** — the layered story (kernel, AGFS, Xfb/Kestrel/
   Argentum UIKit, tools, self-hosting), linking into the docs pages.
9. **Community** — where the code lives; the mailing-list/forum choice
   is open (§7). No Discord, no trackers, by principle.
10. **Contributing** — the permissive roof (no GPL/LGPL), the adoption
    ritual (the manifest admission line), the gates, and the
    from-scratch ethos.
11. **Legal / Credits** — **the lineage note is required**: derived
    from Fiwix (Jordi Sanfeliu, MIT) with attribution kept, plus adopted
    packages and their licences, and an explanation of the permissive
    roof. It also states the **documentation licence: CC BY 4.0** for
    prose (`docs/LICENSE`), with **code samples under the software
    licence (MIT)** — the page renders that statement rather than
    restating it, and the site footer carries the attribution, which is
    also what CC BY requires.
12. **About** — the short project story.

## 4. Pipeline

```
make site  →  site/

inputs :  docs/uikit/**            hand-written guides, index, glossary
          .build/uikit-doc/**      generated UIKit reference (clang-doc; built first)
          pages/**                 site-only content (home, download, news)
          templates/**             page templates (lowdown templating)
          assets/**                CSS, images, tree-sitter output css
outputs:  HTML tree + sitemap.xml + robots.txt + atom.xml
          + roadmap.html (derived) + hardware.html (generated)
```

The generated reference is the **same artifact** the in-OS volume uses —
built once, rendered twice — so the site and the machine can never
disagree.

### 4.1 Repository topology and deployment (GitHub Pages)

**Hosting is decided: GitHub Pages, in a dedicated repository that
does not exist yet** — creating it is the first step of U4.

**Mode: publish from a branch** (the usual repository method). The
committed output *is* the site, so the repository holds:

```
site repo /
    .nojekyll              disables Jekyll processing (and preserves _-prefixed paths)
    LICENSE                generator/templates under the software licence;
                           prose inherits CC BY 4.0 from docs/LICENSE
    Makefile               builds the site (calls the generator)
    generator/             the first-party thin generator (+ templates/)
    pages/                 site-only content: home, download, news posts
    assets/                CSS (Design Language tokens), images
    os/                    THE OS REPO AS A SUBMODULE, pinned to a commit
    <built output>         index.html, docs/, sitemap.xml, robots.txt, atom.xml, …
```

The **submodule pin is the reproducibility anchor**: it fixes the exact
OS commit the site documents, which is also the commit stamped on the
screenshots and the source of the hardware matrix and roadmap.

**The rule inversion, stated deliberately**: "generated output is
never committed" applies to **the OS repository** (drift risk). In the
*site* repository, committing output **is** the deployment mechanism —
and it is safe precisely because the build is reproducible (same
submodule pin + same tool versions → identical bytes), which makes a
Pages update a reviewable diff rather than an opaque blob.

**The base-path trap** (the one thing that breaks every link): the
repository's *name* decides the URL. A `<org>.github.io` repo is served
at the root (`https://<org>.github.io/`); any other name is served
under a prefix (`https://<org>.github.io/<repo>/`). So the generator
must emit **relative links** (or take a single base-URL setting), with
`sitemap.xml` and `atom.xml` taking absolute URLs from that setting —
and the link checker must run against the **deployed base path**, not
just a local file tree.

**Pages mechanics that constrain the build** (verified 2026-09):
- branch publishing serves `/` or `/docs` on a chosen branch;
- **symlinks are not supported** by branch publishing (a site tree
  containing one requires the Actions mode) — so the build output must
  contain no symlinks, which is a build rule;
- **a `CNAME` file does not itself set a custom domain** — that is
  configured in repository settings/API when a domain is adopted;
- HTTPS is automatic; `robots.txt`/`sitemap.xml` are ordinary files.

**Recorded alternative**: publish via **GitHub Actions** (build in CI →
upload artifact → deploy). Needed if we ever require the submodule's
*contents* at build time (branch publishing serves only committed
files, with no build step) or decide against committing output. Costs: a
CI build step, pinned tool versions in the workflow, and — for
tree-sitter grammars — a Node step.

**Prerequisite to settle at first deploy**: where the OS repository is
hosted, since the submodule needs a URL. If it is not published yet,
the URL is a placeholder in the site repo until it is.

## 5. Verification (anti-rot, in the project's own style)

- **Link check** in the build: an unresolvable internal link fails it.
- **No-JS rule enforced mechanically**: a build step asserts content
  pages contain no `<script>` (optional enhancements must be explicitly
  marked and must degrade).
- **Reproducibility**: no build timestamps; dates come from content
  front-matter, so the same inputs give byte-identical output.
- **Docs coverage**: every user-facing doc either has a page or is on an
  **explicit exclusion list** — which is where the design/eval corpora
  are recorded, so "not published" is a decision, not an oversight.
- **Screenshot freshness**: every image records its commit; regenerating
  them is a release-checklist item.
- **Asset licence check**: vendored CSS/JS/fonts and tree-sitter
  grammars must be permissive and attributed.
- **Licence-notice check**: every published page carries the footer
  attribution (CC BY 4.0 needs attribution, so the build should not be
  able to publish a page without it); the site repository carries its
  own `LICENSE` (generator/templates under the software licence, prose
  inheriting CC BY 4.0 from `docs/LICENSE`).

## 6. Milestones

### U0 — Engine and generator spike
Vendor lowdown host-side; one real doc rendered through a template;
wire the no-JS assertion and the link check. **Acceptance**: `make site`
produces a legible page from a real doc with zero scripts and zero
broken links; the engine choice (lowdown vs cmark-gfm) is recorded with
the reason.

### U1 — IA skeleton and docs rendering
Every §3 section present, with the UIKit reference rendered from
clang-doc output and index pages generated. **Acceptance**: every
user-facing doc has a page; reference pages carry the mandatory
sections from the docs plan; the exclusion list is recorded.

### U2 — The pages only this project can have
Hardware matrix (generated), roadmap (derived from `Status:` lines),
the screenshot pipeline (QEMU screendump + commit stamps), glossary.
**Acceptance**: the matrix and roadmap regenerate from the repo with no
hand edits; a new plan doc appears on the roadmap without touching the
site.

### U3 — Download and news
Build-from-source instructions, QEMU quickstart, checksums/signing text,
Atom feed, devlog posts. **Acceptance**: a fresh clone reaches a running
system under QEMU using only the site's instructions (the honest test);
the feed validates.

### U4 — Publish
Create the site repository (**it does not exist yet**), add the OS repo
as a pinned submodule, configure the base URL for the eventual
repository name, build, commit the output, enable Pages (branch
publishing, `.nojekyll` present), and verify **on the live URL**.
**Acceptance**: the site is live and fully readable with JavaScript
disabled; the link checker passes against the deployed base path; TLS
works; `sitemap.xml`/`atom.xml` carry correct absolute URLs.

## 7. Open decisions

- **Repository name** — decides the URL (root vs `/prefix/`) and is
  therefore a **build-config input** (§4.1), not a cosmetic choice.
- **Custom domain** — optional later; note it is configured in
  repository settings, not by committing a `CNAME` file.
- **Publishing mode** — branch publishing with committed output is the
  default (`§4.1`); Actions is the recorded alternative if we need the
  submodule's contents at build time or stop committing output.
- **Documentation licence (decided)**: **CC BY 4.0** for prose
  (`docs/LICENSE`), **MIT** for code samples and all source. The
  alternatives considered and rejected: CC0/MIT-0 (permissive, but
  drops the attribution the project's lineage notes depend on),
  CC BY-SA and the GFDL (copyleft), CC BY-NC\*/ND\* (not permissive).
- **Community channel** — mailing list, forum, or none (with the
  no-tracker/no-analytics policy fixed).
- **Optional search** — `lunr.js` (MIT) as a progressive enhancement,
  on top of the no-JS index pages. Later, never required.
- **Roadmap page** — kept because it is derived, not published; drop it
  if the "user-facing only" rule is read to exclude it.
- **Dogfood goal (recorded, long-term)**: serving this site **from
  Argentum itself**. That needs an HTTP server and the network tier
  that the gap analysis records as unplanned, so it is a fine future
  milestone and not a prerequisite.

## 8. Relationship

`uikit-documentation-plan.md` — same Markdown, same clang-doc output;
the site is its second renderer. `self-hosting-packages.md` — lowdown
and tree-sitter recorded as **host-side** tooling. `bundle-signing-plan.md`
and `package-format-plan.md` — the download page's checksum and signing
honesty. `docs/README.md` — the repository index, distinct from the
site's navigation. `os-profile.md` — the site's theme adopts the
Argentum Design Language tokens, so the brand is consistent from
DESIGN.md to CSS.
