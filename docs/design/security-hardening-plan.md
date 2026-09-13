# Security hardening

Status: **PLAN (2026-09) — decided in direction; not scheduled.**
The authorization *model* is already designed and largely built; what
is missing is the **memory-safety, isolation and detection** layers.
`docs/reference/security-audit-record.md` records what three kernel
audit rounds already found and fixed (22 + 27 fixes + the systemic
fault-recovering-copies change) — this plan is what comes after
patching.

## 1. What already exists (do not re-plan)

- **Permissions**: POSIX ACLs as the single canonical model
  (`permissions-acl.md`, DECIDED; ACL M0–M3 + the `acl` tool done).
- **Privilege**: the principal model (`system-admin-principal.md`) —
  `System`/`Service`/`Display`/`Admin`, **no root login, no `su`/`sudo`**,
  narrow setuid-`System` helpers, verb-level authorization against the
  **real** uid, and the **elevate-last** rule (validate as the user;
  euid 0 only for the final atomic act).
- **Sessions**: `sessionmgr-design.md` (login, unprivileged display).
- **Secrets**: `keychain-plan.md` (per-item AEAD, per-item ACLs with a
  prompt, `bundle` > `verified` > `self-asserted` identity source).
- **Code identity**: `bundle-signing-plan.md` (supported, never
  required; blessing on first run) + `bundle-launch-plan.md` (inode
  marker + `execve` authorization + the `launch` helper).
- **Mediation**: `config` as the only `.conf` writer; the `install`
  contract that refuses post-install scripts and never executes bundle
  content.
- **Post-audit discipline**: verify-first, fault-recovering user-memory
  primitives; `tools/sec_test.c` as the regression harness.

## 1a. What round 4 (the userland round) added (2026-09)

`docs/reference/security-audit-record.md` §9: the image's modes are now
policy-driven and the build fails if they regress; `config` refuses
System/Shared writes without euid 0; libconfig's parser has a nesting
bound; and the toolkit bounds untrusted window geometry before any
surface allocation. What remains absent below is unchanged — none of
these are H0-H5 layers.

## 2. What is verified absent

Swept across all docs and code (2026-09): **ASLR** (none anywhere),
**stack protector** (the kernel is built `-fno-stack-protector`,
`mk/40-kernel.mk:21`), **sandboxing** (no seccomp/pledge/caps/jail),
**SMEP/SMAP**, **W^X policy**, **`noexec`/`nodev`/`nosuid` mount
flags**, **`rlimit`** enforcement, and **audit logging** — every one
returns zero mentions. `ptrace` is disabled outright, so there is also
no debugger-grade introspection. The recurring bug class from the
audits is *kernel trust of user-supplied pointers and structures*.

## 3. Milestones

### H0 — Build-time hardening
`-fstack-protector-strong` on the kernel (currently off) and userland,
`_FORTIFY_SOURCE` where musl supports it, frame pointers kept for
backtraces, and a documented release-build flag set.
**Acceptance**: `sec_test.c` + the smoke set still pass; a deliberate
stack-smash test is caught; measured cost recorded (boot time, syscall
microbenchmark) — if the cost is material it is *reported*, not
silently dropped.

### H1 — Memory protections
- **NX/W^X as policy**: NX mandatory for stack/heap/data; executable
  mappings only via explicit `mprotect(PROT_EXEC)` (JIT-legal, but
  auditable).
- **`noexec`/`nodev`/`nosuid` mount flags**, applied by default to
  `/System/Temporary Files` and removable `/Volumes`. This has a direct
  bonus: it closes much of the "copy the payload out and run it"
  ceiling that `bundle-launch-plan.md` documents honestly.
- **SMEP/SMAP** when the CPU reports them (the user-copy paths already
  exist to be wrapped in `stac`/`clac`).
- **Userspace ASLR** (mmap/stack/exec base); **kernel ASLR** assessed —
  the port already rebases its image at boot (`rebase_image_data`),
  which is most of the prerequisite.
**Acceptance**: per-protection tests in `sec_test.c`; a deliberately
non-relocated exploit binary fails under ASLR; `noexec` verified by
attempting an exec from a `noexec` mount and a removable volume.

### H2 — Untrusted-input discipline (finish the rule)
Extend verify-first/fault-recovering access to **every** syscall ABI
surface, with a **64-bit** size argument everywhere (`check_user_area`
truncation was a recurring trap — sweep it as a *helper*, not per
site), one-copy-then-validate instead of field-by-field double reads
(TOCTOU), and bounds on every id/index lookup.
**Acceptance**: the helper is 64-bit-clean; the audit record's
§5 classes each have a `sec_test.c` case; no syscall dereferences a raw
user pointer.

### H3 — Isolation and resources
- **Declarative capability model** (not syscall filtering): the app
  model already anticipates "entitlements/declared capabilities for a
  future sandbox" — scope it as kernel-enforced capabilities:
  filesystem reach, device access (which `/System/Devices` nodes),
  network, IPC, and the ability to spawn.
- **Per-process namespace / mount view** — stronger than `chroot`,
  which gives no per-process mount isolation today.
- **Resource limits**: `setrlimit` (NPROC/NOFILE/AS/CORE) actually
  enforced plus memory accounting and an OOM policy — a prerequisite
  for multi-user being meaningful rather than trivially DoS-able.
**Acceptance**: a test app denied a capability fails cleanly with the
declared error; a fork bomb hits limits instead of wedging the box.

### H4 — Detection and lifecycle
- **Audit logging** (needs the still-missing user-space log daemon):
  structured events for auth attempts, helper invocations (real uid +
  verb), keychain ACL refusals, blessing decisions, install/update,
  service transitions — policy in a `system.audit` domain, logs under
  `/System/Variable Data`.
- **Fuzzing in CI**: libFuzzer targets for AGFS/FAT/UDF parsers, the
  ELF loader, the network stack and USB descriptor parsing — host-side
  first, aimed squarely at the audit record's dominant class.
- **`sec_test.c` grows into a per-subsystem suite**; the audit register
  is updated whenever a new bug class appears.
**Acceptance**: a fuzz target runs in the build pipeline and has found
at least one real issue; an audit-log query answers "who ran `disk
mount` on this machine".

### H5 — Deferred / out of scope (recorded)
- **Secure boot / measured boot** and any hardware root of trust — and
  the honest dependency: until boot is measured, "verified identity" is
  **advisory** (the signing plan already says so).
- **Syscall filtering** (seccomp-equivalent) — deliberately *after*
  capabilities, and possibly never: declarative capabilities buy more
  for less.
- **TPM-backed key sealing** (keychain K4 inherits this).
- **`mlock`-equivalent for the keychain daemon** — folded into H1's
  memory work, and it becomes urgent **when the swap plan lands**
  (recorded in `keychain-plan.md`).
- **Network security**: a packet filter / firewall plan does not exist
  yet and belongs in its own doc (`netsec`), not bolted onto this one.

## 4. Decisions this plan forces (before dependents land)

1. **Update-channel integrity.** Signing is "supported, never
   required" — but the planned updater applies *remote code*. Updates
   should be **signed or digest-pinned**, or the update channel is the
   weakest link in the system. This is a deliberate, narrow exception
   to the never-required rule, and it should be decided **before the
   updater is implemented**, together with rollback/downgrade
   protection.
2. **Capability granularity** (H3): per-app declared capabilities must
   be expressive enough for the real catalog (Viewer needs files;
   Terminal needs a pty and spawn; Disks needs raw devices) without
   becoming an ACL engine of its own.
3. **Audit versus privacy** (H4): what is logged, retention, and who
   can read it — a single-user OS can log aggressively, a multi-user
   one cannot log other users' command lines the way round 3 just
   fixed procfs to prevent.

## 5. Sequencing rationale

H0–H2 are cheap, independent, and each deletes a bug class (stack
smashing, unprotected mappings, unverified copies). H3 is the
structural project and should follow the app-model work it depends on.
H4 needs the logging daemon and is where the record becomes a
*process*. H5 stays out. Nothing here requires SMP, the accel work, or
any third-party adoption — this is self-contained kernel + userland
work, which is why it can be scheduled at any point.
