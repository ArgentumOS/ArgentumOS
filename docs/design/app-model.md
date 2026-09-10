# Application model — app bundles

Status: **CHOSEN — fully decided** (Q-A..Q-E, §7).

---

## 1. Concept

GUI apps in `/Applications` (and `Users/$USER/Applications`, per the
FSH) are **bundles**: a directory that presents as a single app to the
user and to the shell, launched as one unit. Command-line tools in
`/System/Tools` stay plain executables — bundles are for GUI
applications. The bundle is the unit of distribution, installation, and
launch.

## 2. Bundle layout (decided: flat)

Bundles are **flat directories named `<DisplayName>.app`** (the display
name, not the identifier, in the directory name) — the macOS `.app`
bundle idea (a directory that presents as one app), but flat rather
than nested — identified by the
`.app` extension and the presence of the manifest:

```
HelloWorld.app/
    manifest            — app metadata, written in the .conf format (libconfig)
    bin/
        HelloWorld      — the executable
    Resources/
        icons/
        images/
        sounds/
        strings.conf    — UI strings (Q-L7 per-app resource)
    Libraries/          — optional private shared objects
    PlugIns/            — optional extensions (later milestone)
```

Resource strategy (Q-E): a bundle may carry private `Resources/` and
`Libraries/`, and an install step may also place shared resources
(fonts, images, libraries) into `/Shared` for other apps.

## 3. The manifest (decided: the config .conf format)

No proprietary metadata format — **the manifest is a `.conf` file in
the exact grammar of the config utility** (`docs/design/config-design.md` §10),
read with `libconfig`. Full metadata set in v1 (Q-D):

```
name = "Hello World"
identifier = com.example.HelloWorld
version = 1.0
executable = bin/HelloWorld
icon = Resources/icons/HelloWorld.png
menu-name = "HelloWorld"
document-types = txt, html
url-schemes = hello
```

Fields:

- `name` — display name (used in the global menubar's app menu and the
  launcher).
- `identifier` — reverse-DNS identity, **the same namespace as config
  domains**: the app's settings live in `com.example.HelloWorld.conf`
  at the three scopes. One identity namespace for the whole OS.
- `version` — for installation/upgrade checks.
- `executable` — path to the binary, relative to the bundle.
- `icon` — bundle-relative icon.
- `signature` — **optional** detached-signature file next to the
  manifest. Signing is **supported but never required**
  (docs/design/bundle-signing-plan.md): its absence is normal, and
  verification status is surfaced by `install` and by consumers that
  care (keychain ACLs) — never enforced at launch.
- `requires` — the package's **metadata declaration** (e.g.
  `requires = xattr acl`): what a distributable package needs the
  target filesystem to hold. Absent = modes only. Drives the fidelity
  refusal in docs/design/package-format-plan.md §6.
- `menu-name` — the bold name shown in the global menubar.
- `document-types`, `url-schemes` — declarative registration for
  open-with and deep links, in v1 (Q-D).
- Later: entitlements/declared capabilities for a future sandbox.

## 4. Launch flow

1. The shell (or a launcher) resolves a bundle by name from
   `/Applications` or `Users/$USER/Applications`.
2. It reads and validates the manifest with `libconfig`.
3. It checks the bundle's **run authorization**: a bundle signed with
   a trusted key proceeds; otherwise the launcher consults the user's
   blessing record and, on first run, prompts once (Run / Run Once /
   Cancel) — the signature policy's blessing gate
   (docs/design/bundle-signing-plan.md §3). On a successful blessing
   the record is written before the app starts.
4. It execs `<bundle>/bin/<executable>` with the standard environment
   (PATH, HOME, TMPDIR per the runtime contract) and the app's config
   domain available.
5. The app connects to the X server, creates windows, and publishes
   its global menubar (`menubar_set`).

Steps 1–3 and 4–5 are separated by the privileged door: they are
**the `launch` helper's** job (setuid System, docs/design/
bundle-launch-plan.md), which performs the blessing check
unprivileged, then makes its one privileged act — establishing the
process's **bundle identity** and performing the exec, with the
payload running as the user (never with euid 0).

A bundle's executable is an ordinary binary *inside* the bundle, but
**it is not directly runnable**: `install` marks payload executables
(`AGFS_INODE_BUNDLE_ENTRY`) and `execve` refuses a marked file
unless the caller carries that bundle's launcher-created identity —
so `./Foo.app/bin/Foo` fails from a shell and the bundle is launched,
not its guts. Copying the payload out of the bundle and running the
copy still works, identity-less (the honest ceiling of the scheme).

## 5. Integration with the rest of the design

- **FSH**: `/Applications` (system) + `Users/$USER/Applications`
  (per-user, §9.3 of the FSH). Shared third-party resources that more
  than one app wants go to `/Shared` (Libraries, Fonts, Images, ...)
  via an install step; the bundle may also carry private resources.
- **config**: the manifest reuses the `.conf` grammar and libconfig —
  zero new parsing machinery; app identity, config domain, and bundle
  identifier are one name.
- **i18n**: single locale (UTF-8-only, `docs/design/utf8-only.md`); no
  per-language resource dirs — strings are `Resources/strings.conf`
  (config-backed, Q-L7).
- **GUI**: the bundle's `menu-name` feeds the global menubar app menu;
  document types feed future open-with; the launch flow is the
  compositor's entry point for apps.

## 6. Non-bundle software

Tools (`/System/Tools`), scripts, and daemons are plain executables,
not bundles. A tool that grows a GUI can be re-packaged as a bundle
without changing its binary — the bundle is packaging, not a build
requirement.

## 7. Decisions

- **Q-A — Bundle shape: flat.** `manifest`, `bin/`, `Resources/`,
  `Libraries/` all at the bundle root; no nested container.
- **Q-B — Manifest format: the config `.conf` format via libconfig.**
  One grammar for config and metadata; zero new parsing machinery.
- **Q-C — Bundle naming: `<DisplayName>.app`.** The directory is the
  app's display name plus the `.app` suffix (e.g. `HelloWorld.app/`),
  NOT the reverse-DNS identifier; bundles are identified by the
  `.app` extension and the presence of the manifest file.
- **Q-D — Metadata scope: full in v1.** name, identifier, version,
  executable, icon, menu-name, document-types, url-schemes all in v1;
  entitlements/plugins later.
- **Q-E — Resource strategy: private + shared.** Bundles may carry
  private `Resources/`/`Libraries/` and also install shared resources
  into `/Shared` via an install step.

The application model is fully decided.
