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

- **Xfb** — always-on display service (existing; role promoted to
  system service).
- **EMWM (fork)** — window manager + global menubar, always running as
  a system client; the menubar is alive even at the login screen,
  showing system-state menus (Shutdown/Restart).
- **sessionmgr** (name decided) — the privileged, always-running
  daemon that owns the boundary between system state (no user) and a
  user session. It owns **no UI itself**.
- **The login app** — a separate client (a Momo app in the desktop
  era) that sessionmgr spawns when no session is active; on
  authentication success sessionmgr replaces it with the user session.

## 3. sessionmgr's role (name/role record)

1. Start after Xfb; ensure the system desktop clients (EMWM +
   menubar) are up.
2. Spawn the login app when no session is active.
3. On authentication success: kill the login app; create the user
   session (setuid/setgid, HOME, DISPLAY, process group, session env).
4. On logout: reap the user's process group; return to the login state.
5. Own fast user switching (park/activate sessions) and the power
   actions (shutdown/restart) the menubar offers pre-login.

Not a display manager (never touches the server; Xfb already runs) and
not the login UI itself (the login app is). Privileged system daemon —
not branded after Momo, because it must work before/independent of
Momo apps.

## 4. Security / trust model

- **Trust by construction**: pre-login, the only X clients are
  system-owned (Xfb, EMWM, sessionmgr, login app). No user clients
  exist, so there is nothing to isolate — the session *policy* enforces
  the login-screen boundary, not the X server.
- X access control (auth cookie) gates who may connect at all; user
  clients receive session credentials only after login.
- Keyboard focus at the login screen belongs to the login window by
  construction (no other client exists to grab it).

## 5. Lifecycle

System boot → Xfb up → sessionmgr up → EMWM/menubar up → login app →
auth → user session (process group) → … → logout → reap user group →
login app again. Fast user switching = sessionmgr parks (SIGSTOP) one
session's process group and activates another's — no server restart.

## 6. Open items

- The login app's name and design (a Momo app; later).
- Login→session handoff mechanics (how sessionmgr passes the session
  cookie/env; auth backend — .conf account domains exist).
- Power-action ownership (sessionmgr → init/kernel on shutdown).
- Where the "always-on" Xfb service lives in init/config (system
  service entry, kernel.conf `services=` era).
- Sequencing: design-front only; implementation after Momo and the
  desktop apps exist (login UI is a Momo app).
