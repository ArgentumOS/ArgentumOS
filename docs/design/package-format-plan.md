# Native package format — AGFS volume images

Status: **PLAN (2026-09) — decided in direction; not scheduled.**
A distributable package is a **small AGFS volume image** (`.pkg`). Install
is a validated subtree copy; ACLs, extended attributes and modes come
across **by construction**, because source and target are the same
filesystem.

## 1. The three decisions

1. **Container: an AGFS volume image** (not a stream format, not tar).
2. **Metadata authority: modes only** — packages never carry ownership
   and **never** setuid/setgid, device nodes or hooks, ever.
3. **Non-AGFS targets: refuse when lossy** — if the target filesystem
   cannot hold what the package declares, the install fails with a
   precise reason rather than degrading.

These cohere: because the container is a real AGFS volume, fidelity is
the *default* — and because the deliverable is a `.pkg` file, a
mountable artifact, the strictness of (2) and (3) is affordable.

## 2. Why a volume image (benefits, and the costs taken on)

- **Fidelity by construction**: AGFS stores extended attributes and
  POSIX ACLs natively (ACLs ride as xattrs —
  `fs/agfs/{attribute,xattr}.c`); a copy between two AGFS volumes
  preserves them without a translation layer, and `mode`, timestamps
  and symlinks come along.
- **Inspection for free**: a `.pkg` can be **mounted read-only and
  looked inside** like any volume — users can examine what a package
  will do before installing. This is the property that most justifies
  the container choice.
- **Reuses existing tooling**: `tools/mkagfs.py` (the volume writer) and
  `tools/agfscheck.py` (the validator) already exist; the package
  builder is a validated profile on top of them.
- **Signing is simple**: the signed unit is the image *file*, so the
  digest is over its bytes (§7).
- **Costs accepted**: updates ship **whole images** (no delta updates —
  a consequence of this container, recorded in §10); the geometry
  carries fixed overhead for small packages; and the installer needs a
  **file-backed mount** that does not exist yet (§4).

## 3. What a package carries — and what it never does

**Carries**: regular files (with **permission bits only**), directories,
symlinks; **extended attributes and ACLs**; the `.conf` manifest
(`app-model.md` §3) extended with a **metadata declaration** — e.g.
`requires = xattr acl` — which is what drives the fidelity rule (§6).

**Inert**: the image's `uid`/`gid` fields are **ignored at install**;
ownership is forced to the scope owner (`System` for a global install,
the calling user for a local one), matching the existing origin model.

**Refused at build, defensively stripped at install**:
- **setuid/setgid bits** — `mkpkg` rejects them; `install` clears and
  **records** any it sees (never silent);
- **device nodes, FIFOs, sockets** — a whitelist of file/dir/symlink
  only;
- **hard links** (nothing in the bundle model needs them);
- **scripts/hooks of any kind** — already banned by the install
  contract.

**ACL entries are account-name based, and restricted to the standard
system accounts** (`System`, `Admin`, `Users`, `Service`, `Display`).
Numeric/arbitrary-user entries are refused: packages carry no
ownership, so a numeric uid in an ACL would reference an account that
does not exist on the target — meaningless at best, hostile at worst.
Resolution by name happens at install.

## 4. The enabling kernel work: a loop block device

**There is no loopback mount today** (verified 2026-09):
`drivers/block/` has no loop driver, and `mount()` accepts only
`S_ISBLK` sources for filesystems that declare `FSOP_REQUIRES_DEV`
(`kernel/syscalls/mount.c:120-135`, otherwise `-ENOTBLK`) — so a
regular file **cannot** be a mount source. The nearest existing
primitive is the **ramdisk** (major 1, 10 minors, memory-backed via
`memcpy_b`, sized at boot by `ramdisksize=`; slot 0 is the initrd from
a boot module's memory): RAM-backed, boot-configured, with no userland
attach — the initrd mechanism, not a loop.

**Decision: implement a loop block device**, rather than teaching AGFS
about vnodes. A block device whose read/write callbacks resolve to the
pages of a backing file:

- reuses all existing block plumbing (buffer cache,
  `bread`/`brelse`, partition scan, the `disk` helper) and *is* a block
  device — so `mount` needs **no change at all**;
- works for **every** filesystem unchanged: `.pkg` (AGFS) now,
  ISO9660/FAT/ExFAT images and backup volumes later;
- enforces read-only at the device level, which is where §3's
  modes-only discipline belongs;
- gives `disk` the verbs (`disk loop attach <file> [--ro]`,
  `disk loop detach <dev>`), with inspection a normal mount.

The plan's only kernel guarantee: mounting an image **executes
nothing** — it is a device, and ingest validation (§5) is what guards
the content.

## 5. Install = one validated subtree copy

The traversal rules are the audit record's classes applied to package
ingest (`docs/reference/security-audit-record.md` §5) — this is
**attacker-controlled content**, exactly the class that has bitten
three times:

- walk the source **without following symlinks**; entry-type whitelist;
  refuse specials;
- path validation (no `..`, no absolute paths, the identifier is never a
  path component, containment verified per entry — the existing install
  contract);
- **count and size caps** (the kmalloc-DoS class);
- metadata copy: modes, xattrs/attributes, ACLs (name-resolved);
  **uid/gid forced**; setuid stripped **and recorded**;
- no exec of bundle content — *validated data motion*, unchanged;
- an **install record** (a config domain) for `uninstall`, which matters
  for shared-resource payloads that land in `/Shared`.

## 6. The fidelity rule — refuse when lossy

Before writing a byte, `install` checks the target filesystem against
the package's metadata declaration. If the target cannot hold it, the
install **fails with a precise reason**.

A verified consequence: **the ext2 driver has no xattr support at all**
(`fs/ext2/` contains none; neither do FAT/minix), so a package that
declares `xattr`/`acl` is effectively **AGFS-only** in v1. That is
acceptable — app bundles install to `/Applications` and
`~/Applications`, both AGFS — and the rule is *per metadata*: a package
declaring nothing installs anywhere the mode bits survive. Recorded
alternative (a future decision, not now): a `--degrade` flag that
installs anyway with an explicit report.

## 7. Signing and updating

- **Detached signature**: `<name>.pkg.sig` next to the package, over the
  **image bytes**. A package cannot be self-signed (the signature would
  be inside the digested content) — hence detached, which also matches
  the existing model of a `signature` file beside `manifest`.
- **Whole-image digest** replaces the per-file notion: per-file digests
  are *not* part of this format, which is what makes delta updates
  impossible (§10).
- `install` reports `verified (key …)`/`unverified` and installs either
  way; the **first-run blessing** is unchanged
  (`bundle-signing-plan.md`, `bundle-launch-plan.md`).
- The **updater ships whole images** and must be signed or
  digest-pinned per the decision `security-hardening-plan.md` §4
  records; rollback keeps the previous image, so budget disk for it.

## 8. Tooling

| Tool | Role |
|---|---|
| `mkpkg` | bundle tree + manifest → `.pkg`; validates the whitelist, **rejects setuid/devices/hooks**, stamps the metadata declaration; built on `mkagfs.py`'s writer |
| `agfscheck` | validates the image (already exists) |
| `install` / `uninstall` | ingest (§5) and removal (install record) |
| `disk loop attach/detach` | file-backed read-only inspection (§4) |

## 9. Milestones

### W0 — Loop block device
A file-backed block device (`disk loop attach/detach`; read-only flag).
**Acceptance**: a package image attaches, mounts read-only and lists
correctly; a corrupt or truncated image fails cleanly (no panic, no
partial mount); writes on a `--ro` device are refused; `detach`
teardown is clean.

### W1 — `mkpkg` and the package profile
The builder (whitelist validation, metadata declaration, label carries
the identifier), plus a recorded geometry decision (block size; journal
present-but-empty so the image stays a valid AGFS volume).
**Acceptance**: a package round-trips directories, modes, symlinks,
xattrs and ACLs **byte-exact** when mounted; a setuid file or a device
node is rejected at build time with a clear error.

### W2 — `install` ingest
Copy-subtree with metadata; ownership forced; setuid stripped and
reported; traversal rules and caps (§5); the fidelity refusal (§6).
**Acceptance**: an ACL-carrying package installs to `/Applications` and
`acl get` on the result matches the package; the same package refused
onto FAT/ext2 with the precise reason; a package containing a setuid
file reports the strip; a package with `..` or an escaping symlink is
rejected.

### W3 — Signing wiring
Detached `.sig`, `verify` over the image digest, fingerprint display for
untrusted keys, blessing unchanged. **Acceptance**: one flipped byte
fails verification; an unsigned package takes the blessing path.

### W4 — Updater and uninstall
Whole-image replacement with the prior image retained for rollback;
`uninstall` uses the install record (especially for `/Shared`
payloads). **Acceptance**: an update replaces an app and a rollback
restores the previous image; uninstall removes a shared-resource payload
completely.

## 10. Out of scope (recorded)

- **Delta/incremental updates** — impossible with a whole-image digest;
  adding them is a new format decision, not an extension.
- **tar/cpio ingestion**: third-party content must be repackaged with
  `mkpkg`. A tar *import* tool is a possible later convenience; it is
  not this format.
- **Multi-payload / multi-scope packages**: one payload kind plus routed
  shared resources, per the existing install contract.
- **Source packages** (the self-hosting manifest covers sources
  separately) and app sandboxing (`security-hardening-plan.md` H3).

## 11. Relationship

Extends `app-model.md` (the manifest gains `requires`), constrains
`system-admin-principal.md`'s install contract (ingest rules),
preserves `permissions-acl.md` (ACLs as xattrs, name-resolved),
integrates with `bundle-signing-plan.md` (detached signature;
whole-image instead of manifest+payload digest) and the updater
(`shared-libraries-plan.md`), and hands `security-hardening-plan.md` two
fuzz targets: **the AGFS image reader** and **the ingest walker**.
