# Image codec libraries — the decode/encode door

Status: **PLAN (2026-09) — decided in direction; not scheduled.**
A permissive set of image codec libraries as system libraries: the
decode (and where the desktop needs it, encode) door for the Viewer
app, UIKit image views, thumbnailing, and the SDL_image satellite
when it arrives.

## 1. Admission — licenses all permit

| Library | Formats | License | Build |
|---|---|---|---|
| **libpng** | PNG | zlib | CMake (already an x11 leaf for sbix glyphs — promoted to a system library here) |
| **libjpeg-turbo** | JPEG | BSD-3-Clause + IJG (permissive) | CMake; SIMD off first (no nasm dependency), SIMD a later accel |
| **giflib** | GIF | MIT (ESR) | **plain `Makefile.unx`** — ~10 small sources, no autotools invocation at all |
| **libwebp** | WebP | BSD-3 (Google) | CMake |
| **libtiff** | TIFF | BSD-3 family (permissive) | CMake |

All five clear the permissive roof; none drags autotools into any
build (png/turbo/webp/tiff are CMake-native, giflib is plain-make).
Pins are recorded when the manifest rows land at S0 (standing
policy — admission row accompanies the build, not the plan).

## 2. What "practical" means (honest scope)

- **Decode: all five**, plus first-party trivial formats where a
  library is overkill — BMP/PNM/PPM are already first-party territory
  (the Viewer's v1 BMP baseline) and need no third-party code.
- **Encode: only where the desktop needs it** — PNG (screenshots,
  document export), JPEG (photo export). WebP/TIFF encode are
  optional-by-consumer; GIF encode is not needed (GIF is a decode
  format for the desktop; animation stays a display concern).
- **Out of the practical set (recorded, not rejected)**: AVIF (needs
  the aom/dav1d codec world), JPEG-XL, HEIC, raw-camera formats —
  heavy codec ecosystems; each gets a trigger (a real consumer or
  format demand), same as every other future adoption.

## 3. Placement & relationship

- Shared objects → `/System/Libraries/` (flat; loader search spans
  `System/Libraries` + `Shared/Libraries`): `libpng`, `libjpeg`,
  `libgif`, `libwebp`, `libtiff` + versioned realnames; headers
  consumed at build time from pinned checkouts (no global header
  tree).
- **Consumers**: the Viewer app (document-type opening already in its
  design — the codecs make it an image viewer), UIKit image views,
  thumbnails, and the **SDL_image satellite** (recorded in the SDL
  plan) which rides on these exact libraries. libpng additionally
  stays an x11 leaf for sbix glyphs — one library, two roles.
- First-party facade: none initially — the C APIs are fine for the
  C++ toolkit to call directly; a unified Image I/O facade is only a
  direction if three-plus consumers all hand-roll the same glue.

## 4. Milestones

### I0 — Cross-seed build (host)
CMake builds of png/turbo/webp/tiff + a house build of giflib
(`Makefile.unx` pattern, not its configure) against the musl
sysroot; SIMD off; shared libraries. **Acceptance**: five `.so`
files + headers staged; a host-side decode round-trip of one file
per format.

### I1 — Stage into FSH + in-guest decode smoke
Stage `/System/Libraries`. **Acceptance (in-guest)**: a first-party
probe (the `imgprobe`-class test tool) decodes a known PNG/JPEG/GIF/
WebP/TIFF from the root image and pixel-verifies the result against
a reference — the established decode-verification pattern (like
`fbdump`/tone checks), no network needed.

### I2 — First consumers
Viewer app opens PNG/JPEG/GIF/WebP/TIFF by document type; UIKit
image views decode through the same libs; screenshot save emits PNG.
**Acceptance**: the Viewer displays all five formats from the
desktop's file manager; a screenshot round-trips (capture → PNG →
view).

### I3 — SDL_image satellite (when the SDL plan's satellites land)
SDL_image builds against these libraries (libpng/jpeg/webp) rather
than vendoring its own — the image door is the foundation, SDL_image
is a consumer. **Acceptance**: an SDL demo loads a PNG/JPEG texture
through SDL_image, embedded per the SDL plan's S3.

## 5. Relationship & non-goals

- Replaces nothing (BMP/PNM decode stays first-party); libpng's x11
  leaf role is unchanged, it gains a system-library role.
- Non-goals: format *purity* work (decoder hardening is upstream's
  job — we pin and track), codec accel beyond optional later SIMD,
  the heavy codec ecosystems listed in §2, and any first-party
  reimplementation of a licensed format.
