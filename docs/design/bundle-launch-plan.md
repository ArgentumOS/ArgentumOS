# Bundle launch — the sanctioned door and exec authorization

Status: **DECIDED (2026-09) — mechanism unimplemented.**
Closes the direct-exec bypass documented in
`docs/design/bundle-signing-plan.md` §3: a bundle's payload
executables are **marked at install** and **refuse to exec** without
a launcher-established bundle identity, so a bundle cannot be run
from the command line.

## 1. The sanctioned door

**`launch`** — a setuid-System helper in the established helper
family (`power`/`account`/`disk`/`install`/`config`), argv[0]-dispatched
twins as needed (`launch <bundle>` / `open <bundle>`, the latter for
the shell and the file manager). A privileged launch *service* was
considered and rejected: the helper adds no daemon and no long-lived
privileged surface.

Order of operations (the elevate-last rule applies):

1. Resolve the bundle; read and validate the manifest **as the
   calling user** (euid dropped).
2. Check run authorization — signature trusted? else the blessing
   prompt (bundle-signing-plan §3) — still unprivileged.
3. **The one privileged act**: set the kernel's bundle-identity flag
   (only possible with euid System) and `execve` the payload.
4. The kernel verifies the payload's marker, grants the identity, and
   runs the payload **with the user's uid** — `euid 0` exists only
   inside the helper's validate-and-exec window and **must never be
   inherited by the app** (an explicit acceptance test in N1).

## 2. Bundle instance identity

A per-process identity — `(bundle identifier, bundle instance)` —
established **only** by the launcher and:

- **inherited across `fork` and `exec`** by descendants, so an app's
  own helper binaries inside its bundle work normally;
- **single-bundle**: a process carries at most one; exec'ing
  *another* bundle's marked payload is refused;
- **unforgeable**: only an euid-System process can set it, so a shell
  cannot fake it (`PF_*`-style flag in `include/fnx/process.h`).

It also strengthens identity for **keychain ACLs**: the identity
*source* is now ordered `bundle` (launcher-established) > `verified`
(signature) > `self-asserted` (neither) — a launcher-established
identity cannot be claimed by an unrelated binary.

## 3. Kernel exec authorization

- **The marker**: a new AGFS inode flag **`BUNDLE_ENTRY`** in the
  spare bits of `struct agfs_inode.flags` (`include/fnx/agfs.h` —
  only `IN_USE`/`INLINE_DATA`/`LONG_SYMLINK`/`ATTR_INODE` are taken).
  Additive: old volumes simply have it clear and an old driver
  ignores the bit, so **no format break**. Set by `install` on the
  bundle's payload executables (both scopes) — and on bundled
  script files, since a shebang script is exec'd through the same
  path.
- **The check**: in `do_execve()` (`kernel/syscalls/execve.c`, beside
  the existing `S_ISUID` handling) — if the inode carries
  `BUNDLE_ENTRY`, require that the calling process already carries
  the identity **for that bundle**; otherwise refuse with `EACCES`.
- System tools are unmarked, so a bundle can exec them freely; the
  refusal is specific to payloads reached without a launcher.

## 4. What this stops — and what it cannot (honest ceiling)

**Stops**: the casual and accidental direct invocation
(`./Foo.app/bin/Foo`, an absolute path, from any shell or script) —
refused by the kernel, with the shell printing a pointer to the
launcher. For **`/Applications` (global)** bundles this is *hard*:
the payload is System-owned, so the user cannot clear the marker.

**`~/Applications` (local)** bundles are enforced by default, but the
owner can **clear the marker** on their own files — their guardrail
to disable, exactly like `chmod`. Recorded, not a bug.

**Cannot stop**: **copying the payload elsewhere**
(`cp Foo.app/bin/Foo ~/foo && ./foo`) — a copy is a different,
unmarked file, so it runs as an identity-less process (with the
keychain consequence: no `bundle` identity → `self-asserted`). This
is the honest ceiling: the mechanism prevents **in-place direct
execution**, not the ability to run the code. Like the blessing
prompt, it is a guardrail, not a security boundary.

## 5. Interactions

- **keychain ACLs**: identity source ordering (§2) — amend
  keychain-plan.
- **`install`**: marks payload inodes (§3) — amend the install
  contract in system-admin-principal, and add `launch` to the helper
  list.
- **app-model launch flow**: step 3 *is* the launcher; the earlier
  note that a bundle binary "can also be run directly" is replaced by
  the enforcement rule.
- **bundle-signing-plan §3 limits**: the direct-exec bypass becomes a
  refusal for marked payloads; the surviving limit is the copy-out.
- **UX**: the refusal must be legible — the shell/terminal prints
  "this is a bundle payload; launch the bundle" rather than a bare
  `EACCES`.

## 6. Milestones

### N0 — Marker + kernel exec check
`AGFS_INODE_BUNDLE_ENTRY`, `install` marking payload executables
(global + local), the `do_execve()` authorization check.
**Acceptance**: with the flag set by a privileged test harness (the
helper does not exist until N1), a marked payload is refused from a
shell with `EACCES` while an unmarked binary is unaffected; an old
driver on a marked volume ignores the bit (no enforcement, no
corruption).

### N1 — The `launch` helper + identity
The setuid-System helper, identity establishment and inheritance,
the elevation discipline. **Acceptance**: launching a bundle works
end to end; the app's children (including its own in-bundle helpers)
inherit the identity; exec'ing a *different* bundle's payload is
refused; **the app never runs with euid 0** (asserted explicitly);
the blessing prompt still appears for unsigned bundles.

### N2 — Identity source in the keychain + local-scope semantics
ACL entries record `bundle` identity where present; documented
local-bundle behaviour (marker clearable by the owner); the legible
shell refusal. **Acceptance**: an app launched via the launcher gets
a `bundle`-source ACL match without a prompt where granted; the same
payload copied out and executed does not; clearing a local marker
restores direct execution (documented).

## 7. Out of scope (recorded)

Sandboxing/containment of the running app (a separate future plan),
preventing copies of payloads, exec-time signature verification
(never — the signing policy), entitlements/capability enforcement,
and extending the marker to System tools.
