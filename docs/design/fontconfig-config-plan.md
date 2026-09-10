# fontconfig configuration → libconfig domain plan

Status: M0 DONE (committed): default config loads from the libconfig
domain (system.fonts.conf → fclibconf.c), no XML fontconfig file ships;
guest text_pipeline green. M1 (full match/test/edit DSL) is next.
Decisions 2026-09: patch fontconfig to read the domain directly, no XML
on FNX; full rule DSL domain-expressible; policy recorded in
docs/design/config-design.md §0.

## 1. Goal

fontconfig is the first adopted third-party lib whose config surface must
conform to the FNX config-language policy (config-design.md §0: all
software with config files uses libconfig). Its XML config (`fonts.conf`,
the `fcxml.c` front-end, the compiled-in default load in `fcinit.c`) is
replaced on FNX by a libconfig **domain** — `/System/Configuration/
system.fonts.conf` — that fontconfig itself reads at init. No XML config
file exists on FNX; the XML *parser* (`fcxml.c`) stays compiled in for the
public `FcConfigParseAndLoad(path)` compatibility API only (nothing on FNX
feeds it).

Scope decisions (user, 2026-09):
- **Patch the consumer** (musl-pwconf precedent): the default-config load
  path builds the `FcConfig` from the domain, no generated XML.
- **Full rule DSL**: every live element of `fonts.dtd` (the `fcxml.c`
  grammar — see the DSL table below) is expressible in the domain, not
  just dirs/cachedir.
- Fontconfig keeps working unchanged for any future consumer that passes
  an explicit XML path via `FONTCONFIG_FILE` or `FcConfigParseAndLoad`.

## 2. Background: the XML surface being replaced

fontconfig builds one `FcConfig` object model (fcint.h `struct _FcConfig`,
fccfg.c) from XML via `fcxml.c` (SAX). The live elements, mapped to what a
replacement front-end must populate (from the `fonts.dtd`/`fcxml.c`
walk, all file:line in the vendored `third_party/x11/fontconfig`):

| XML element | FcConfig effect | loader must |
|---|---|---|
| `dir` (+`prefix`) | `config->fontDirs` triple (d\|map\|salt) | `FcConfigAddFontDir`/`FcConfigResetFontDirs` |
| `cachedir` | `config->cacheDirs` | `FcConfigAddCacheDir` |
| `include` (`ignore_missing`, `prefix=xdg`) | flush ruleset → `config->subst[kind]`; recursive parse; `deprecated` migration | (FNX: no conf.d split — see §4 rules) |
| `config/rescan` | `config->rescanInterval` | set int |
| `description` | ruleset name/description/domain | `FcRuleSetAddDescription` |
| `remap-dir` | fontDirs triple | `FcConfigAddFontDir` |
| `reset-dirs` | clears fontDirs | `FcConfigResetFontDirs` |
| `match` (target pattern/font/scan) | `FcRule` chain per kind into `config->subst[kind]` + `config->maxObjects` | `FcRuleCreate` chain + `FcRuleSetAdd(ruleset, rule, kind)` |
| `test` (qual/name/compare/ignore-blanks) | `FcTest` (kind/qual/object/op/expr) | `FcTestCreate` (+ `FcObjectFromName`, op table, `FcOpFlagIgnoreBlanks`) |
| `edit` (name/mode/binding) | `FcEdit` (object/op/expr/binding) | `FcEditCreate` + mode/binding op tables |
| `alias` (+family/prefer/accept/default) | synthetic test + edits on FC_FAMILY → pattern ruleset | transcribed same as match/test/edit |
| `selectfont/acceptfont/rejectfont` (+glob/pattern/patelt) | accept/reject globs + patterns | `FcConfigGlobAdd`/`FcConfigPatternsAdd` |
| value elems (int/double/string/bool/const, matrix/range/charset/langset/name) + expr ops (or/and/eq/…/if/…) | `FcExpr` trees | the expression encoding (§5) |

Dead XML dropped: `blank` and `cache` (parser ignores/discards them).
Preserved parser quirks: `test target="scan"`, `dir`/`remap-dir` `salt`,
`alias` order-insensitivity.

fcxml's external deps are only the SAX parser + fontconfig-internal
builders (all `FcPrivate` via fcint.h) — a libconfig front-end reuses the
same builders, so no public API changes.

## 3. The domain

File: `/System/Configuration/system.fonts.conf` (staged from
`userland/configuration/system.fonts.conf`, edited by the `config` tool
like every other `system.*` domain). First-party infra domain, flat
dot-key map per config-design §10/§10.1.

```conf
# system.fonts — fontconfig configuration (fontconfig-config-plan)
dirs = /System/Shared/Fonts, /Shared/Fonts        # FSH dirs, absolute
cachedir = "/System/Variable Data/fontconfig"
rescan = 30
accept = [ "/System/Shared/Fonts/*" ]            # selectfont globs
# rules: ordered match/test/edit chain (conf.d equivalent), the
# anonymous-array-of-records spelling (config-design §10.2):
rules = [
  { match = pattern
    tests = [ { object = family; compare = eq; value = "Sans" } ]
    edits = [ { object = family; mode = assign; value = "DejaVu Sans" } ] }
]
```

Conventions: FNX font dirs are absolute FSH paths — the XML `prefix`
(relative/xdg/cwd) machinery is meaningless here and not carried; a `dir`
is a plain path string. `cachedir` likewise. Rule composition is **one
ordered `rules` list in the domain** — FNX ships no `conf.d` split; the
numeric-prefix file ordering that conf.d provided is expressed by the
array order (0,1,2,…). `include` of other fontconfig files is dropped
(there are no other files on FNX; the domain is the single source).

## 4. Loader architecture (`fclibconf.c`)

New front-end in the vendored tree (`src/fclibconf.c`, mirroring
`src/fcxml.c`'s role), built unconditionally:

- Reads `/System/Configuration/system.fonts.conf` with the FNX libconfig
  C API (`userland/libconfig.h` + `libconfig.so.1` — already shared,
  staged in `/System/Libraries`; fontconfig's link gains `-lconfig` and
  `libfontconfig.so.1` NEEDED `libconfig.so.1`).
- Walks the parsed **tree** (config-design §10.2: records + arrays of
  records, addressed by dot/bracket keys such as `rules[0].edits[1]`)
  and drives the **same internal builders** fcxml uses (the §2 table's
  loader column), so `FcConfigGetFontDirs`, `FcConfigSubstitute`,
  `FcConfigAcceptFont`, `FcConfigGetCacheDirs`, `FcConfigFileInfoIter*`
  all behave as before (ruleset descriptions derive from each `rules`
  array element).
- Wired at the default-config load: `FcInitLoadOwnConfig` (fcinit.c)
  calls `FcConfigParseAndLoad(config, 0, complain)`. The patch makes that
  `file == 0` branch load the domain via fclibconf when no
  `FONTCONFIG_FILE` is set (FONTCONFIG_FILE keeps the XML path as the
  escape hatch); the `FcInitFallbackConfig` compiled-in XML string is
  likewise replaced by a domain-only fallback (empty config + FSH dirs
  default). `FcConfigParseAndLoad`/`FromMemory` with explicit XML stay on
  fcxml (public API compat; nothing on FNX calls them).
- fcxml + expat remain linked (their symbols are part of the shared lib
  surface) but the FNX default never reaches them.

Expression encoding (§2's expr ops): each edit/test `value` is either a
scalar or record (a record = an `op` block spelling the operator):

```conf
rules = [ { edits = [ { object = size
                       value = { op = plus; lhs = size; rhs = 2 } } ] } ]
```

`op` = the XML operator set (or and eq not_eq less … if … round), `lhs`/
`rhs`/`then`/`else`/`cond` recurse to the same value spelling (constants
may be names — `FcVStackName`/`FcObjectFromName`-typed — when the target
kind allows). matrix/range/charset/langset have block forms (`{ matrix =
a, b, c, d }`, `{ range = 12, 24 }`, …). `const` resolves via
`FcNameGetConstant` as in fcxml. The grammar is deliberately the flat
dot-key + block-spelling of config-design §10 — no libconfig changes.

## 5. Milestones

- **M0 — plumbing**: fclibconf skeleton reading `dirs`/`cachedir`/
  `rescan` (+ `accept`/`reject` globs) → real `FcConfig`; default-load
  patch + domain fallback; drop the XML staging + the FSH
  `/System/Configuration/fonts/fonts.conf` file + its `fonts.conf`
  build/install handling; libfontconfig NEEDED libconfig.so.1; guest
  `text_pipeline` + fc match green (proves the init path works with the
  domain).
  *Acceptance:* no XML fontconfig file on FNX; guest pipeline OK.
- **M1 — full DSL**: match/test/edit + expr trees + alias +
  selectfont/pattern transcription (the whole §2 table) in the domain;
  fclibconf covers every rule shape.
  *Acceptance:* a domain exercising every element parses + applies;
  `FcConfigSubstitute` behavior matches the XML equivalent (transcribed
  upstream conf.d rules → identical match output in-guest).
- **M2 — equivalence corpus + regression**: transcribe fontconfig's own
  test configs (test/*.conf) + the shipped `fonts.conf.in` rules into
  the domain; run the pipeline + fc behaviors; full acceptance matrix +
  fshlint + recovery; docs current.
- **M3 — follow-ups** (if ever needed): keep `fcxml`/expat or excise
  (currently kept for API compat); Xft-era rule needs extend the
  domain.

Commit per milestone; `tools/x11-shared-build.sh` fontconfig block gains
the fclibconf build (it is in-tree source) + the `-lconfig` link.

## 6. Risks / notes

- `FcConfigFileInfoIter*`/fc-conflist provenance (file names) becomes
  `system.fonts.conf` + one ruleset per `rules` array element (name =
  its index, description from an optional per-rule `description` key) so
  the public file-info API stays meaningful; the stock fc-conflist
  binary itself is not shipped (a domain-aware config view is future
  tooling).
- Per-read flat-map walk at `FcInit` only — negligible cost.
- libconfig is first-party MIT; linking it into vendored fontconfig keeps
  the permissive roof (no new license surface).
- The kernel.conf parser is unrelated (flat subset, boot-time); this
  touches userland config only.
