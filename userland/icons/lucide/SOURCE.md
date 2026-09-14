# Lucide (the system icon set)

Source: the `lucide-static` npm package, **v1.46.0** (`lucide-icons/lucide`),
**ISC licence** — see `LICENSE` beside this file.

Adopted as *data*: `2102` SVGs, 24x24 stroke geometry, `currentColor`, no
library. Nothing links them and nothing parses them on-FNX: the OS reads
**pre-rendered BMPs** generated from these files ahead of time
(`docs/design/workspace-plan.md`, decision D14), so there is no SVG parser
and no raster decoder in the runtime.

Regenerating the rasters = re-run that host-side step against this pinned
version. Do not edit these files in place; change the version and re-derive.

Recorded in `docs/design/self-hosting-packages.md` section 6 (standing
policy: every adoption updates that manifest).
