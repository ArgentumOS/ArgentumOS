# Keychain — system-wide, per-user secrets

Status: **PLAN (2026-09) — decided in direction; nothing implemented.**
A system-wide keychain facility: a `keychain` service with a client
API, a `keychain` CLI, and a Keychain GUI app — per-user stores
unlocked by the account password, plus a **System keychain** for
machine secrets. It is a *service* first and an app second (the
pasteboard pattern, not a lone app).

## 1. Decisions (settled)

- **Scope**: per-user keychains **and** the System keychain, designed
  together.
- **Unlock**: the account password — auto-unlock at login; the
  master key is wrapped by `KDF(account password)` so a password
  change **re-wraps only** (no item re-encryption).
- **App access**: **per-item ACLs + a prompt** on first access by a
  new app (Allow / Always / Deny). ACL entries also record the
  identity's **verification strength** — `verified` (signed against
  a trusted key) or `self-asserted` (unsigned) — per the signing
  policy (docs/design/bundle-signing-plan.md).

## 2. Where the bytes live

FSH amendment (fsh-proposal, applied): `Keychains/` is a first-class
directory —
- `/Users/$USER/Keychains/login.keychain` (default per-user store),
- `/System/Keychains/System.keychain` (machine secrets, Admin-gated).

Not under `Configuration/` (encrypted blobs, not editable settings)
and not under `Variable Data/` (not disposable state).

## 3. Architecture

- **Service**: `keychain`, in the services domain — one instance per
  user session, plus one system instance. It is the only code that
  ever holds decrypted items.
- **Crypto (LibreSSL `libcrypto`, already planned)**: per-store
  random master key from `getrandom`; master key wrapped by
  PBKDF2-HMAC-SHA512 or scrypt with a per-store salt; items encrypted
  **per item** with AES-256-GCM or ChaCha20-Poly1305 (AEAD → tamper
  detection is structural). Per-item encryption is what makes ACL and
  prompt granularity possible without exposing the whole store.
- **Store format**: documented magic + version header; a store is a
  self-contained file, so export/import and backup are file copies of
  an encrypted blob (no plaintext ever written).
- **Client API**: a `Keychain` client in the toolkit/utility library —
  `add`/`find`/`delete`/`lock`/`unlock`/`changeACL` over typed items
  (name, service/domain, account, secret kind: password | token |
  private key | note; timestamps; ACL).
- **CLI**: `/System/Tools/keychain` with the same verbs (scriptable,
  Admin-aware for the System keychain).
- **GUI app**: the **Keychain** app (Argentum UIKit) — browse, edit,
  generate, reveal with a prompt; joins the release app roster when
  K3 lands.
- **System keychain unlock**: by an Admin credential at boot/first
  Admin login (there is no TPM or secure boot yet — the honest
  position; hardware-rooted sealing is a later trigger).

## 4. Threat model (honest)

**Protects against**: offline disk theft (encrypted store + KDF),
other users (per-user unlock + file permissions), accidental exposure
(the API is the only path; ACLs gate apps; secrets never hit a
plaintext file).

**Does not fully protect against**: a malicious process **already
running as the same user** after unlock — it can request items, and
the ACL prompt is a human decision that social engineering defeats.
There is no process isolation yet (sandboxing is unplanned) and no
signed-bundle identity needs care: **signing is supported but never
required** (docs/design/bundle-signing-plan.md), so an app ACL's
identity is `verified` only when the bundle carries a signature that
checks against a trusted key — otherwise it is `self-asserted`, which
an unsigned app can claim. The prompt still applies either way, and
items may opt into "verified identity required". Process isolation
(sandboxing, unplanned) remains the other gap; both are recorded here
so the ACL prompt is not mistaken for a sandbox.

**Side benefits / watch items**: with no swap today, secrets cannot
be paged to disk (**when the swap plan lands, verify an
`mlock`-equivalent exists** for the daemon's unlocked memory — audit
item); the unlock prompt travels through our own stack, but
same-user input capture is not defended against (same limit class).

## 5. Consumers

- **LibreSSL** client credentials (private keys retrieved by API, not
  read from plaintext files).
- **Installer / updater** repository credentials (the updater is
  planned; the keychain is where its secrets belong).
- **Terminal / SSH** when an SSH stack is adopted (recorded, not
  scheduled — SSH itself is unplanned).
- **Apps** storing service tokens (the general password-manager case
  the GUI app serves).
- Out: browser integration, sync/cloud, FIDO/passkeys, PKCS#11
  module — each trigger-gated later.

## 6. Milestones

### K0 — Store format + daemon + CLI (passphrase unlock)
Daemon (per-user instance), store format with a documented
header, `libcrypto` crypto, CLI verbs. Unlock by an explicit
passphrase for now (account integration is K1).
**Acceptance**: add/find/delete/lock/unlock round-trip; wrong
passphrase fails cleanly; a flipped ciphertext byte is rejected
(AEAD); no plaintext is ever written to disk.

### K1 — Per-user + System instances, account-password unlock
Wire unlock to the account/principal system: auto-unlock at login,
re-wrap on password change, auto-lock (explicit, idle, session end);
the System keychain with its Admin-gated unlock.
**Acceptance**: login unlocks the user store; session end re-locks;
changing the account password preserves every item; the System
keychain refuses a non-Admin.

### K2 — Item ACLs + first-access prompt
Per-item ACLs keyed on app identity (app-model), with the UIKit
Allow/Always/Deny prompt on first access by a new app; refusal is
silent for non-permitted apps.
**Acceptance**: two apps, one allowed and one denied, behave
differently; the prompt appears exactly once per new app/item pair;
"Always" persists in the item ACL **as a `self-asserted` entry** for
an unsigned app and as `verified` for a signed one; a
`verified identity required` item refuses the unsigned app silently.

### K3 — Consumers + the Keychain app
LibreSSL key retrieval, updater credentials, and the Keychain GUI
app; the app joins the release roster.
**Acceptance**: a TLS client key is used from the keychain without a
plaintext key file existing; the GUI can browse/edit/generate items.

### K4 — Hardening & portability (deferred)
Encrypted export/import, idle-lock policy in the config corpus, the
`mlock` audit closed, hardware sealing (TPM/secure boot) when that
infrastructure exists, and the **bundle-signing track**
(docs/design/bundle-signing-plan.md) to make ACL identities
verifiable where a bundle is signed.
