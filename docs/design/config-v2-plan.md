# libconfig v2 — tree values, arrays of records, additive merging

Status: M0–M2 DONE (bb5c394, 74985f5, d8fc0b1/f486a9a); M3 consumer
regression green (host probes + CLI suites + fshlint 0 + guest boot
matrix: config CLI, pwconf config_m3_test, toybox account tools, init
mounts/procfs on the refreshed rootxbfs image); M3 docs current
(config-design §11). Normative grammar: docs/design/config-design.md
§10/§10.1 v2 (committed 57a9e8b). This plan moves `userland/libconfig.c`
(+ header, `config` CLI, corpus) onto that model. fontconfig's domain
loader (docs/design/fontconfig-config-plan.md) is the first consumer of
the v2 tree API and starts after CV2-M3 (the next milestone).

## 1. Goal

config-design §10.2 (v2) made records first-class values and arrays able
to hold records, added bracket key segments (`rules[0].edits[1]`), and
made **arrays additive across scopes** (effective = concatenation in
precedence order system → user → shared). Today libconfig.c parses to a
**flat dot-key entry list** + a separate explicit-block-name list (the
§10.1 writer-spelling hack); that model cannot represent the v2 grammar,
so the parser/store/reader/writer/CLI move to a **tree** model.

The v2 change is a grammar *superset*: every v1 file parses identically
(flat lines, blocks as today, comma arrays) and reads through the same
flat dot-key API — so all existing consumers and on-disk domains are
drop-in. The observable deltas are new capability, plus one behavioral
change to call out: multi-scope ARRAY keys now concatenate instead of
whole-value shadowing (§5 v2).

## 2. Current state (inventory)

- `userland/libconfig.c` (2329 lines, single file, MIT): line parser
  (`parse_conf` → flat `struct entry` list), value inference, flat
  dot-key readers (`config_read*`/`config_get_*`), prefix reads
  (`config_get_all`), explicit-block tracking for the nested writer
  spelling, atomic write helpers, scope resolution (system → user →
  shared).
- `userland/libconfig.h` (207 lines): types `CONFIG_TYPE_STRING/BOOL/
  INT/FLOAT/ARRAY`; `config_value_t` union; read/write/record APIs.
- `userland/tools/config.c` (797 lines): CLI verbs read/write (+
  `-type`), display of scalars + scalar arrays.
- Built: `.build/fnxlib/libconfig.so.1` (shared, staged in
  /System/Libraries) + `libconfig.a` (static carve-out consumers) —
  mk/00-base.mk:168-174, mk/20-userland.mk:31.
- Consumers: musl `pwconf`/`hosts` renderers, toybox account tools, Xfb
  configargs, init (mounts/hostname), the `config` tool, `text_pipeline`
  (indirect). Kernel.conf is a separate flat parser (unchanged, §12).
- Corpus: `tools/kconf_corpus.sh` + host binaries (`tools/kconf_host.c`,
  `tools/kconf_libconfig_host.c`), `.build/config_*` / `config_cli_*`
  harnesses, guest config suites (config_m0/m1/m7 …).

## 3. Design (v2 implementation)

- **Store**: replace the flat entry list with a tree: `entry` gains a
  record value (ordered child map) and arrays hold elements that may be
  scalars or records. The explicit-block-name list is deleted (record
  nodes ARE the structure).
- **Parser**: extend `parse_value`/array parsing for the bracketed
  literal `[ … ]` and record-as-value; enforce the §10 diagnostics:
  duplicate record names in a block, scalar-vs-container conflicts,
  quoted `{`-leading bare values. Keep v1 forms byte-identical.
- **Keys**: add `ident[i]` segments; address resolution walks records
  and indexes arrays.
- **Readers**: flat dot-key getters become tree walks (unchanged
  signatures); new tree getters return record / array-of-record values
  (`CONFIG_TYPE_RECORD` added); `config_get_all`/`config_record_*`
  enumerate over the tree (record enumeration = real record children,
  replacing the block-name list).
- **Scope resolution**: scalars + records = highest defining scope wins
  (unchanged); **arrays = concatenation** of each defining scope's list
  in system → user → shared order. `config_read_scope` stays for
  per-scope inspection (domains needing non-additive composition).
- **Writer**: canonical nested spelling — records as `key = { … }`,
  arrays of records as the bracketed anonymous form, flat domains
  byte-for-byte round-trip (existing §10.1 guarantee kept).
- **CLI**: `config read` prints the nested spelling; `config write`
  accepts bracket paths + array/record literals; array display handles
  records.
- Kernel.conf parser + the `config` grammar doc's flat subset: untouched.

## 4. Milestones

- **CV2-M0 — tree parse + record values.** libconfig.c store/parser move
  to the tree; CONFIG_TYPE_RECORD; bracketed literal + record values +
  the §10 diagnostics; flat getters walk the tree.
  *Acceptance:* the entire existing corpus (kconf corpus, host conf
  tests, CLI suites, every shipped `system.*.conf` + userland config)
  passes unchanged; new parse cases (anonymous arrays, records in
  arrays, dup-record/scalar-container errors, quoted `{`) covered by
  added corpus files; no consumer rebuilt.
  *Implementation design (from the 2026-09 analysis):* parse_conf is
  LINE-based with a pctx container stack (prefix flattening + per-
  container record-name binding). The contained approach keeps the flat
  entry store and adds a pctx **array mode**: after `key = [`, items are
  parsed across lines (records via the existing block machinery with
  synthetic keys `key[idx].child`, never exposed), scalars as today;
  named blocks `key = { … }` keep their v1 flattening so record-domain
  consumers (pwconf/mounts) are untouched, and config_read of a block
  name SYNTHESIZES a CONFIG_TYPE_RECORD value from its prefix children
  (the v2 "reads never care which spelling" guarantee). Header adds
  CONFIG_TYPE_RECORD + a record representation + value_free/child
  accessors (additive only). Bracket KEY segments stay M1; the v2
  canonical writer stays M2 (v2-shaped values reject cleanly in M0).
- **CV2-M1 — bracket addressing + additive merge + tree reads.**
  `ident[i]` key segments; §5-v2 array resolution across scopes; tree
  getters + record enumeration over the tree; `config_read_scope` for
  arrays.
  *Acceptance:* bracket-path reads/writes round-trip; a synthetic
  two-scope array domain concatenates system→user→shared (and unit
  cases: scope missing = skipped, all-scalar legacy unchanged);
  per-scope array inspection works; current shipped domains have no
  multi-scope arrays (verified — no behavior change in the wild).
- **CV2-M2 — canonical writer + config CLI.** Writer emits v2 spelling
  (flat round-trip byte-for-byte preserved); CLI read prints nested
  trees, write accepts bracket keys + array/record literal values.
  *Acceptance:* `config write` of a record/array-of-record domain then
  `config read` round-trips byte-identical; a rules-shaped fontconfig
  domain edits surgically via `rules[0].edits[1].value`; CLI corpus
  green.
- **CV2-M3 — consumer regression + guest acceptance.** Rebuild the
  shared lib + every consumer against v2; run the full guest matrix
  (config suites, musl pwconf/hosts guest tests, toybox account tools,
  Xfb configargs, init mounts, text_pipeline) + fshlint + recovery.
  Docs current (config-design §11 header/API; plan status).
  *Acceptance:* all markers green; fshlint 0; tree clean.

Handoff: fontconfig M0 (fclibconf on the v2 tree API) per
docs/design/fontconfig-config-plan.md — the domain it reads
(`system.fonts`, `rules` = array of records) is the v2 grammar.

## 5. Risks / notes

- **Multi-scope array behavior change** is the only semantic delta; the
  corpus verifies no shipped domain currently relies on whole-value
  array shadowing.
- Parse-error surface grows (dup records, scalar-vs-container): v1 files
  are a strict subset, so only NEW malformed shapes can newly fail.
- `config_value` lifetime/ownership rules (lib-owned until free) carry
  to record/array-of-record values.
- Performance: tree walks at read time replace flat scans; negligible
  for config-sized files, but the corpus includes a large-domain
  sanity case.
- Single commit per milestone; each leaves the tree + docs green.
