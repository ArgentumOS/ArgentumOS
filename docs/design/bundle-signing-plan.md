# Bundle signing — supported, never required

Status: **DECIDED POLICY (2026-09) — mechanism unimplemented.**
Bundle signing is **supported but not required**: unsigned bundles
install and run exactly as they do today. A signature is surfaced as
a **verified/unverified status**, never a gate.

## 1. Why not a gate (the honest framing)

There is no secure boot and no enforced verification chain, and the
loader will not check signatures (§3) — because requiring them would
contradict this policy. So a signature is **not a security
boundary**: malware can be signed as easily as it can be unsigned,
and an unsigned binary runs identically to a signed one. What
signing actually buys:

- **Integrity / tamper evidence** for bundles that travel between
  machines (a downloaded bundle can be shown unmodified).
- **A stronger identity** for mechanisms that must distinguish apps
  — the **keychain ACLs** are the first real consumer (§3).
- **Provenance** for bundles delivered by the (planned) update
  channel: a signed update can be attributed to the same key as the
  running system.

## 2. Mechanism

- **Detached signature over a canonical bundle digest**: the digest
  covers the manifest plus the payload tree, with the manifest as
  the authoritative index (so the digest is computable without
  trusting file-system ordering; symlinks resolved per the install
  rules).
- **Signature file**: a `signature` file next to `manifest` inside
  the bundle. Its presence is optional; its absence is *normal*.
- **Keys**: Ed25519 (or RSA) through `libcrypto`. Signing keys are
  stored **in the keychain** (the System or the user keychain per
  scope) — the first consumer of the keychain's private-key item
  kind, and a natural fit: the key never exists as a plaintext file.
- **Trust**: **self-signed keys are the norm** (personal software);
  trust means "this key is in my keychain / in the trust domain".
  No CA hierarchy is required. An optional developer trust domain
  (a `.conf` trust list, separate from the TLS CA anchors) can name
  keys that the *system* trusts for provenance purposes.
- **Tooling**: `sign` and `verify` verbs in the install family
  (`/System/Tools/sign`, argv[0]-dispatched twins like
  install/uninstall); `verify` is available to any consumer; nothing
  calls it as a gate.

## 3. Where verification is used (and where it is not)

| Consumer | Behaviour |
|---|---|
| `install` | verifies if a signature is present and **reports** `verified (key …)` / `unverified`; **installs either way** |
| **keychain item ACLs** | the one place signing changes behaviour: ACL entries record the identity's **verification strength** (see below) |
| updater (shared-libraries plan) | reports the signing key of an update; unsigned updates still apply |
| loader / exec | **never** — no signature check at launch (that would be a requirement) |
| file manager / launcher | may display the status marker; no gating |

**Keychain ACL semantics** (the design consequence): an ACL entry
names an app identity *and* how that identity was established —
`verified` (signature checked against a trusted key) or
`self-asserted` (unsigned). Default behaviour is unchanged: the
prompt still appears for unsigned apps, and "Always" persists —
but as a **self-asserted** entry, flagged in the Keychain UI.
Individual items may **opt into "verified identity required"**, in
which case an unsigned app is refused silently. Opt-in, item-level,
never global.

## 4. Milestones

### B0 — Digest + sign/verify tools
Canonical digest (manifest + payload, symlink rules) and the
`sign`/`verify` pair; `signature` file support in bundles;
`install` reports status. **Acceptance**: a modified bundle fails
`verify`; an unsigned bundle installs byte-identically to today;
the digest is stable across two runs on the same tree.

### B1 — Keys in the keychain
Key generation and use through the keychain API (no plaintext key
files); `sign` reads its key from the keychain. **Acceptance**: sign
a bundle with a keychain-held key; verify with the corresponding
public key; the private key never appears on disk in the clear.

### B2 — Consumer wiring
Keychain ACL verification strengths + the prompt/flag behaviour of
§3; updater reports the signing key. **Acceptance**: a
`verified identity required` item refuses an unsigned app silently
and accepts a signed one; an unsigned app still passes ordinary
items with the prompt.

### B3 — Developer trust domain (deferred)
The optional `.conf` trust list naming system-trusted keys, for
provenance in the update channel.

## 5. Out of scope (recorded)

Secure boot / boot-path verification (kernel, loader, firmware),
forced verification anywhere, revocation infrastructure
(CRL/OCSP-like), notarization services, per-ELF verification at
exec, and entitlements/capability enforcement (that is the future
sandbox, a separate plan).
