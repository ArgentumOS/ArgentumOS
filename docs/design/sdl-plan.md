# SDL as the third-party graphics/audio door

Status: **PLAN (2026-09) — decided in direction; not scheduled.**
A port of SDL3 as FNX's third-party media/game library: the door
through which permissive games, emulators, and media apps arrive.

## 1. Admission — the license allows

- **SDL3 is zlib-licensed** (as is SDL2) — admissible under the
  permissive roof, no copyleft anywhere in the tree. Pin: **SDL 3.4.16**
  (current stable, 2026).
- **SDL3, not SDL2**: both are zlib, but SDL3 is the maintained
  current line *and* is **autotools-free by construction** — SDL3's
  build system is CMake only; upstream dropped the autotools build,
  so nothing in the vendored tree needs excising (the LibreSSL
  discipline is satisfied by the upstream's own choice). SDL2 apps
  arrive via SDL3's bundled SDL2-compatibility layer, not a second
  library.

## 2. Why it fits — SDL consumes exactly what FNX has

SDL's two hard dependencies are video and audio, and FNX already
ships both doors:

- **Video**: SDL3's X11 driver over Xfb (`:0`) — no GL/GLES/KMS in
  scope; SDL's software renderer is the drawing path.
- **Audio**: SDL3 still carries the **OSS driver** (`dsp` — "Open
  Sound System (/dev/dsp)"), which is precisely FNX's audio family
  (sb16, gus, es1370, ac97, intel-hda, virtio-snd, …). `SDL_AudioOpenDevice`
  speaks to `/dev/dsp` with no new kernel surface. **But that path is
  exclusive**: the shipping default should be a **mixer-native SDL
  audio backend** over the first-party audio service
  (docs/design/audio-mixer-plan.md §4), so games mix with other apps;
  the `dsp` backend stays for direct/exclusive mode and hardware
  bring-up.
- **Input**: via X11 events (keyboard/mouse already reach Xfb) — no
  evdev dependency needed.

## 3. Dependency audit (the one real unknown)

The X11 video driver's build-time needs against the existing x11
prefix (xorgproto/xcb/libX11/xtrans/libXau/Xdmcp/pixman/libxkbfile/
libXfont2/fontconfig/freetype/libpng/expat) must be audited at S0:
SDL3 probes Xrandr/Xrender/Xext at CMake time and disables what is
absent — the audit decides whether any **X leaves** (libXrandr,
libXrender, libXext) must join `tools/x11-shared-build.sh` (the same
leaf-adoption pattern as libpng/expat) or whether the driver's
defaults suffice. The audio side needs no audit: `dsp` requires only
a POSIX `open`.

## 4. Milestones

### S0 — Cross-seed build + audit (host)
CMake build of the 3.4.16 pin against the musl sysroot + x11
prefix, `SDL_X11=ON`, `SDL_OSS=ON`, software renderer on; record the
X-leaf audit verdict. **Acceptance**: `libSDL3.so` + `SDL3` headers
built shared; a host-side SDL test links and runs against the
sysroot's X11.

### S1 — Stage into FSH
`libSDL3.so` → `/System/Libraries` (house rule: the loader search
path spans `System/Libraries`); headers consumed at build time from
the pinned checkout, no global header tree. **Acceptance**:
`libSDL3.so` loads on-FNX (versioned SONAME resolves).

### S2 — In-guest smoke (the real proof)
A small first-party SDL demo on the FNX root image: opens an X11
window on the Xfb desktop via `SDL_CreateWindow` **and** plays a
tone through `SDL_AudioOpenDevice` → the OSS `/dev/dsp` path (tone
verification is the established audio acceptance). **Acceptance**:
window visible on the desktop, tone audible through the same
OSS family the wired drivers already verify.

### S3 — UIKit-embeddable SDL view (the sanctioned bridge)
SDL apps must not be separate windows floating beside the desktop;
they embed inside Argentum windows like any UIKit content.
Architecture — X11-idiomatic, **no SDL fork**:

- A UIKit native-container view (libargentum) owns an X11
  **subwindow** of its app window and hands its XID to the SDL app
  through SDL3's native-window entry
  (`SDL_CreateWindowWithProperties` + the x11-window property); SDL
  renders with its software renderer into that subwindow.
- Pointer events reach the SDL child automatically (X11 subwindow
  clipping); keyboard focus is managed with the **XEMBED** container
  role, implemented first-party in the UIKit container — SDL3's X11
  driver is the XEMBED client side. This is the one piece of new X
  plumbing; the S2 smoke should confirm SDL3's embedded-focus
  handling on FNX's Xfb (fallback: explicit `SetInputFocus` routing).
- **Lifecycle rule**: the container owns the SDL window — view close
  ⇒ `SDL_DestroyWindow` before the X subwindow dies (the same
  teardown discipline as every other owned-native-object rule).
- **Acceptance**: a first-party Argentum app hosts a running SDL3
  demo (soft-rendered animation + tone) inside its window;
  keyboard input reaches the demo; closing the app tears the SDL
  window down without X errors; resize reconfigures the subwindow
  cleanly.
- **Scope gate**: if the desktop's window model needs extension for
  child-window focus policy, that is part of this milestone —
  embedding must not be bolted onto a model that fights it.

### S4 — On-FNX self-rebuild
Rebuild SDL3 on-FNX (CMake in-guest, per the roster) then rebuild
the demo against it — the manifest's full self-host cycle.

## 5. What it unlocks, and the honest frame

SDL is an **enabler, not a feature**: it is the standard porting
door for permissive games/emulators/media. Which apps come through
it is decided per-app at adoption time (RetroArch-class GPL apps are
out at the app layer, not the SDL layer — the door stays open to
permissive apps). Satellites (SDL_image/mixer/ttf/net) are separate
later adoptions — SDL_ttf is the natural first (FNX already builds
freetype + harfbuzz).

## 6. Relationship & out of scope

- Complements the X11 stack; replaces nothing. First-party GUI stays
  Argentum UIKit on X11 — SDL is the *third-party* path, never the
  house toolkit; the sanctioned bridge is the **UIKit-embeddable
  view** (§S4): SDL renders as a guest inside a UIKit window and
  never becomes the toolkit.
- Out (recorded): GL/GLES/Vulkan, KMS/DRM/Wayland backends, all
  satellites in v1, SDL2 as a second library (compat layer instead).
- Manifest: admission row to `self-hosting-packages.md` §B at S0 —
  zlib, 3.4.16 pin, CMake-only (no autotools in-tree by upstream
  choice), no new build-time tools; x11-leaf additions (if any) are
  an x11-shared-build update, not a new package.
