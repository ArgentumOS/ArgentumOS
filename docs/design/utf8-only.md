# Encoding policy: UTF-8 only (decided)

Status: DECISION — the OS supports UTF-8 as its single text encoding.
No other locales or character encodings will be supported.

---

## The decision

The FNX OS supports **only UTF-8** for all text. There is exactly one
locale — UTF-8 — and no legacy encodings, no alternate locales, no
conversion infrastructure.

## What this means

- **Every text byte stream is UTF-8**: files, configs, device I/O,
  terminal input/output, environment variables, command-line arguments,
  filenames.
- **Filenames** are byte strings interpreted as UTF-8 (consistent with
  the FSH design; names like `Variable Data` are UTF-8).
- **Config files** are UTF-8 (already specified in
  `docs/design/config-design.md` §10: "Encoding: UTF-8; an optional BOM at the
  start of the file is ignored").
- **Locale handling is trivial**: `setlocale()` accepts only `C`,
  `POSIX`, or `C.UTF-8`-equivalent (all effectively the same behavior);
  `LANG`/`LC_*` environment variables are honored only in that they
  select the single supported locale, and are otherwise inert. musl's
  minimal locale support is already close to this — no locale data
  files, no collation tables, no number/date formatting variants.
- **No iconv** — there is nothing to convert between. No multibyte
  conversion tables in libc beyond UTF-8 encode/decode.
- **Terminal/console** output is UTF-8; input is decoded as UTF-8.

## What is explicitly out of scope

- Legacy 8-bit encodings (Latin-1, CP437, KOI-8, ...), EBCDIC, UTF-16,
  UTF-32 as storage or interchange formats.
- Per-locale collation, case conversion, or number/currency/date
  formatting beyond what UTF-8 itself implies.
- Any second locale, now or later.

## Consequences

- Simpler libc (no locale data, no iconv); simpler tools (toybox, dash —
  no locale-dependent branches needed); simpler terminal stack.
- Porting rule: userland must not depend on `setlocale` changing
  behavior, and must treat all text as UTF-8. Anything requiring a
  different encoding is rejected at port time (the same
  mindful-porting gate as the FSH path policy).
- Byte-transparent tools (`cat`, `cp`, ...) need no changes — UTF-8 is a
  superset of ASCII, so plain-ASCII data flows through unchanged.

## Related decisions

- `docs/design/fsh-proposal.md` — filesystem hierarchy; names are UTF-8.
- `docs/design/config-design.md` — `.conf` format is UTF-8.
- `docs/design/toybox-fsh-plan.md` — userland porting; the same encoding rule
  applies to all ported software.
