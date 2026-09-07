# `config` — a universal configuration utility

Status: DRAFT — design decisions locked; no open questions.
Modeled loosely on macOS `defaults`, but with the configuration **tree**
that the FSH redesign provides (`Configuration/` is a first-class directory
at every scope) instead of plists.

## 0. Config-language policy (standing, 2026-09)

**All software with config files on FNX uses libconfig.** One
configuration language, one tool (`config`), one file-per-domain model —
no format zoo (no XML/ini/JSON/TOML config authored by FNX or by any
adopted port).

- **First-party + adopted third-party software**: configuration is a
  libconfig domain in a `Configuration/` directory (system scope by
  default), read by the software itself or by the `config` tool.
  Adopting a port must not introduce a second config format — convert or
  gate it behind the domain (recorded per
  docs/design/self-hosting-packages.md §6).
- **A consumer's private on-disk format may remain only where the domain
  targets it** (the kernel.conf precedent: `system.kernel` drives the
  ESP's `kernel.conf` file the bootloader reads). FNX never authors
  config in a foreign format directly.
- **fontconfig** is the standing migration example: its XML config
  surface (`fonts.conf`, the `fcxml` front-end) is being replaced by a
  libconfig domain + loader (docs/design/fontconfig-config-plan.md) so
  no XML config exists on FNX.
- **Behavioural scripting is NOT configuration (policy carve-out,
  2026-09)**: scripts and behavioural/automation material (extension
  scripts, app behaviour data) are not libconfig domains and do not
  live in `Configuration/`. They ship in a dedicated **Application
  Support/** directory in the system, shared, and user domains —
  `/System/Application Support/`, `/Shared/Application Support/`,
  `/Users/<user>/Application Support/` (FSH; macOS's
  `~/Library/Application Support` analogue). Rule of thumb: a thing
  you *read/write as settings* is Configuration (libconfig); a thing
  you *run or that changes behaviour* is Application Support.

---

---

## 1. Purpose

One command, for every app and every scope, to read, write, and delete
configuration. No daemon, no cache, no format zoo: configuration is plain
text files in `Configuration/` directories, and `config` is a thin, typed
command-line layer over them.

## 2. Storage model (decided)

- **One file per app domain**, `.conf` extension. A domain is the app's
  namespace — the same idea as a `defaults` domain.
- **Reserved pseudo-domain for first-party apps**: FNX's own apps use
  `system.<app>` (e.g. `system.dock`, `system.xfb`,
  `system.kernel`); the `system` root is reserved and is
  not reverse-DNS. Third-party apps keep reverse-DNS domains
  (`com.example.HelloWorld.conf`).
- Files live in a `Configuration/` directory at the **three scopes** of
  the origin model (decided):

  | Scope | Flag | Location | Role (docs plan D4/§5.0) |
  |---|---|---|---|
  | system | `-s` | `/System/Configuration/` | the machine's real configuration — authoritative |
  | user | `-u` (default) | `Users/$USER/Configuration/` | the person (overrides defaults, never System) |
  | shared | `-g` | `/Shared/Configuration/` | overridable defaults (first- + third-party) |

  Placement follows setting kind (plan §5.0): non-overridable global
  settings (identity, boot policy) live in System scope; overridable
  first-party settings ship in Shared under `system.<app>`; a
  System copy of the same domain locks a value. See §5.

  ```
  /System/Configuration/system.passwd.conf     (identity: authoritative)
  /Shared/Configuration/system.xfb.conf        (overridable first-party default)
  /Shared/Configuration/com.example.HelloWorld.conf   (third-party)
  Users/$USER/Configuration/system.shell.conf  (the person)
  ```

  (`/Shared/Configuration/` is a new FSH directory added by this design;
  it completes the origin model: OS / third-party / person.)

## 3. File format (decided)

Plain UTF-8 text, one key per line, `#` comments:

```
# dock preferences
autohide = true
tile-size = 48
magnification = 1.5
prompt = "> "
recent-apps = Terminal, Editor, Mail
```

- `key = value`; whitespace around `=` and at line ends trimmed.
- **Type inference** (decided): `true`/`false` → bool; integer → int;
  float → float; comma-separated list → array (elements inferred);
  anything else → string.
- Quoting: a string may be double-quoted to keep spaces or `#`/`=` as
  literals.
- **Keys are flat with dot-nested structure (decided)**: no INI sections;
  hierarchy is expressed with dot-separated keys, e.g. `window.x = 10`,
  `window.y = 20`. `config read <domain> window` lists every key with the
  `window.` prefix.

## 4. Command surface

```
config list                          list domains (all scopes)
config list [-s|-g|-u]               list domains in one scope
config read  [-s|-g|-u] [domain] [key]   read a scope, a domain, or one key
config write [-s|-g|-u] <domain> <key> <value> [-type T]
config delete [-s|-g|-u] <domain> [key]
```

- Default scope is `-u` (user). `config read dock` → user's
  `system.dock.conf`; if the domain is absent there, resolution falls
  back per precedence (Q-F, superseded — see §5).
- `config read` with no key prints the whole domain as `key = value`
  lines; with a key, prints just the value (so it composes in scripts:
  `x=$(config read dock tile-size)`).
- `-type T` forces bool/int/float/string/array on write; without it the
  value is inferred the same way reading infers.
- Writes are **atomic**: write to a temp file in the same directory,
  fsync, rename over the target. A failed write never leaves a
  half-written config.

## 5. Precedence (decided; owner review 2026 supersedes the original
order — docs/design/system-config-files-plan.md D4)

**Resolve system → user → shared** when reading a key: a value in
`/System/Configuration` is checked first and **cannot be overridden at
all**; the person's file (`Users/<u>/Configuration`) is consulted next
and overrides *defaults*, never System; `/Shared/Configuration` holds
overridable defaults and is checked last. Beneath all file scopes sits
the software's compiled-in default (docs plan D5): a value in any scope
overrides it. Roles: system = the machine's real configuration
(authoritative), shared = overridable shipped defaults (first- and
third-party), user = the person. This supersedes config-design's earlier
"user → shared → system" (Q-F).

## 6. Validation and defaults

`config` is deliberately **schema-less**: it stores what it's told, in
the requested type. Apps validate their own values after reading (a bad
value is just a bad value — the app can fall back to its compiled-in
default). No per-domain schema registry in v1.

## 7. Apps reading config — libconfig (decided)

**libconfig is built as part of this milestone.** Apps link it instead of
shelling out to the `config` binary or parsing files by hand:

- typed getters: `config_get_bool/string/int/float/array(domain, key, &out)`;
- scope resolution built in (system → user → shared) with a
  `config_get_user_override()`-style query when an app needs to know
  *where* a value came from;
- prefix reads for dot-nested keys (`config_get_all(domain, "window")`);
- atomic write helpers matching the CLI's temp + rename semantics;
- absent-key handling: the app supplies a default, so a missing value is
  never an error.

Reading the plain files directly remains valid for trivial cases, but
every real app should use libconfig so type and scope rules live in one
place.

## 8. Relationship to the FSH

- The three `Configuration/` directories already exist in the hierarchy
  (`/System/Configuration/`, `Users/$USER/Configuration/`) or are added
  by this design (`/Shared/Configuration/`).
- `config` treats a domain name as a filename, not a path: it never
  walks the tree by hand, it just resolves
  `<scope>/Configuration/<domain>.conf`.
- `Users/$USER/Configuration/` is scaffolded with the home; per-user
  config needs no setup beyond the directory existing.

---

## 9. Decisions

- **Q-D — Key structure: flat + dot-nested.** No INI sections; hierarchy
  via dot-separated keys (`window.x`); prefix reads for nested groups.
- **Q-N — First-party domain namespace: reserved pseudo-domain
  `system`.** FNX's own apps are `system.<app>` (not
  reverse-DNS); third parties keep reverse-DNS (`com.example.<app>`).
- **Q-E — libconfig: built now.** Typed getters, scope resolution,
  prefix reads, atomic writes; apps link it instead of shelling out.
- **Q-F — Precedence: system → user → shared.** `/System/Configuration`
  is authoritative (checked first — nothing overrides it); the person
  wins over third-party machine-wide defaults; then the OS default.
  (Original order superseded by owner review — see §5 and
  docs/design/system-config-files-plan.md D4.)

The design has no open items.

---

*This draft is grounded in the FSH decisions: the origin model (§3.3 of
`docs/design/fsh-proposal.md`) and the Q7 spaced-name / Q12 PATH conventions.*

## 10. `.conf` grammar (normative)

```
.conf      := (line EOL)*
line       := empty | comment | assignment | close-brace
empty      := WS*
comment    := WS* '#' any-char*            -- full-line comments only
assignment := WS* key WS* '=' WS* value WS*
            | WS* key WS* '{' group-line* close-brace WS*   (block value)
group-line := empty | comment | assignment
close-brace:= WS* '}' WS*                  -- on its own line
key        := segment ('.' segment)*       -- dot-separated, no empty segments
segment    := ident | ident '[' index ']'  -- rules[0].edits[1] (§10.2)
ident      := [A-Za-z_] [A-Za-z0-9_-]*
index      := digits                        -- 0-based array position
value      := boolean | integer | float | quoted-string | array
            | bare-string | record | empty
boolean    := 'true' | 'false'             -- lowercase, case-sensitive
integer    := sign? digits | sign? '0x' hexdigits
float      := sign? digits '.' digits? exp? | sign? digits exp
exp        := [eE] sign? digits
quoted-string := '"' (escape | char)* '"'
escape     := '\' ( '"' | '\' | 'n' | 't' )   -- unknown escapes: parse error
array      := csv-list | bracketed-list
csv-list   := element (WS* ',' WS* element)*     -- v1 comma form (scalars)
bracketed-list := '[' WS* ']' | '[' list-item (WS* ',' WS* list-item)* WS* ']'
list-item  := scalar-element | record            -- records need the
                                                  -- bracketed form (§10.2)
element    := boolean | integer | float | quoted-string | bare-element
scalar-element := boolean | integer | float | quoted-string | bare-element
bare-element  := char+ except {WS, ',', '"', '{', '}'}
bare-string  := char+ except {WS, '#', '=', ',', '"', '{', '}'}
record     := '{' record-line* close-brace WS*   -- block value (§10.2)
record-line := empty | comment | assignment
empty      := (nothing after '=')
WS         := ' ' | '\t'
```

### Rules and edge cases

- **Keys** are case-sensitive; segments start with a letter or `_` and may
  contain letters, digits, `_`, `-`. Max 64 chars per segment, 255 per
  key. No trailing/leading/consecutive dots.
- **Type precedence on read**: integer → float → boolean → array →
  string. So `1` is an int (never bool), `1.5` is float, `true`/`false`
  are bool, a token with a top-level `,` is an array, everything else is
  a string. `0x1F` is an int.
- **Multi-word strings must be quoted**: `prompt = hello world` is a
  parse error; `prompt = "hello world"` is a string. Values containing
  `#`, `=`, `,`, or `"` must be quoted (`#` only starts a comment at
  line start; there are **no trailing comments**).
- **Arrays**: `recent-apps = Terminal, Editor, Mail` — elements are
  individually inferred (bool/int/float/string). Quoted elements may
  contain commas: `"a,b", c`. A **bracketed literal** `[ … ]` is an
  explicit array that may also hold records (§10.2); scalars inside it
  are inferred as usual. Brackets only open an array at value position —
  `a[0] = 1` is a key, `a = [1]` is a value.
- **Empty value** (`key =`) is an empty string.
- **Duplicate keys**: last occurrence wins (deterministic).
- **Encoding**: UTF-8; an optional BOM at the start of the file is
  ignored. Values and keys are stored as given (no case folding).
- **Parser limits** (protective): line ≤ 4096 bytes, file ≤ 1 MB.
- **Writing** canonicalizes: bool → `true`/`false`, int → decimal,
  float → decimal, string → bare when it contains no whitespace/`#`/`=`/
  `,`/`"`, else quoted, array → comma-joined with per-element quoting.
  A file whose top level contains explicit blocks is written in the
  nested (block) spelling; a pure flat file keeps its flat spelling
  byte-for-byte (§10.1).

## 10.1 Records, arrays of records, and tree values (v2)

**Records are values.** A block (`key = { … }`) is a *record* — a
first-class config value, not a file spelling — and records may appear
anywhere a value can, including as **anonymous array elements**:

```conf
rules = [ { match = pattern
            tests  = [ { object = family; compare = eq; value = "Sans" } ]
            edits  = [ { object = family; mode = assign; value = "DejaVu Sans" } ] },
          { match = pattern
            edits  = [ { object = family; mode = prepend; value = "DejaVu Serif" } ] } ]
```

The in-memory model is a **tree**: a value is a scalar, a record
(ordered field map), or an array whose elements are scalars or records.
The flat dot-key read model remains: a dot path walks the tree, `config
read <domain> rules` returns the whole nested value, and existing flat
domains + the kernel.conf flat subset (§12) are unchanged.

**Key addressing.** Segments may carry a 0-based array index:
`rules[1].edits[0].value`. Bare segment + bracket forms may mix
(`rules[0].tests.1` ≡ `rules[0].tests[1]`), but each array element is
addressed by its own index only — no implicit ordinal keys are ever
exposed or written. The canonical writer emits the anonymous-array
spelling above; flat domains keep their flat spelling byte-for-byte.

**Parse rules** (carried from v1 + new):

- A record value opens when the first non-WS char after `=` is `{` and
  likewise as an array element; `}` closes the innermost open record.
  `key = {}` is an empty record. A bare value beginning with `{` must be
  quoted (`key = "{notablock"`); an unclosed record or stray `}` is a
  parse error.
- A record's name is a single segment (dotted block keys are a parse
  error). Full keys ≤ 255 chars.
- **Duplicate record names in one block are a parse error** — records
  are identity-bearing and must not silently collapse. Duplicate leaf
  keys keep last-wins.
- A name may not be **both a stored scalar and a container**: `a = 1`
  with `a = { … }` / `a.b = 2` (either order, either spelling) is a
  parse error. An array value is one value — it may not also be a
  record name.
- **Empty value** (`key =`) is an empty string.

**Scope merging (v2, supersedes §5 for array values).** Precedence
remains resolve system → user → shared, system authoritative. Scalars
and records follow the v1 rule (the highest scope that defines the key
wins wholesale). **Arrays are additive**: the effective value is the
concatenation of every scope's list that defines the key, in precedence
order — system elements first, then user, then shared — so a lower
scope's list is never shadowed and nothing is silently dropped. A
domain that needs a different composition (replace, per-element merge)
documents it and reads the per-scope values itself via
`config_read_scope`. fontconfig's `rules` is a system-only domain today
(single list, file order preserved).

**Writing**: the canonical writer emits record/array-of-record domains
in the nested spelling above (4-space indent); flat domains round-trip
byte-for-byte. Records in arrays are written anonymously.

## 11. libconfig header

The library API is provided as a real header, `userland/libconfig.h`
(userland; the kernel does not include it). It follows the grammar above:
typed getters with system → user → shared resolution, scope-explicit
reads/writes, atomic writes (temp + fsync + rename), domain/key
validation, and error codes for NOT_FOUND / TYPE / PARSE / IO / INVALID /
ACCESS / NOMEM. See the header for the full contract.

v2 (this design) is additive on the header:

- `CONFIG_TYPE_RECORD` joins the value union as an ordered field map
  (`v.record.fields[]`, each a malloc'd `name` + lib-owned
  `config_value_t`). Record values are produced by `config_read` of a
  block name (synthesized from its prefix children) and by tree reads
  of array elements. `config_record_child()` resolves one field;
  `config_value_free()`/`config_value_copy()` recurse into records.
- `config_valid_address()` validates *addressing keys*: plain dotted
  keys plus `ident[i]` / bare-digit index segments
  (`rules[1].edits[0].value`, `rules[0].tests.1`). Reads and writes
  accept them; files only ever carry ident segments (the canonical
  writer emits arrays anonymously).
- Reads resolve an address in two phases: the leading ident run is a
  dotted base key resolved against the flat store, then index/name
  steps descend the stored tree value. Array bases are **additive**
  across scopes for `config_read` (concatenated system → user →
  shared); `config_read_scope` stays per-scope.
- The canonical writer serializes `CONFIG_TYPE_RECORD` and arrays
  whose elements are records in the nested v2 spelling; scalar and
  scalar-array leaves keep the v1 comma spelling so flat domains
  round-trip byte-for-byte.

See the plan (docs/design/config-v2-plan.md) for the milestone-by-
milestone status.

## 12. `kernel.conf` — the kernel's boot configuration (decided)

The kernel's own options live in a config file **next to the kernel on
the ESP**: `/System/ESP/EFI/BOOT/kernel.conf`, in the same `.conf` grammar as
everything else (§10) — one format for user config *and* kernel boot
config. Example:

```
# FNX kernel boot options (docs/design/fsh-proposal.md §9.2 Q8)
console = ttyS0
root = /dev/hda1
verbose = true
```

Rules:

- The file is read **early in boot** (before the XBFS root is mounted),
  via early ESP access — the FAT32 driver, or UEFI boot-services file
  I/O before `ExitBootServices` until then. The early read is a plain
  `\kernel.conf` on the ESP: no symlinks, no `/System` mount yet.
- The kernel cmdline, when present, **overrides individual keys**.
- The kernel needs a **small kernel-side `.conf` parser** implementing
  the same grammar (flat/dot-nested keys; string/int/bool values; the
  §10 rules). It is a lean read-only subset — no writes, no scopes, no
  arrays needed in v1. Because `config`/Settings rewrite the file
  **canonically** (§10 "Writing"), the kernel parser must accept
  canonical output (bare-or-quoted strings, `true`/`false`, ints incl.
  `0x`, empty `key =`, duplicate-key last-wins). The grammar stays
  identical so a file works everywhere; the parser is shared source
  between kernel and userland where practical.

Editing (decided, Q5 owner review): `system.kernel` is a
**pinned single-file domain**. It is the one config domain whose file
lives outside the three scope roots — physically on the ESP, next to
the kernel. libconfig carries a small built-in alias table:
`system.kernel` → `/System/ESP/EFI/BOOT/kernel.conf` (the ESP is mounted
at `/System/ESP` from FSH Q2). No `/System/Configuration` file or
symlink exists for it.

- The alias is **System-authoritative and exempt from layering**: a
  read of `system.kernel.*` loads only the ESP file; user/shared
  scope files named `system.kernel.conf` are not consulted
  (boot config is machine state — a user-scope value could only ever
  claim to set something the kernel never saw). Resolution for this
  domain is: ESP file value, else the kernel's compiled-in default
  (D5-style), else nothing — never a user/shared merge.
- **Reads/writes/keys** go straight to `/System/ESP/EFI/BOOT/kernel.conf` like
  any ordinary file domain: the existing atomic writer (temp + fsync +
  rename) needs no symlink or cross-filesystem special-casing, since
  temp and target share the ESP directory.
- **Unavailability** is explicit: an unmounted ESP makes the alias
  resolve to a missing file, which `config` surfaces as the kernel
  domain being unavailable (IO error, per libconfig resolution rules) —
  acceptable, since userland runs post-mount.
- The ESP file remains canonical: it is what boot consumes and what
  `config` edits in place.

Implementation note: `userland/libconfig.c` derives every domain path
from the three scope roots today; the alias is a small lookup before
that derivation (a `domain_path` short-circuit), plus the read path
skipping the scope merge for this one domain. The kernel-side early
read (plain `\kernel.conf` on the ESP, before the root is mounted)
is unchanged.

This gives the kernel a self-contained boot identity: `FNX.efi` + its
config live together on the ESP, inspectable and editable through the
normal `config` CLI/API once `/System/ESP` is mounted.
