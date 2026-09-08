# sessionmgr — the session/login architecture

Status: **DESIGN RECORD (2026-09) — direction decided in conversation;
nothing implemented.** Implementation belongs to the desktop era (login
UI is a Momo app; Xfb already runs today).

## 1. The model

- **The X server is a system service, always running.** Xfb starts at
  boot (init-spawned, system-owned) before any login and is never
  killed by logout — the display never dies, no server restart between
  sessions. This is the NeXT/macOS WindowServer model.
- **The graphical login manager is just another X11 client** — it does
  not start the X server. Logout returns to the login window instantly
  (the server stayed up the whole time).
- Sessions are **client sets + process groups**, never server
  lifetimes.

## 2. Components

- **Xfb** — always-on display service, **run as the `Display` principal** (decided: never System).
- **Argentum Workspace** (was EMWM) — window manager + global menubar, always running as a system client under `Display`; the menubar is alive even at the login screen, showing system-state menus (Shutdown/Restart → the `power` helper).
- **sessionmgr** (name decided) — the privileged, always-running
  daemon that owns the boundary between system state (no user) and a
  user session. It owns **no UI itself**.
- **The greeters** — sessionmgr spawns one per seat when no session is active: a **console greeter** (text, on the serial console tty) and a **graphical greeter** (an Argentum UIKit app on the X display). Both run as `Display` (unprivileged); on authentication success sessionmgr replaces the greeter with the user session. The greeters never run as System and never perform the drop themselves.

## 3. sessionmgr's role (name/role record)

1. Start after Xfb; ensure the display clients (Workspace + menubar)
   are up — all under the `Display` principal.
2. Spawn a greeter (console text greeter on the serial tty, graphical
   greeter on X) when no session is active.
3. On authentication success: kill the greeter; **perform the drop
   contract** — setgroups (role + privilege groups from the account
   domain), setgid, setuid, environment (`HOME`, `DISPLAY`, session
   env, process group) — then exec the person's session. The drop
   sequence is the §6.3-critical code and lives only here.
4. On logout: reap the user's process group; return to the greeter
   state.
5. Own fast user switching (park/activate sessions) and the power
   actions (shutdown/restart via the `power` helper) the menubar
   offers pre-login.

Not a display manager (never touches the server; Xfb already runs),
not the login UI itself (the greeters are), and not a generic root
runner (no `su` — sessionmgr only ever starts *sessions*). Privileged
System daemon — justified as the login-boundary owner; not branded
after a desktop app, because it must work before/independent of them.

## 4. Security / trust model

- **Least privilege at the login screen**: Xfb, the Workspace/menubar,
  and both greeters run as the unprivileged `Display` principal
  (device access via `Video`/`Input` group ACLs on devfs nodes), never
  as System. `sessionmgr` is the only System process in the pre-login
  world; it spawns greeters as `Display` and alone performs
  verify-and-drop.
- **Trust by construction**: pre-login, the only X clients are
  system-owned (Xfb, Workspace, sessionmgr, graphical greeter) — no
  user clients exist, so there is nothing to isolate; the session
  *policy* enforces the login-screen boundary, not the X server.
- X access control (auth cookie) gates who may connect at all; user
  clients receive session credentials only after login.
- Keyboard focus at the login screen belongs to the greeter by
  construction (no other client exists to grab it).
- Authentication: sessionmgr verifies the password against the
  account domain (`system.passwd.conf` + crypt; no PAM), refuses
  non-person targets (`System`/`Service`/`Display` have no shell and
  cannot log in; `su` does not exist).

## 5. Lifecycle

System boot → Xfb up (as Display) → sessionmgr up → Workspace/menubar
up (as Display) → greeter (console and/or graphical) → auth →
sessionmgr drops to the person (drop contract, §3.3) → user session
(process group) → … → logout → reap user group → greeter again. Fast
user switching = sessionmgr parks (SIGSTOP) one session's process
group and activates another's — no server restart.

## 6. Open items

- Greeter names/designs: the console text greeter and the graphical
greeter (an Argentum UIKit app; later).
- Greeter↔sessionmgr handoff: the session credential/env channel and
the auth request protocol (who may ask sessionmgr to start a session;
rate-limiting password attempts).
- `Display`'s uid value and devfs membership staging (fb0 → Video,
input → Input); where the auth cookie for the graphical display is
stored (Variable Data/Display).
- Power-action ownership: menubar pre-login actions route through the
`power` helper (§4 fleet).
- Where the always-on Xfb service lives in init/config (system
service entry, kernel.conf `services=` era).
- Sequencing: design-front only; implementation after the Argentum
desktop apps exist (the graphical greeter is an Argentum UIKit app).
The console greeter can land earlier (text-only).
