# x11-xvfb-fb-plan — a real X11 server on FNX via the Xvfb core

Status: PROPOSED (2026-09). Decision requested before code starts.

## 1. Goal and framing

Run a **real X11 server natively on FNX**, rendering to the real FNX
framebuffer (`/dev/fb0`) and driven by real FNX input (`/dev/psaux` +
the keyboard device), so that genuine X client applications (Xt/Motif,
FLTK-over-X, any X toolkit) run unmodified on FNX.

The chosen route is **"extract Xvfb and attach the FNX framebuffer"**:
use the *modern* X server's Xvfb DDX (`hw/vfb`, live on
`server-21.1-branch`) as the base, replace its in-memory screen with
`mmap("/dev/fb0")`, and add real input devices. This is effectively
building a **modern Xfbdev** — the same creature as the Xfbdev removed
from xserver in 2017 (commit `feed7e3f`) but on a maintained core,
without Xfbdev's deleted-code resurrection cost and without modular
Xorg's dlopen requirement (FNX has no dynamic loader).

Why this beats the alternatives (verified against xserver source,
2026-09):

| Route | Why not / why this |
|---|---|
| modern modular Xorg 21.x | dlopen'd drivers/fbdevhw ⇒ needs an ELF dynamic loader in the kernel first (6–12 wk+) |
| 1.19-era Xfbdev | two *deleted* kdrive target dirs resurrected against a moved tree; 2016-era code, old libXfont 1.x |
| kdrive (Xephyr host) as base | viable but drags Xephyr glue (xcb/epoxy) to strip; bigger input layer than needed |
| **Xvfb extraction** | 3-file DDX, monolithic (no dlopen), screen = pointer+geometry into `fbScreenInit`, memory provider already abstracted (`-fbdir` mmap), depth-24/bpp-32 XRGB8888 == FNX fb byte-for-byte |

## 2. What the surgery is (verified in source)

`hw/vfb` = `InitInput.c` + `InitOutput.c` + a meson build. The Xvfb
binary links the full dix server + `libxserver_fb` + mi + XKB stubs;
**no dlopen anywhere** (module loading is xfree86-only).

**Screen.** `vfbScreenInit` allocates pixel memory and hands
`fbScreenInit(pScreen, pbits, w, h, dpi, dpi, paddedWidth, bitsPerPixel)`
a raw pointer + geometry — zero hardware assumptions. Memory providers
are already pluggable: `NORMAL_MEMORY_FB | SHARED_MEMORY_FB |
MMAPPED_FILE_FB` (`-fbdir`, with msync block/wakeup handlers). The swap:

1. replace the allocation with `mmap("/dev/fb0", MAP_SHARED)` (FNX fb
   supports userland mmap — the compositor already does this);
2. bypass the XWD-capture preamble Xvfb writes at the front of its
   buffer (real fb starts at offset 0) and drop `msync`;
3. pass the **real stride** as `paddedWidth` (verify: pitch == w×4 or
   padded);
4. depth 24 / bpp 32 TrueColor masks 0xff0000/0x00ff00/0x0000ff =
   XRGB8888 little-endian — matches FNX fb exactly.

Direct rendering: a single full-screen X server has no occlusion, so X
draws straight into the mapped fb — **no shadow/blit layer needed**.

**Input.** Xvfb creates core pointer/keyboard devices but feeds them
nothing (XTEST-only). The plumbing is already linked
(`QueuePointerEvents`/`QueueKeyboardEvents` → `mieq`). Port the kdrive
pattern from `hw/kdrive/src/kinput.c` (~100 lines): block/wakeup-handler
poll of `/dev/psaux` (PS/2 packets — same stream the compositor parses)
and the FNX keyboard device, enqueue into the X event queue.

Two caveats:
- `miDCInitialize` installs a **no-op invisible cursor**; swap in a real
  software cursor when a mouse appears (~50 lines).
- a physical keyboard needs an `XkbRMLVOSet`; use the server **default
  keymap** (the NULL-rmlvo path Xvfb already boots with) or ship
  precompiled `.xkm` — avoids xkbcomp + xkeyboard-config entirely.

**Server core deps.** pixman, libXfont2 (core PCF fonts), libxkbfile,
xtrans, xorgproto. All pure portable C (static-musl OK, Alpine-proven).
No xshmfence/libdrm/udev/libxkbcommon. **The server's `os/ospoll.c` has
an epoll backend — FNX's epoll(2) satisfies it; poll(2) is NOT
required.** SIGIO is not used (removed upstream 2016).

Branch pin: use `server-21.1-branch` (upstream `master` is a defunct
stub; `main` is the live dev line). Build just the Xvfb target:
`meson -Dxorg=false -Dxwayland=false -Dxephyr=false -Dxnest=false
-Dxvfb=true` (plus the auto-detected server deps).

## 3. FNX host-surface inventory

Already sufficient (verified in this repo):

- AF_UNIX path sockets (`/tmp/...`) + TCP/IP — libgui, DHCP, ping work;
  X's `/tmp/.X11-unix/X0` is a plain path socket.
- select + **epoll** (ospoll epoll backend); fork/exec/clone; signals;
  mmap PROT_READ/WRITE/EXEC; SysV shm (XShm); `/dev/fb0` userland mmap;
  `/dev/psaux` PS/2 stream; custom keyboard device (4-byte records,
  same source the compositor's key events come from); static musl.
- rootfs/dev nodes, devfs registry pattern for any new nodes.

Gaps / to add (all small):

- none hard for the core port; the real work is POSIX-fidelity
  debugging in the os layer (socket semantics, timers, /dev/console).
- possible `poll(2)` as a convenience later (currently commented out in
  `include/fnx/unistd.h`); not required if ospoll picks epoll.
- root image is **8 MB and ~half full** — a static Xvfb + libX11 +
  fonts data is several MB; plan to grow the root image or stage X on
  the persistent ext2 ATA disk.

## 4. Milestones

Shared assumption: one experienced dev; QEMU headless iteration loop
(serial console + host X clients over TCP + HMP screendump — reuse the
`tools/lv_gui_test.py` harness patterns). Exit criterion per milestone
is explicit so each step is independently shippable/verifiable.

| # | Milestone | Work | Exit criterion | Effort |
|---|---|---|---|---|
| M0 | **Toolchain + vendored deps** | Vendor `server-21.1-branch` + pixman, libXfont2, libxkbfile, xtrans, xorgproto under `third_party/`; meson/ninja toolchain in the dev env; rootfs sizing decision | `Xvfb` cross-builds for static musl (host smoke: boots on Linux, `xdpyinfo` over its socket answers) | 3–5 d |
| M1 | **os/dix bring-up on FNX** | Port the monolithic server to FNX's POSIX surface (sockets, timers, event loop, console) — dummy Xvfb screen at first | Xvfb runs on FNX headless (dummy fb), serves :0; **host** `DISPLAY=<fnx-ip>:0 xdpyinfo` answers over TCP | 1–2 wk |
| M2 | **Real framebuffer output** | `/dev/fb0` mmap provider, stride, strip XWD preamble, depth-24/bpp-32 visual, real cursor | An X client (host-side, over TCP) drawing a pattern appears on the QEMU fb (screendump check); pixels/colors exact | 4–6 d |
| M3 | **Real input** | `/dev/psaux` + kbd devices (kdinput pattern), default keymap, cursor follows pointer | Inject PS/2 packets (ttyS1-style seam or psaux source override) ⇒ cursor moves on the fb; key events reach a client | 3–5 d |
| M4 | **Client stack on FNX** | Vendor libX11 (client) + a small classic app (xeyes/`xclock`-class) + PCF fonts | The app runs *on FNX* against the local X server; interactive via keyboard/mouse | 4–6 d |
| M5 | **Boot integration** | Decide display ownership (see §6); start X from init/shell; sizing + fonts installed | `make run-uefi` boots to an interactive X desktop (or a documented opt-in) | 2–3 d |

Total: **~3–5 weeks**; the bulk is M1 (shared core-port cost — every X
route pays it) and M4's libX11/app data plumbing.

## 5. Risks and de-risks (verify in M0–M2, not later)

1. **fb0 stride / endianness** — if the fb pitches or channels differ
   from w×4 XRGB8888, pixels smear. De-risk in M0 with a host-side pixel
   probe against the existing compositor path (known-good 0x00RRGGBB).
2. **XKB without xkeyboard-config** — the server default keymap path
   must actually produce a usable keyboard on FNX. De-risk in M3; fall
   back to precompiled `.xkm` (small, no xkbcomp at runtime).
3. **epoll backend selection** in `os/ospoll.c` on FNX/musl — confirm it
   compiles and selects epoll (else add `poll(2)`, ~1 d kernel work).
4. **Cursor** — no-op cursor until swapped; cosmetic until M3.
5. **Root image size** — static server + fonts; grow root.img or stage
   on the ATA disk (decide in M0).
6. **os-layer POSIX fidelity** — sockets/timers/console semantics; the
   iterative host-vs-FNX test loop in M1 is the mitigator.

## 6. Open decisions for review

- **Display ownership**: X server owns `/dev/fb0` when running, so the
  compositor + LVGL desktop and the X desktop are mutually exclusive
  (same fb). Default = X replaces the LVGL auto-start in init when
  enabled; keep the compositor path intact and opt-in.
- **Test topology**: host X clients over TCP during bring-up (fast,
  reuses the NIC) vs FNX-local clients (M4) — the plan assumes both.
- **Client showcase for M4**: xeyes-class minimal app first; xterm only
  if its deps (libXft/fontconfig?) can be avoided — xterm needs
  libX11+libXt+libXaw or Xaw-less build; confirm before promising.
- **Repo layout**: `third_party/xserver` (+ deps) as pinned submodules,
  mirroring the LVGL/FLTK precedent.

## 7. Non-goals

- No dynamic loader work (modular Xorg out of scope).
- No KMS/DRM, udev/dbus/logind, Wayland, GLX/glamor.
- Not replacing the compositor project — X is an additional, alternative
  desktop path (and if it lands, it answers the toolkit question for
  good: every X toolkit just works).

## Update (M0.5): standalone plain-make build — meson is no longer required

Building Xvfb no longer needs meson/ninja/autotools at all. The 265
server sources + generated config headers are extracted into
`third_party/x11/xvfb-src/` (committed, a8fd657) with one plain Makefile;
`make xvfb64` from the repo root produces `.build/x11/xvfb/Xvfb` (static
musl ELF64, host-smoke verified: X client reads screen=640x480 depth=24).

- Regenerate the tree with `tools/x11-extract-xvfb.py` (reads the meson
  build.ninja at `.build/x11/xserver` only as a source-of-truth graph).
- Per-module archives + `--start-group` are load-bearing (xi/xi_stubs,
  xkb/xkb_stubs member semantics); do not flatten to one object list.
- Third-party deps still come from `.build/x11-prefix` (pixman, xkbfile,
  xfont2, libsha1, Xau, Xdmcp) built by `tools/x11-deps-build.sh`.
- The upstream Xorg tree is **no longer vendored** (removed to save
  ~81 MB; see the commit log). For an upgrade, fetch xorg-server from
  freedesktop gitlab, apply `third_party/x11/xserver-fnx.patch` (the
  archived FNX-local meson changes), then re-extract as above.

## Update (decided): Xfb configuration via libconfig — `com.fnx.xfb`

Xfb takes **all** of its configuration from FNX libconfig; every
command-line option has a config key, `com.fnx.xfb.<key>`, whose value is
the option's **default**. The real command line overrides the config (same
precedence rule as `kernel.conf`, docs/config-design.md §12). Domain is
`com.fnx.xfb` per the repo convention (`com.fnx.*`, §2 of
config-design.md) — the `org.example.fnx.xfb.$OPT` naming sketch maps onto
it unchanged.

### Mechanism: config-derived argv, zero parser changes

All server options are parsed in one place: `ProcessCommandLine()`
(`os/utils.c:671`) — DDX hook first (`hw/xfb/InitOutput.c::ddxProcessArgument`),
then the ~53-option core chain, then the positional `:N` display. Xfb (the
FNX fork) therefore needs **no parser surgery**:

1. Link FNX libconfig into the server (compile `userland/libconfig.c` +
   `include/libconfig.h` into `userland/xfb`, exactly like the `config` CLI
   build pattern).
2. Before `ProcessCommandLine` runs, read the `com.fnx.xfb` domain and
   build a **config-derived argv prefix**: each set key becomes its
   canonical tokens via the arity table below.
3. Run the normal parse over *config-argv + real argv*.

Real-cmdline-wins is not left to parser last-wins semantics: for every
configured option, if the same option occurs in the real argv, the
config-derived occurrence is **suppressed** (per-key override). This
matters for accumulating options such as `-screen`/`+extension`, where two
occurrences would add two screens/extensions instead of overriding.

Scope resolution is the standard libconfig chain (user → shared → system),
so a per-user `com.fnx.xfb` overrides the system default — for free.

### Key naming and types

- Key = the option token, lowercased, leading sign stripped
  (`-screen`→`screen`, `-ac`→`ac`, `-fbdir`→`fbdir`).
- `+foo`/`-foo` polarity pairs collapse to **one boolean key** named after
  the enable form (`render`, `xinerama`, `bs`, `iglx`, `dpms`,
  `byteswappedclients`, `autorepeat`, `blanking`, `pn`, `reset`).
- Flag options are booleans (`ac = true` emits `-ac`); value options are
  strings or ints; the synthesizer emits exactly the token(s) the parser
  matches (including the legacy bare-token enable forms below).
- **Excluded from the map** (they are actions or boot context, not
  defaults): `-help`/`-version` (print+exit), the positional `ttyN`
  (Xorg VT switching — meaningless under Xfb), and `-I` (ignored by the
  parser). `-displayfd` is mapped but discouraged (it is a launcher
  handshake, not a preference).

### Arity table (what the synthesizer emits per key)

**A. Display, screen and DDX options (the surface FNX actually uses)**

| Option(s) | Key | Type | Synthesized argv | Notes |
|---|---|---|---|---|
| `:N` (positional) | `display` | string | `:<value>` | `xfbdesk-init` starts `:0`; `display = "0"` is the shipped default |
| `-screen N WxHxD` | `screen` | string | `-screen 0 <value>` | screen number fixed to 0 under FNX; when the key is **absent**, Xfb keeps today's behavior (geometry read from `/dev/fb0`) — see open items |
| `-pixdepths list` | `pixdepths` | string | `-pixdepths <value>` | |
| `+render` / `-render` | `render` | bool | true→`+render`, false→`-render` | RENDER ext on/off |
| `-blackpixel n` | `blackpixel` | int | `-blackpixel <n>` | |
| `-whitepixel n` | `whitepixel` | int | `-whitepixel <n>` | |
| `-linebias n` | `linebias` | int | `-linebias <n>` | |
| `-fbdir dir` | `fbdir` | string | `-fbdir <dir>` | Xvfb memory-file dir |
| `-shmem` | `shmem` | bool | true→`-shmem` | shared-mem framebuffer |
| `-ac` | `ac` | bool | true→`-ac` | FNX boots with `-ac` (xfbdesk-init); shipped default `ac = true` |

**B. Core server options with real effect under Xfb**

| Option(s) | Key | Type | Notes |
|---|---|---|---|
| `-auth file` | `auth` | string | X authority file |
| `-nolisten trans` / `-listen trans` | `nolisten` / `listen` | string | transports (`tcp`/`unix`); `nolisten` shipped default |
| `-noreset` / `-reset` | `reset` | bool | true→`-noreset` (keep clients across last-disconnect) |
| `-fp path` | `fp` | string | font path |
| `-dpi n` | `dpi` | int | |
| `-cc class` | `cc` | int | default visual class |
| `-deferglyphs mode` | `deferglyphs` | string | |
| `-background none\|color` | `background` | string | `none` = keep Xvfb's default black root |
| `-maxclients n` | `maxclients` | int | |
| `-maxbigreqsize n` | `maxbigreqsize` | int | |
| `-seat id` | `seat` | string | |
| `-fakescreenfps n` | `fakescreenfps` | int | |
| `-audit n` | `audit` | int | |
| `-a n` / `-t n` / `-f n` | `a` / `t` / `f` | int | pointer accel num/threshold, bell volume |
| `-p n` / `-s n` | `p` / `s` | int | screensaver interval / timeout (minutes; parser multiplies) |
| `-displayfd n` | `displayfd` | int | launcher handshake; mapped but not a preference |

**C. Parseable but vestigial under Xfb** — accepted by the parser, no
effect on an fb server; still given keys so *every* option obeys the rule
(marked no-op so nobody relies on them):

`-br`, `bs` (+/-), `byteswappedclients` (+/-), `-core`, `-nocursor`,
`dpms` (+/-), `iglx` (+/-), `-nolock`, `pn` (-/nopn), `-pogo`,
`autorepeat` (`r` bare/-r — note the parser's enable form is the **bare
token** `r`), `-retro`, `-terminate` (optional numeric delay), `-tst`,
`blanking` (`v` bare/-v), `-wr`, `-dumbSched`, `-sigstop`,
`-schedInterval`, `-schedMax`, `xinerama` (+/-), `-disablexineramaextension`.

### Build and integration

- `userland/xfb` is the FNX-native fork (diverges from pristine
  `third_party/x11/xvfb-src`, see its README for the delta); add
  `userland/libconfig.c` to its Makefile and `-I<repo>/include` for
  `libconfig.h` — the same two-file link the `config` CLI uses.
- The config read runs once at startup, before `ProcessCommandLine`;
  failures (missing domain, parse error) degrade to "no config-derived
  argv" and a serial/log notice — a broken `com.fnx.xfb.conf` must never
  stop X from starting.
- Shipped defaults live in the system domain as
  `userland/com.fnx.xfb.conf` (installed to
  `/System/Configuration/com.fnx.xfb.conf` in the root image):
  `ac = true`, `nolisten = "tcp"`, `display = "0"`, matching today's
  `xfbdesk-init` launch; `screen` is deliberately unset so geometry keeps
  coming from `/dev/fb0` (see open items).

### Open items

- **`screen` vs the fb0 auto-geometry**: when `screen` is absent Xfb
  reads `/dev/fb0` geometry; when set, `-screen 0 WxHxD` is emitted and
  must win over the fb0 auto-detect (the FNX fb0 integration currently
  sizes its screen from the device — the interaction needs one decision
  in implementation: config `screen` overrides device geometry entirely).
- Key spelling for legacy pairs with bare enable tokens (`r`, `v`,
  `dpms`, `c`) is kept literal-but-documented rather than renamed, to keep
  `$OPT` derivable; revisit only if a real user-facing option needs it.
