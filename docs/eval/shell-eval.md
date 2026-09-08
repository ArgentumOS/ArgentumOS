# Shell evaluation — FNX system shell options

Status: **EVALUATION (2026-09), SUPERSEDED decision** — the original
decided direction (Finch = fork of mksh as a sole system shell aliased
`sh`) is **replaced**: mksh is out (personal reasons, 2026-09); the
user chose **from-scratch**, and the two-shell model dissolved the
sole-shell premise — dash stays `/bin/sh` forever, Finch is the user
shell. Full design: `docs/design/finch-shell-plan.md`. This doc remains
the license/candidate record that shaped the choice.

Working name Finch (the bird kin — FNX-phoenix family: Argentum the
toolkit, Kestrel the WM, Finch the shell). "fsh" was considered and
rejected: FSH already names the filesystem hierarchy (fsh-proposal,
fshlint, FSH env) — a fatal brand collision. The live `/bin/sh` today
is dash, with FSH patches — and that is now the permanent arrangement.

FNX has two shell jobs: `/bin/sh` (POSIX scripting, tiny, static-
capable) and the **interactive login shell** (editing/history/
completion). A candidate can serve one or both. Constraint: permissive
license on-FNX (no GPL/LGPL; MIT/BSD/ISC/Apache/public-domain fine) —
and GNU readline is GPL-3+, so anything needing readline-style editing
must implement it itself.

## License facts (verified at research time, 2026-09)

| Shell | License | Interactive editing? | /bin/sh capable? |
|---|---|---|---|
| dash | BSD-3-Clause | no | yes (POSIX-strict) |
| zsh | MIT-style core, but optional shell-function files are GPL (omit them) | zle + compsys | superset |
| mksh | MirOS licence (BSD/MIT/ISC spirit, fully permissive) | built-in emacs + vi | yes (Korn superset) |
| oksh | pdksh core public domain; portability files BSD/ISC (musl listed as supported) | vi/emacs (OpenBSD ksh) | yes (Korn superset) |
| ksh93 (93u+m) | EPL-2.0 — file-level copyleft, fails a hard permissive line | yes | superset |
| Oils (osh/ysh) | Apache-2.0 | pre-1.0; ysh not yet a bash-compatible interactive shell | not yet |
| elvish | BSD-2-Clause | yes | **no** (own language) |
| nushell | MIT | yes | **no** (own language) |
| bash | GPL-3.0-or-later | readline (GPL) | — excluded |
| fish | GPL-2.0 | own | — excluded |

Sources: dash COPYING (kernel.org); zsh LICENCE (zsh-users/zsh); mksh.1
(MirBSD/mksh, MirOS licence quoted); oksh README + LEGAL (ibara/oksh);
ksh93 LICENSE (EPL-2.0 badge); Oils LICENSE.txt; elvish LICENSE;
nushell LICENSE; bash COPYING; fish COPYING.

## Cases per candidate

- **dash (BSD-3)** — the keep-as-sh case: tiny, POSIX-strict, correct,
  static-friendly for the recovery set. One unlinked GPL generator
  file (`mksignames.c`) contributes only generated output — standard
  shipping arrangement. Useless interactively.
- **mksh (MirOS)** — the best single-shell case: fully permissive,
  ~one 40k-line C file, built-in emacs+vi editing/history/completion
  (no readline), Korn superset of sh, musl-clean, Android ships it as
  its system shell. One binary can serve both jobs.
- **oksh/loksh (public domain + BSD/ISC)** — the OpenBSD-quality case:
  same Korn feature story, musl explicitly supported; loksh repo could
  not be located at research time (oksh v7.9 is the live one).
- **zsh (MIT-style core)** — the power case: zle/compsys/deep history,
  but ~10× the code of the Korn options, and its config-obsessed
  dotfile universe fights the all-libconfig/Application-Support
  policy. A platform decision, not an sh decision.
- **ksh93 (EPL-2.0)** — excluded by the hard permissive line (file-
  level copyleft), and the largest codebase for marginal gain.
- **Oils (Apache-2.0)** — the watch case: eventual permissive bash
  replacement; not there yet.
- **elvish/nushell (BSD-2/MIT)** — genuinely good interactive shells,
  entirely non-POSIX: no `/bin/sh` role; user-taste installs, not
  first-party ships.

## Finch — from scratch (decision 2026-09)

mksh is out (personal reasons). Re-evaluated: **oksh** (portable
OpenBSD ksh — pdksh core PD, BSD/ISC portability glue, musl-supported;
the same ksh-family case as mksh under a different project) vs **dash
fork** vs **from-scratch**. User chose from-scratch, per the house
doctrine — and the two-shell model (below) removed the reason a fork
was ever attractive: a from-scratch shell should not pay POSIX
conformance to re-implement dash.

**Finch = a designed language** (rc semantics under a C-skin syntax,
full POSIX job control), from-scratch, C, first-party; installed as
`finch`; **not** `/bin/sh`. dash remains `/bin/sh`, the static recovery
sh, and the script runner — permanent. The fork era's QoL layer
(listed below) survives as the interactive-surface design in
`docs/design/finch-shell-plan.md` §2.5, where the from-scratch parser
makes it strictly more capable (live reparse, token-aware completion).

Fork-era QoL layer, for the record:

- **Interactive**: tab completion + menu; ^R history search (no `!`
  expansion); bindable keys; colored-prompt helpers.
- **Scripting**: case modifiers / herestrings / `[[ -v var ]]`-class
  conveniences were bash-like extras — superseded by the designed
  language; irrelevant to dash.
- **Guardrail**: the old "never break POSIX sh-mode" rule is MOOT —
  Finch has no sh-mode; dash owns POSIX.

FNX modifications carried into the design: FSH integration (paths per
fsh-proposal: settings in Configuration/finch.conf domains, behaviour
in Application Support startup scripts, history in Variable Data),
prompt per config-design (behaviour-vs-settings split), static Finch
in the recovery carve-out only if dash is ever unavailable (not v1).

## Open items

- Resolved by the from-scratch decision: sh-mode POSIX verification is
  MOOT (Finch has no sh-mode; dash is `/bin/sh`); dash retirement is
  MOOT — dash is permanent.
- zsh as a *user-installable* option under /Shared (policy-clean if
  shipped without the GPL'd function files) — out of first-party
  scope, recorded so the door is documented.
- The from-scratch milestone list lives in
  docs/design/finch-shell-plan.md (§5); open-at-execution items there
  (§6) are the forward record.

## Relationship

- Config policy: docs/design/config-design.md §0 (all config libconfig;
  Application Support carve-out + domain matching).
- Recovery/static world: docs/design/shared-libraries-plan.md.
- Current sh: dash with FSH patches (third_party/dash + dash-fsh.patch)
  — permanent (see decision above).
- Finch design: docs/design/finch-shell-plan.md.
