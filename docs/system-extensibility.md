# System extensibility — how FNX answers what classic Mac OS extensions did

Status: **CHOSEN — fully decided** (Q-X1..Q-X7, §8). Classic Mac OS
(System 7–Mac OS 9) loaded small
pieces of code — *system extensions* — into the running OS at boot to
add drivers, network stacks, media support, and UI features. This doc
examines, role by role, what that mechanism actually did for users and
developers, and where the equivalent capability lives in FNX today or
should live by design. Conclusion: **FNX needs no system extension
mechanism** — the roles are covered by processes, libraries,
the config system, and app bundles; the one genuinely missing classic
capability (adding kernel functionality after build) is served by the
**Tier-1 out-of-tree source-driver path (§6)**, with loadable modules
**deferred to sharpened triggers** (Q-X1/Q-X7) and `kernel.conf` as the
trim mechanism. The classic failure modes (arbitrary code patching a
shared system, load-order fragility) are inherited *away* by the POSIX
process model.

---

## 1. What classic system extensions were

An extension was a small file of code, dropped into
`System Folder:Extensions:`, loaded at startup, and run against the
running OS via the `'INIT'` mechanism — code resources enumerated at
boot (originally by the "INIT 31 trick" from the System file itself),
later PowerPC code fragments. Extensions operated by **patching the
system**: trap-table patching, memory patches, Gestalt selector
registration, `'DRVR'` drivers, Manager registrations. Whole OS
subsystems shipped this way (MacTCP/Open Transport, USB/FireWire
support, QuickTime, QuickDraw 3D, the Appearance Manager) so the OS
could be trimmed by disabling pieces. Control panels could carry INIT
code too — an extension with a configuration UI in one file.

The mechanism existed because there was **no other way to extend the
OS**: no processes with the kernel's authority, no separate driver
load, no per-user install. Every extension ran in the system's address
space with full privileges. That gave it its power and its famous
fragility: patch conflicts, order dependencies, no sandbox, no clean
unload, and boot-time bisection ("extensions off" = Shift at boot;
Extensions Manager sets from System 7.5).

## 2. Role inventory — what extensions actually provided

| Classic role | Example | FNX equivalent |
|---|---|---|
| Hardware drivers | USB, FireWire, SCSI, printers | **in-kernel drivers** (compile-time) |
| Network stacks | MacTCP, Open Transport | in-kernel net stack + userland tools (`ifconfig`, DHCP) |
| Media/format support | QuickTime, QuickDraw 3D | userland **libraries**; image formats arrive with the Viewer/decoders |
| OS look & feel | Appearance Manager | **the ported toolkit** (the own-toolkit/libwidgets direction was superseded and removed; see `docs/gui-e-toolkit.md`) — Platinum is built in, not bolted on |
| Background services | time sync, folder actions | **daemons** — plain executables (app-model §6) |
| Global floating UI | Control Strip, Application Switcher | owned by the **shell**, not a daemon |
| Scripting additions | OSAXes | none today (no AppleScript analog designed) |
| Per-device settings UI | Control Panels | the **Settings app** + config domains |
| Extension config sets | Extensions Manager | startup config in a system-scope config domain (proposed, §5) |

The striking thing about the table: every row already has a POSIX-native
home in FNX — a process, a library, a config domain, or an in-kernel
driver. There is no row that *requires* boot-time code injection.

## 3. Why the patch model is inherited away

Every classic failure mode is structurally impossible in FNX:

1. **Arbitrary code in the kernel's address space** — impossible: there
   is no mechanism to run third-party code privileged. Kernel is
   monolithic, drivers compiled in; userland is process-isolated with
   ACLs.
2. **Patch conflicts** (two extensions on the same trap) — impossible:
   no patching. Two services wanting the same resource resolve through
   normal process rules (sockets, files, config precedence).
3. **Load-order fragility** — replaced by *deterministic* startup: `init`
   launches services in declared order (fsh-proposal's Configuration
   dir, §147); no catalog-enumeration-order surprises.
4. **No clean unload / no sandbox** — processes start and stop cleanly;
   a daemon is killable, restartable, and memory-protected.
5. **Boot-time bisection** — retained as a *feature*, at the right
   level: disable services via config, not by physical removal (§5).
6. **System heap pressure from every extension** — no shared system
   heap; each process owns its memory.

The classic Mac needed extensions because its OS could not be extended
any other way. FNX's answer to "how do I extend the system?" is: **run
a process, link a library, or (for kernel work) rebuild.** Those are
the only three doors, and they are all deliberate.

## 4. What FNX does not have

- **No kernel module loader — today.** Adding a driver means a kernel
  rebuild — true of Fiwix, true of FNX, and right for a small
  educational kernel. A loader would drag in symbol exports, an
  in-kernel ABI to freeze, and a security surface — all for hardware a
  hobby OS ships compiled-in. **Deferred, not forever (Q-X1):** if
  real-hardware support ever demands post-build drivers, that is the
  trigger to evaluate a minimal loader on its merits. Meanwhile the
  classic Mac's ability to add hardware support post-build is replaced
  by: the driver roster ships in the kernel, and **`kernel.conf`
  trims** (below).
- **No boot-time code injection for "OS features".** QuickTime-class
  capability = a userland library; Open Transport-class = kernel net
  stack + userland tools; Appearance-class = the toolkit. None of these
  need to patch anything.
- **No control-panel/extension hybrids.** A file that both patches the
  OS at boot and presents settings UI is the exact shape of the classic
  conflict problem. FNX's shape: service (daemon or built-in) + config
  domain + Settings app. UI and code are separate by construction.

## 5. The two things worth keeping (proposed design)

### 5.1 Driver trimming via `kernel.conf` (the Extensions Manager, kernel-side)

The classic "trim the OS by disabling extensions" is available at the
driver level without code removal. `kernel.conf` (§12 of config-design,
same `.conf` grammar) gains per-driver gates read before the driver
probe phase:

```
driver.ahci.enabled = true
driver.usb.enabled  = true
driver.sound.enabled = false
```

A lean read-only gate check in the PCI/driver init path (a couple of
lines per driver table entry). Value: a way to isolate a driver
conflict or free RAM without a rebuild — the clean-boot "Shift key"
analog is `driver.*.enabled = false` in kernel.conf. Low priority:
drivers are small and in-tree, so this is a later nicety, not v1.

### 5.2 Service sets via a system-scope config domain (the Extensions Manager, userland-side)

The genuinely useful UX from Extensions Manager was **sets**: define a
known-good combination and switch wholesale. FNX gets this at the
service level, in the config grammar:

- Domain `com.fnx.system.startup` (system scope only), keys per service
  (`service.ntpd.enabled`, ...) plus named **sets**:
  `set.base = ["init", "compositor", "workspace"]`,
  `set.all = [...all services...]`.
- `init` reads the domain at boot and launches exactly the enabled set,
  in the declared order (fsh-proposal Configuration, §147).
- A clean-boot switch = `kernel.conf` key `services = base` (or a
  one-boot override), the Shift-at-boot equivalent — no physical file
  removal, revertible by editing config.
- The **Settings app** browses and edits the domain (it already browses
  system Configuration), giving the control-panel editing experience
  without any control-panel code.

Both keep the FNX one-grammar rule: the "extension manager" is just
config, because there are no extensions to manage — only drivers and
services, and both are configured, not patched.

## 6. Third-party hardware drivers (the Q-X1 future)

The reality check first: hardware vendors will effectively never ship
binary drivers for FNX (they barely do for most hobby OSes — even Linux
gets vendor drivers mostly as upstream contributions). The realistic
"third parties" are contributors and users with odd hardware, and all
of them produce **source**. So the third-party driver question is not
"do we load binaries?" but "can a driver written outside the FNX tree
be added to the OS without forking the kernel?" — and that has an
answer needing no module loader.

- **Tier 1 — out-of-tree source drivers (the designed path, Q-X6).**
  FNX drivers are plain C files whose probe matches PCI vendor/device
  IDs during the bus scan (e.g. `igb.c`'s vendor/device match,
  driven from `drivers/pci/pci.c`). A third party therefore needs only
  a **build hook** — `FNX_EXTRA_DRIVERS=...` or a `drivers-external/`
  directory — whose C files compile into the kernel image alongside
  the in-tree drivers. The contract is a **documented driver API**: the
  kernel interfaces a driver may call (PCI config/MMIO access,
  `request_irq`, DMA helpers, devfs node registration with
  `fs_operations`), kept source-compatible by one-author discipline
  (Linux's in-kernel API model minus the churn). Zero new machinery:
  no symbol exports, no ELF loader, no ABI freeze, no security
  surface. Good drivers get merged in-tree over time (the Linux
  model), so the roster stays "the OS's own" — code built with the
  kernel, never injected. Generic class drivers (AHCI, the USB HCD
  set, NIC families, audio) already cover the bulk of real hardware;
  the long tail (exotic NICs, sound cards, tuners) is exactly what
  Tier 1 serves.
- **Tier 2 — loadable modules (the Q-X1 eval).** Only two things
  genuinely justify modules: **binary distribution without source**
  (rare — and legally fraught if the kernel stays GPL-derived) or
  **hot reload for driver iteration**. The requirements are known:
  kernel symbol export, an ELF relocation loader, module
  lifecycle/refcounts, and either a frozen driver ABI or source-built
  modules (FreeBSD/NetBSD style — modules compiled against the kernel
  build, so still no ABI freeze). Tier 1 does not preclude adding it
  later; the driver API would be shared.
- **Tier 3 — userland experimental drivers (UIO-style, deferred).**
  Mapping a PCI BAR + routing an IRQ behind a devfs node (read/write/
  mmap) would let driver bring-up run in a normal process — crashes
  don't take the kernel down, and the process model applies even to
  drivers. It is a small kernel addition for a niche dev tool (real
  driver work needs timers/DMA that are awkward from userland), so it
  is deferred, not specced.

## 7. App-level extensibility (already designed, for contrast)

The Mac OS 9-era extension surface also included app-visible
extensibility; FNX's app model covers it without system code:
document-types and url-schemes for open-with and deep links,
`menu-name` + click-time menu validation for UI integration, AT-tree
introspection, and bundle `PlugIns/` (app-model, later milestone) for
per-app plugins. Contextual-menu *items* and scripting additions have
no FNX analog designed — if a scripting story ever exists it should be
a userland one (process + IPC), not an injected-code one.

## 8. Decisions

- **Q-X1 — Kernel modules: evaluated later, with sharpened triggers
  (Q-X7).** Compile-time drivers plus the Tier-1 source path (§6) are
  the designed ways to add hardware support; `kernel.conf` gates
  (§5.1) are the trim mechanism. A minimal loader is revisited only
  on the triggers of Q-X7.
- **Q-X2 — Service sets: adopted.** `com.fnx.system.startup` (system
  scope) with `service.*.enabled` keys and named sets (`set.base`,
  `set.all`, ...), read by `init` in declared order; a clean-boot
  switch is `kernel.conf` `services = base` — the Shift-at-boot
  analog, revertible by editing config.
- **Q-X3 — Driver gates in `kernel.conf`: later.** The key shape is
  specified (§5.1: `driver.<name>.enabled`, read before the probe
  phase); implement when a real driver conflict or RAM need shows up.
- **Q-X4 — Settings app: the only control-panel surface.** Settings
  browses all system-scope config domains; no per-service control
  panels.
- **Q-X5 — Scripting/automation (OSAX analog): deferred.** No
  AppleScript analog in the design; revisit only if a scripting story
  is ever wanted, as a userland process+IPC design.
- **Q-X6 — Third-party drivers: Tier 1 adopted as the designed path.**
  The out-of-tree source-driver build hook (`FNX_EXTRA_DRIVERS` or
  `drivers-external/`) plus a documented driver API (§6); merge-
  in-tree remains the long-term home; Tier 2 stays under Q-X1's
  triggers; Tier 3 (userland/UIO) deferred.
- **Q-X7 — Q-X1 triggers sharpened.** Revisit a minimal module loader
  only when (a) binary-only driver distribution is genuinely wanted,
  (b) hot reload for driver iteration is demanded, or (c) real
  hardware support appears that neither the compiled-in roster nor
  Tier-1 source integration can satisfy.
