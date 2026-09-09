# Service management

Status: **PLAN (2026-09) — shaped in conversation; no code.** Builds on
the decided config layer (`system-extensibility.md` §5.2), the
privilege model's principals (`system-admin-principal.md` §2), and
sessionmgr's boundary (`sessionmgr-design.md`). Explicitly an
**anti-systemd** design; the rules are recorded, not implied.

## 1. The model

- **init is the supervisor.** No service-manager daemon, no second
  root process owning services. PID 1 reads the services domain at
  boot, launches the enabled set in order, reaps, and applies the
  bounded-restart policy. Extending init is the design; a new
  privileged actor is not.
- **Services are config, not scripts.** A `system.services` domain
  (system scope only) is the whole truth: enabled set, order,
  identity, kind. Named sets (`set.base`, `set.all`) give the
  Extension-Manager wholesale-switch UX; `kernel.conf services =
  base` is the clean-boot switch (shift-at-boot analog).
- **Declarations carry the principal.** Every service names its
  identity (`principal = System | Service | Display | <dedicated>`)
  and optional privilege groups; init performs the **drop contract**
  (setgroups → setgid → setuid) before each exec — the
  sessionmgr/login discipline reused for services. A `Network`-group
  daemon is expressed in the declaration, never in a launcher script.

### Domain shape (flat, house style)

```
service.xfb.command = "/System/Shared/X11/bin/Xfb"
service.xfb.principal = Display
service.xfb.kind = daemon
service.ntpd.command = "/System/Tools/ntpd"
service.ntpd.principal = Service
service.ntpd.groups = Network
service.ntpd.kind = daemon
set.base = ["xfb"]
set.all = ["xfb", "ntpd", ...]
```

(The `.command`/grammar details are open; shape only.)

## 2. Anti-systemd rules (recorded)

- No service-manager daemon, no socket activation, no D-Bus, no
  cgroups, no logind. PID 1 owns services; inter-service talk is
  AF_UNIX; per-service state is `/System/Variable Data/<svc>` under
  its principal.
- Bounded restarts only — a crash loop is a bug to surface, not a
  storm to feed.

## 3. Kinds & policy

- **`daemon`**: long-running, supervised. **Bounded restart
  (decided)**: exits unexpectedly → restart, with a crash-loop cap
  (N restarts in a window, then stays down + logged).
- **`oneshot`**: runs to exit; init reports success/failure; no
  restart (decided: fail and log).

## 4. The `service` command (decided)

A first-party tool in the fleet's verb style:
`service <name> status|start|stop|restart` (plus `service list` for
the enabled set + state). Mutating verbs are **Admin-gated** (system
services are system state — the `disk`/`config` helper discipline);
`status`/`list` are open to anyone.

- **Channel**: `service` talks to init over an AF_UNIX socket; init
  performs the actual start/stop (it owns the processes and the drop
  contract). The command validates and forwards — no service control
  bypasses init.
- Requests are identity-checked (real caller + groups) — the same
  real-caller authorization as the helper fleet; init never acts on
  an unauthenticated request.

## 5. Boundary with sessionmgr

- **System services** (pre-session daemons): Xfb as `Display` is the
  first; declared in `system.services`, owned by init.
- **Session-bound services** (greeters, the Argentum Workspace, the
  per-person §J/§K housekeeping daemon): belong to **sessionmgr's
  world** — launched per-session under the person/Display, never in
  the system-services domain. `service` does not manage session
  things; the boundary is the same one sessionmgr already owns.

## 6. Milestones

### SM0 — Domain + init launch
init reads `system.services`, launches the enabled `daemon`/`oneshot`
set in order with the drop contract.
**Acceptance**: Xfb declared as a Display daemon boots exactly as
today (behavioral parity); a declared one-shot runs to exit; a bogus
declaration is logged and skipped, not fatal.

### SM1 — Bounded restart
Crash-loop-capped restart for daemons.
**Acceptance**: killing a declared daemon restarts it; repeated
immediate crashes hit the cap and stay down with a log; the boot
continues regardless.

### SM2 — `service` command + channel
AF_UNIX channel to init; `service <name> status|start|stop|restart`
and `list`; Admin gate on mutating verbs.
**Acceptance**: status reflects reality (running/stopped/crashed);
start/stop/restart of a declared daemon works without reboot; a
non-Admin caller is refused mutation; kill-cycle mid-restart leaves
init's state consistent.

### SM3 — Sets + clean-boot
Named sets honored; `kernel.conf services = base` clean-boot switch.
**Acceptance**: switching sets changes the launched set; the
clean-boot key boots only the base set; Settings app browses/edits
the domain.

## 7. Open

- Domain grammar details (command as path + argv array; per-service
  environment; log destination defaulting to Variable Data).
- Crash-loop cap parameters (N and window) and whether restart
  applies to Display services that take the desktop down with them
  (Xfb restart semantics need the sessionmgr boundary to stay
  coherent).
- `service` binary home (a system tool under the helper discipline)
  and its real-caller auth details.
- v1 roster beyond Xfb — services are added as daemons actually
  exist (type-maintainer is session-bound, not here).
