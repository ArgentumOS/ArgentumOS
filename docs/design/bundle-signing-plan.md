# Bundle signing — supported, never required

Status: **DECIDED POLICY (2026-09) — mechanism unimplemented.**
Bundle signing is **supported but not required**: unsigned bundles
install normally. A signature is surfaced as a **verified/unverified
status**, never an install-time gate — but an **unsigned bundle
requires a one-time user blessing on first run** (§3).

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

## 3. Unsigned bundles: one-time blessing on run

Launching an **unsigned** bundle — or one whose signature key is not
trusted — for the first time prompts the user once, in the launch
path: **Run** / **Run Once** / **Cancel**. Approval persists as a
per-user **blessing record**; afterwards the bundle launches
silently. A bundle signed with a **trusted** key launches with no
prompt at all.

- **The record**: a per-user `.conf` domain (`blessed-apps`) in the
  user's `Configuration/`, keyed by app identifier and **bound to the
  bundle's content digest** — so a tampered or *updated* bundle
  invalidates its blessing and prompts again. Blessings are
  per-user: each person blesses on their own first run.
- **Install never blesses** (§2's contract is unchanged): install is
  validated data motion and reports signature status; blessing is a
  run-time, user-authorised act, whether the bundle arrived by
  download, removable media, or a `local` install the user did
  themselves.
- **Informed prompting**: if a signature exists but its key is
  untrusted, the prompt shows the key fingerprint — blessing is a
  decision about a *known* key, not a guess.
- **Honest limits**: this is a **safety prompt, not a security
  boundary**. It lives in the launch path, and the ELF loader never
  checks signatures. Direct execution of a bundle payload is
  **refused by the kernel**, not merely unprompted: payload
  executables are marked at install and require a launcher-created
  bundle identity (docs/design/bundle-launch-plan.md). What remains
  possible is **copying the payload out** of the bundle and running
  the copy — a different, unmarked file that runs identity-less. It
  also does not make the app sandboxed, and it does **not** grant a
  verified identity: an unsigned app stays `self-asserted` for
  keychain ACLs, and an item marked *verified identity required*
  still refuses it silently.

## 4. Where verification is used (and where it is not)

| Consumer | Behaviour |
|---|---|
| `install` | verifies if a signature is present and **reports** `verified (key …)` / `unverified`; **installs either way** |
| **keychain item ACLs** | the one place signing changes behaviour: ACL entries record the identity's **verification strength** (see below) |
| updater (shared-libraries plan) | reports the signing key of an update; unsigned updates still apply |
| **launch path** (launcher / open-with / Workspace) | checks the **blessing record** for unsigned or untrusted-key bundles; prompts once (§3); signed-with-trusted-key passes silently |
| loader / exec | never checks **signatures**; `execve` **does** check the bundle marker + identity (docs/design/bundle-launch-plan.md) — a direct exec of a marked payload is refused |
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

### B2 — Blessing on first run
Per-user `blessed-apps` domain; the launch path's prompt (Run / Run
Once / Cancel) and record; digest binding; fingerprint display for
untrusted keys. **Acceptance**: an unsigned bundle prompts exactly
once and launches silently thereafter; modifying the bundle
re-prompts; a signed-with-trusted-key bundle never prompts; running
the bundle binary directly from a shell bypasses the prompt (the
documented safety-not-security limit); `install` still never
prompts.

### B3 — Consumer wiring
Keychain ACL verification strengths + the prompt/flag behaviour of
§3; updater reports the signing key. **Acceptance**: a
`verified identity required` item refuses an unsigned app silently
and accepts a signed one; an unsigned app still passes ordinary
items with the prompt.

### B4 — Developer trust domain (deferred)
The optional `.conf` trust list naming system-trusted keys, for
provenance in the update channel.

## 6. Out of scope (recorded)

Secure boot / boot-path verification (kernel, loader, firmware),
forced verification anywhere, revocation infrastructure
(CRL/OCSP-like), notarization services, per-ELF verification at
exec, entitlements/capability enforcement (that is the future
sandbox, a separate plan), and **enforcing the blessing against a
direct exec** — the blessing is a launch-path safety prompt, not a
containment mechanism.
