# Finch shell plan

Status: **PLAN (2026-09) — from-scratch, fully designed, not yet implemented.**

Finch is FNX's **user shell**: a from-scratch interactive shell and
script interpreter written in C, with a designed language — rc
semantics under a C-skin syntax — and full POSIX job control. It is
deliberately **not** `/bin/sh`: dash remains the POSIX script shell
(recovery shell, FSH-patched, `/bin/sh` and script runner) forever.
Finch and dash coexist; neither pretends to be the other.

Working name Finch (bird-kin: Argentum UIKit, Kestrel the WM, Finch the
shell). "fsh" rejected — FSH already names the filesystem hierarchy.

Supersedes the mksh-fork direction in `docs/eval/shell-eval.md` (see
history below). Related: `docs/design/config-design.md` (settings
domains), `docs/design/fsh-proposal.md` (paths), `docs/design/
shared-libraries-plan.md` (static carve-outs).

## 1. History / why from-scratch

The shell direction was, in order:

1. **Fork of mksh** as a single "sole system shell aliased sh"
   (eval cb3ad57) — the economy argument: one POSIX-correct base, one
   binary serves `/bin/sh` and interactive.
2. **mksh out (personal reasons, 2026-09)**. Re-evaluated: oksh
   (portable OpenBSD ksh — same ksh-family case, different project),
   dash fork, from-scratch. User chose **from-scratch**.
3. **Two-shell model** dissolved the fork debate's premise: a
   from-scratch shell should not pay POSIX-conformance cost to
   re-implement what dash already is. dash is `/bin/sh`, forever;
   Finch is the user's shell, and owes no sh-mode, no sh-compat
   guardrail, no POSIX torture corpus.

Design sessions then fixed the language (rc semantics + C-skin),
control flow, funcs/scoping, process model, interactive surface, and
startup/config — the specification below.

## 2. Language specification

### 2.1 Identity

- Finch is the **user shell**: interactive login shell and script
  interpreter (`#!` scripts — `#` is a comment, so shebang lines
  parse). Non-interactive runs skip user startup and prompt config.
- Full POSIX **job control** on the console and ptys.
- Written in C, first-party (`userland/finch/`), installed as
  `finch` at `/System/Tools/finch`. dash untouched at `/bin/sh`.

### 2.2 Semantics (rc-core)

- Every value is a **list of words**. `$x` expands to its elements;
  `"$x"` joins to one word (space-separated). No word-splitting on
  expansion — IFS has no role; expansion never re-splits.
- **Functions over aliases**: `func` is the only mechanism; there are
  no aliases and no `trap` string-eval. Signals are funcs.
- **Status as a value**: `$status` holds the last command's exit.
- **Globbing**: unquoted words glob at expansion; no match yields the
  literal word (rc rule; no nullglob errors, no surprise empties).
- Grammar stays small enough to hold in one's head.

### 2.3 Syntax (C-skin)

- Control flow: `if (…)`, `for (…)`, `switch (…)`, `while (…)`,
  braces `{ }`, infix comparisons, `//` and `/* */` comments (`#` too).
- `$x` variable spelling (bare words are never variables).
- `func f(a, b) { }`; calls `f(1, 2)` with parens + commas.
- Strings: `"…"` interpolates (`$x`, escapes); `'…'` is literal.
- Conditions: content with an operator/comparison/call/`$(`/`$x` is an
  **expression** (empty list false, else true); otherwise it is a
  **command list** tested by exit status. `&&`/`||` between commands
  inside a condition are rc-style status logic.
- `switch ($x) { case <pattern>: … default: … }` — pattern dispatch
  (first match wins), no fallthrough, `break` unnecessary.
- `for (i = 0; i < n; i++)`: header assignments resolve outward.
  `for (x in $list)`: x binds fresh per iteration, loop-local.
- Pattern tests use the **`matches`** keyword: `if ($x matches *.c)`.
  `switch` cases share the same pattern language.
- Integers auto-detected in `+ - * / % < <= > >= ++ --` and `+=`
  when both operands look numeric; `$list += (a b c)` appends.
- Pipeline: `a | b | c`; `$status` = last command's exit.
- Redirections sh-spelled: `< f`, `> f`, `>> f`, `2> f`, `2>&1`,
  heredoc `<<EOF` (editor enters continuation; body is literal).
- Capture: `x = $(cmd …)` → list of output words; `$(<file)` reads a
  file into a list.

### 2.4 Scoping & functions

- `{ }` creates **real scope**. `local x = 3` declares a
  block-scoped binding; plain `x = 3` assigns the nearest existing
  binding outward — global if none. (`local` is the C declaration,
  keyword-flavored; assignments behave exactly like C's.)
- Funcs are **C-lexical closures**: they see globals + their own
  params/locals only — never the caller's locals. Closures capture
  their defining scope.
- Funcs are **value-returning**: `return <list>` yields a value; a
  call in an expression context (`x = f(1, 2)`, comparison operand)
  captures it; a bare `f(1, 2)` statement runs for effect; `$status`
  remains set from the body.
- **`export` is a binding attribute**: `export x = 3` creates an
  exported binding; later `x = 4` updates the child environment;
  `export x` exports an existing binding. Reads like C's storage
  class next to `local`.

### 2.5 Interactive surface

- One **modeless editor** (raw mode, ISIG off; ^C cancels the line,
  ^Z keeps job-control meaning). Readline-grammar defaults
  (^A/^E/^K/^U/^W, arrows, ^P/^N history, ^L redraw, ^D eof/delete),
  all bindable via `.conf`.
- **UTF-8-native**: codepoint cursor movement, wcwidth rendering, the
  buffer never splits a multibyte char.
- **Live reparse**: the parser re-runs per keystroke — syntax
  coloring, inline error marking (unterminated quote/comment, bad
  token), and construct-state awareness; incomplete constructs enter
  multi-line continuation with auto-indent on `{`/`}`.
- Terminal contract: a tiny built-in capability model (FNX console,
  xterm-class), SGR colors (Argentum palette via config later). No
  terminfo.
- History: ^R incremental search + prefix recall; **no** `!!`/`!n`
  expansion. Stored as complete parsed commands; **appended per
  command** to `/Users/$USER/Variable Data/finch_history` (+ periodic
  trim), so crashes never lose a session.
- Completion: **token-aware** (first word → commands/funcs,
  `$`-prefixed → variables, after `<`/`>`/`switch` → files), tab
  completes, ambiguity renders a list below the prompt with arrow
  selection.

### 2.6 Startup & config (per FSH + config-design)

- **Settings are data** — a `finch.conf` domain (libconfig):
  prompt string, editor key bindings, colors, history size,
  completion menu style, options. Defaults at `/System/Configuration/
  finch.conf`, per-user override at `/Users/$USER/Configuration/
  finch.conf` (config-design precedence).
- **Behaviour is code** — a startup script in Finch's own language,
  defining funcs, exports, hooks. Sourced at interactive start:
  `/System/Application Support/…` then `/Users/$USER/Application
  Support/…`.
- **Prompt** = `.conf` string with escape tokens (%u user, %h host,
  %d cwd, %# root marker, color runs). A computed prompt (func) is a
  deferred power-user override.
- **User Template** (`/System/User Template/`) seeds each new user
  with a default `Configuration/finch.conf` + startup script.

### 2.7 Special variables

`$status` (last exit), `$argv` (script/command args; func params are
named and do not consume it), `$pid`, `$ppid`, `$cwd`. No option-letter
variable (`$-`) — replaced by real settings.

## 3. Deliberately deferred

- Fuller numeric story (bases, floats, `$(( ))`-style arithmetic
  context) — only loop counters + comparisons are v1.
- Programmable completion scripting (bash-grade) — token-aware
  built-ins first; a completion API later if a consumer appears.
- Prompt as a func (computed prompts).
- `until` / `do…while` / aliases / history `!` expansion / `trap`
  string-eval — none exist; add only if a real need appears.
- Clipboard integration with the desktop terminal (X selection) —
  when Kestrel/Argentum terminal lands.

## 4. Kernel contract

Verified present in FNX today: `setsid`, `setpgid`, `kill` (negative
pid → group), `TIOCGPGRP`/`TIOCSPGRP`/`TIOCSCTTY`
(drivers/char/tty.c), ^C → SIGINT and ^Z → `kill_pgrp(tty->pgid,
SIGTSTP)` on the foreground group, termios family.

**Missing — one kernel delta, part of this plan (M3):**
`wait4` lacks **WUNTRACED/WCONTINUED** support. A job-control shell
must learn when a background job stops/continues (SIGCHLD +
`waitpid(…, WUNTRACED)`). The delta: stop/continue reporting in
`kernel/syscalls/wait4.c` + SIGCHLD delivery with stop information.
Small, well-understood; the only kernel work the shell needs.

## 5. Milestones

Each milestone is independently verifiable; acceptance criteria are
exact. Implementation in C (musl), first-party.

### M0 — Parser + AST
Tokenizer (comments `//` `/* */` `#`, quotes, `$`, strings,
numbers, heredoc marker), recursive-descent parser producing an AST
with error positions; parser must be cheap/re-entrant enough for
per-keystroke reparse later.
**Acceptance**: a parse-test corpus (every syntax form in §2) parses;
malformed input yields a position + message; `fuzz`-style junk input
never crashes.

### M1 — Evaluation core
Word expansion (lists, no splitting, `$x` vs `"$x"`, interpolation,
`matches`, globs), command execution (external, `$( )` capture,
`$(<file)`), pipelines, redirections, the condition rule, `$status`.
**Acceptance**: scripted corpus — expansion edge cases, pipeline
statuses, redirections including `2>&1` and heredoc — runs correctly
against dash's answers where the behavior overlaps.

### M2 — Control flow + funcs + scoping
`if/for/switch/while`; `{ }` scope; `local`; C-lexical closures;
value returns; `f(1, 2)` calls; `export` attribute; special vars.
**Acceptance**: the scoping/closure suite (mutual recursion, closures
capturing locals, `local` shadowing, assignment-resolves-outward)
passes; a `switch`-pattern and `for`-binding corpus passes.

### M3 — Process model + job control (+ kernel delta)
`&`, job table, `jobs`/`fg`/`bg`, ^Z suspend/continue, foreground
process-group discipline on the tty. **Kernel delta**: WUNTRACED/
WCONTINUED in wait4 + stopped-child SIGCHLD reporting.
**Acceptance**: a PTY script drives the shell: run a sleep in the
background, ^Z the foreground, `jobs` shows it, `fg` resumes, exit
statuses survive; kernel delta committed and regression-tested.

### M4 — Interactive surface
Raw-mode editor (UTF-8/wcwidth, bindings), live reparse (coloring,
error marking, continuation + auto-indent), ^R history with
append-per-command persistence + trim, token-aware completion menu.
**Acceptance**: interactive harness types multi-line `func` bodies,
multi-byte text (cursor never splits a char), incomplete `{` enters
continuation with indent, `$x` completion lists variables, history
survives kill -9 (appended entries present on restart).

### M5 — Config & startup
`finch.conf` domains (system + per-user override), prompt escapes,
bindings from config, startup scripts (system + user Application
Support), User Template seeds, non-interactive/shebang mode.
**Acceptance**: a changed prompt/color/binding in the user `.conf`
overrides the system default; a func in the user startup script is
present in an interactive session and absent in a non-interactive run;
a `#!` script executes.

### M6 — Integration & hardening
Finch wired as the interactive user shell (init/login path), dash
untouched at `/bin/sh`, recovery carve-out unaffected, edge/torture
corpus (quoting, expansion, heredoc interplay), crash-consistency
tests for history appends, fshlint clean, docs updated.
**Acceptance**: boot to an interactive Finch on the console; the whole
existing userland script corpus still runs under dash; kill-cycle
tests show no history loss and no job-table corruption; all acceptance
suites of M0–M5 still green.

## 6. Open at execution

- Default keybinding map details (the ^-set + arrows + menu keys).
- Exact `finch.conf` grammar/keys (with libconfig domains).
- User Template seed content (default prompt, starter funcs).
- `matches` precedence vs other infix operators (parser detail).
- Kestrel/Argentum desktop terminal: TERM identity + capability entry
  in the built-in model.
