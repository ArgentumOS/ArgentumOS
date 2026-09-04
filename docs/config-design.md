# `config` — a universal configuration utility

Status: DRAFT — design decisions locked; no open questions.
Modeled loosely on macOS `defaults`, but with the configuration **tree**
that the FSH redesign provides (`Configuration/` is a first-class directory
at every scope) instead of plists.

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
  `system.config.<app>` (e.g. `system.config.dock`, `system.config.xfb`,
  `system.config.kernel`); the `system.config` root is reserved and is
  not reverse-DNS. Third-party apps keep reverse-DNS domains
  (`com.example.HelloWorld.conf`).
- Files live in a `Configuration/` directory at the **three scopes** of
  the origin model (decided):

  | Scope | Flag | Location | Owner |
  |---|---|---|---|
  | user | `-u` (default) | `Users/$USER/Configuration/` | the person |
  | shared | `-g` | `/Shared/Configuration/` | third parties, machine-wide |
  | system | `-s` | `/System/Configuration/` | the OS |

  ```
  /System/Configuration/system.config.dock.conf
  /Shared/Configuration/com.example.HelloWorld.conf
  Users/$USER/Configuration/system.config.shell.conf
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
  `system.config.dock.conf`; if the domain is absent there, resolution falls
  back per precedence (Q-F).
- `config read` with no key prints the whole domain as `key = value`
  lines; with a key, prints just the value (so it composes in scripts:
  `x=$(config read dock tile-size)`).
- `-type T` forces bool/int/float/string/array on write; without it the
  value is inferred the same way reading infers.
- Writes are **atomic**: write to a temp file in the same directory,
  fsync, rename over the target. A failed write never leaves a
  half-written config.

## 5. Precedence (decided)

**Resolve user → shared → system** when reading a key: the person's
setting wins, then the third-party machine-wide value, then the OS
default. The OS itself never reads `Shared/`; third-party apps never
write `System/`.

## 6. Validation and defaults

`config` is deliberately **schema-less**: it stores what it's told, in
the requested type. Apps validate their own values after reading (a bad
value is just a bad value — the app can fall back to its compiled-in
default). No per-domain schema registry in v1.

## 7. Apps reading config — libconfig (decided)

**libconfig is built as part of this milestone.** Apps link it instead of
shelling out to the `config` binary or parsing files by hand:

- typed getters: `config_get_bool/string/int/float/array(domain, key, &out)`;
- scope resolution built in (user → shared → system) with a
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
  `system.config`.** FNX's own apps are `system.config.<app>` (not
  reverse-DNS); third parties keep reverse-DNS (`com.example.<app>`).
- **Q-E — libconfig: built now.** Typed getters, scope resolution,
  prefix reads, atomic writes; apps link it instead of shelling out.
- **Q-F — Precedence: user → shared → system.** Person wins, then
  third-party machine-wide, then OS default.

The design has no open items.

---

*This draft is grounded in the FSH decisions: the origin model (§3.3 of
`docs/fsh-proposal.md`) and the Q7 spaced-name / Q12 PATH conventions.*

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
segment    := [A-Za-z_] [A-Za-z0-9_-]*
value      := boolean | integer | float | quoted-string | array
            | bare-string | empty
boolean    := 'true' | 'false'             -- lowercase, case-sensitive
integer    := sign? digits | sign? '0x' hexdigits
float      := sign? digits '.' digits? exp? | sign? digits exp
exp        := [eE] sign? digits
quoted-string := '"' (escape | char)* '"'
escape     := '\' ( '"' | '\' | 'n' | 't' )   -- unknown escapes: parse error
array      := element (WS* ',' WS* element)*
element    := boolean | integer | float | quoted-string | bare-element
bare-element  := char+ except {WS, ',', '"'}
bare-string  := char+ except {WS, '#', '=', ',', '"'}
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
  contain commas: `"a,b", c`.
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

## 10.1 Group records (block values)

Block values make a **record domain** expressible: `key = { … }` opens
a group whose lines are relative to the key (`user = { admin = { uid =
0 } }` spells the key `user.admin.uid`). Blocks may nest arbitrarily and
empty blocks (`x = {}`) are valid. Blocks are a *file spelling*: the
in-memory model stays a flat dot-key map, and flat and nested spellings
of the same keys may be mixed in one file (reads never care which
spelling produced a key). This amendment is the normative home of
docs/system-config-files-plan.md §3.

Rules and edge cases:

- A block value opens when the first non-WS char after `=` is `{`, and
  `}` closes the innermost open group. Both appear on their own lines in
  canonical files (`key = {` … `}`); an inline `}` right after `{`
  (`key = {}`) is an empty group. Anything else after `{` on the line is
  a parse error, so a *bare value beginning with `{` is no longer
  legal* — quote it: `key = "{notablock"`. (A `{` elsewhere in a value,
  e.g. `key = a{b`, is untouched.) An unclosed group at EOF or a stray
  `}` is a parse error.
- A block's name is a single segment (a dotted block key is a parse
  error). Full keys stay ≤ 255 chars and relative keys follow the key
  grammar.
- **Duplicate record names in one block are a parse error** — records
  are identity-bearing and must not silently collapse. Duplicate leaf
  keys keep the existing rule (last occurrence wins), whether flat or
  inside a block.
- A name may not be **both a stored scalar and a container**: `a = 1`
  with `a.b = 2` (either order, either spelling) is a parse error.
- **Writing**: the canonical writer emits record domains (files with an
  explicit top-level block) in the nested spelling with 4-space
  indentation, and flat domains exactly as before — the two forms
  round-trip byte-for-byte after a rewrite.
- The kernel parser (§12) is a flat subset and is unchanged: `kernel.conf`
  is a flat domain and only record domains use the nested spelling.

## 11. libconfig header

The library API is provided as a real header, `include/libconfig.h`
(userland; the kernel does not include it). It follows the grammar above:
typed getters with user → shared → system resolution, scope-explicit
reads/writes, atomic writes (temp + fsync + rename), domain/key
validation, and error codes for NOT_FOUND / TYPE / PARSE / IO / INVALID /
ACCESS / NOMEM. See the header for the full contract.

## 12. `kernel.conf` — the kernel's boot configuration (decided)

The kernel's own options live in a config file **next to the kernel on
the ESP**: `/System/ESP/kernel.conf`, in the same `.conf` grammar as
everything else (§10) — one format for user config *and* kernel boot
config. Example:

```
# FNX kernel boot options (docs/fsh-proposal.md §9.2 Q8)
console = ttyS0
root = /dev/hda1
verbose = true
```

Rules:

- The file is read **early in boot** (before the BFS root is mounted),
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

Editing (decided, Q5 owner review): `system.config.kernel` is a
**pinned single-file domain**. It is the one config domain whose file
lives outside the three scope roots — physically on the ESP, next to
the kernel. libconfig carries a small built-in alias table:
`system.config.kernel` → `/System/ESP/kernel.conf` (the ESP is mounted
at `/System/ESP` from FSH Q2). No `/System/Configuration` file or
symlink exists for it.

- The alias is **System-authoritative and exempt from layering**: a
  read of `system.config.kernel.*` loads only the ESP file; user/shared
  scope files named `system.config.kernel.conf` are not consulted
  (boot config is machine state — a user-scope value could only ever
  claim to set something the kernel never saw). Resolution for this
  domain is: ESP file value, else the kernel's compiled-in default
  (D5-style), else nothing — never a user/shared merge.
- **Reads/writes/keys** go straight to `/System/ESP/kernel.conf` like
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
