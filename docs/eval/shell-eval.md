# Shell evaluation — FNX system shell options

Status: **EVALUATION (2026-09)** with a recorded **leaning: mksh as the
sole system shell, with FNX modifications** — direction, revisit at
adoption (the live `/bin/sh` today is dash, with FSH patches).

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

## Leaning: mksh as the sole system shell, with FNX modifications

**One shell, both jobs** — mksh becomes `/bin/sh` *and* the interactive
login shell; dash retires from the running system (its role subsumed).
Reasons: permissive, musl-clean, single small codebase, editing built
in, Android precedent, Korn superset means no sh-vs-interactive split
to maintain. FNX modifications (recorded; scoped at adoption):

- **FSH integration** (the dash-FSH patch pattern): environment,
  home/PATH defaults under FSH (`/Users/<user>`, no `/etc`), PS1 and
  startup defaults for the FNX desktop.
- **sh-mode semantics**: verify mksh's behavior invoked as `sh`
  (POSIX-mode strictness) — the sole-shell claim depends on it being a
  correct `/bin/sh` for FNX scripts and configure runs.
- **Init/rc under the config policy**: startup customization is
  behaviour, not settings — lives in
  `Application Support/` per the policy (domain-matched `<app>`
  subdir), not a dotfile zoo.
- **Recovery set**: a static mksh joins the static recovery carve-out
  (replaces static dash there too, if dash is fully retired).
- **fshlint**: zero-allow on the shell binary like all System/Tools.

## Open items

- sh-mode POSIX verification (mksh invoked as `sh` vs strict POSIX).
- Whether dash is *fully* retired or kept only as the static recovery
  sh during the transition.
- zsh as a *user-installable* option under /Shared (policy-clean if
  shipped without the GPL'd function files) — out of first-party
  scope, recorded so the door is documented.

## Relationship

- Config policy: docs/design/config-design.md §0 (all config libconfig;
  Application Support carve-out + domain matching).
- Recovery/static world: docs/design/shared-libraries-plan.md.
- Current sh: dash with FSH patches (third_party/dash + dash-fsh.patch).
