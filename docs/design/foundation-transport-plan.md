# The Foundation transport: libcurl

Status: **DECIDED (2026-09-21) — W7's slice 2; not yet landed.** The unit it serves is
docs/design/foundation-plan.md's **W7, the URL loading system**.

## 1. The decision

W7's row names three prerequisites: W6 (done), **a transport** (HTTP over the shipped
socket layer), and a credential store. The plan's recorded position for the first was
*"the socket layer ships; the protocol layer is ours to write"* (§12.6). **The user's
decision (2026-09-21) replaces that: vendor libcurl and let it own the transport.** W7's
HTTP layer becomes a **binding over libcurl** rather than a first-party protocol
implementation, and §12.6's row is corrected in place rather than left standing.

## 2. Why the licence was never the obstacle, and what the real cost is

- **Licence**: libcurl is under **curl's own MIT/X-derivative licence** (SPDX id `curl`;
  "inspired by MIT/X, but not identical"). Permissive, no copyleft — admissible under
  `self-hosting-packages.md` §4 ("permissive license, no GPL/LGPL on-FNX"). It is **not
  verbatim MIT**, and that is recorded here because the house code is MIT and a vendored
  dependency is not the house code.
- **Build**: curl ships a **`CMakeLists.txt`**, and CMake is already the roster's
  configure generator (§C: it is what builds the C++ runtimes, and what the LibreSSL plan
  mandates for that library). **curl's autotools layer is never invoked** — the same rule
  `libressl-plan.md` states for its own library, now applied here.
- **What it owns, accepted deliberately**: connection establishment, DNS, HTTP/1.1 (and
  2/3 if enabled), redirects, chunked transfer, connection reuse, and cookies. That is
  exactly the surface W7 would otherwise have had to write *and test*, and it is why the
  fork in the road was **scope**, not licence.
- **What it must NOT duplicate: TLS.** `libressl-plan.md` decides LibreSSL/`libtls` as the
  one system SSL library. So the first landing is built **without any TLS backend — plain
  HTTP** — and HTTPS arrives when LibreSSL lands, with curl bound to LibreSSL's
  OpenSSL-compatible `libssl`/`libcrypto`. **No second TLS library enters the tree.**

**AND HTTPS IS NOW IN SCOPE (user's decision, 2026-09-21): LIBRESSL IS BROUGHT FORWARD so this
transport lands WITH `https://`.** That makes libcurl **LibreSSL-plan §5/L2's first real
consumer** — the trigger that plan was waiting for. The measurement that settles *which* backend,
read off curl's own option list at the 8.22 pin, is that there was no cheaper route:

| curl TLS backend | verdict |
|---|---|
| **OpenSSL API (→ LibreSSL)** | the one that composes — curl reports it as `LibreSSL/x.y` |
| GnuTLS | LGPL — blocked by the admission rule |
| wolfSSL | GPLv2 / commercial |
| Rustls | Rust — excluded by the toolchain doctrine (clang/LLVM family) |
| mbedTLS | permissive, but `libressl-plan.md` REJECTED it by name as "not a system library" |
| Schannel | Windows only |
| BearSSL, SecureTransport | **REMOVED from curl in 8.22** — a `-D` naming one is silently unused |

So LibreSSL is not merely this plan's preference; with the project's own rules applied it is the
**only** backend left standing. Until L2 lands, `tools/curl-build.sh` asserts the negative
(`Enabled SSL backends:` empty, `NEEDED` = `libc.so` only) and the smoke probe pins that
`https://` answers `CURLE_UNSUPPORTED_PROTOCOL` — a check that **flips to a real handshake** when
the TLS binding arrives, exactly as the plan's §45-Y select check flipped when its rule landed.

## 3. The pin and the recipe

- **Pin: curl 8.22.0** — the newest release on curl's download index at 2026-09-21
  (measured: `8.17.0 … 8.22.0`).
- **`third_party/curl`** as a submodule, like every other vendored source here, checked
  out at tag **`curl-8_22_0`**.
- **`tools/curl-build.sh`**, following the pattern `tools/lcms2-build.sh` established:
  built **twice** — the guest with `tools/musl-clang64.sh` into `.build/curl-prefix`, and
  the host with the host compiler into `.build/curl-host-prefix`. (Whether the host copy
  is needed at all is a finding for the script's first run: the Foundation's probes are
  guest-only today, so the host half exists only if a host consumer appears.)
- **Staged** the way every shared library here is staged: the **versioned real file plus
  its soname symlink** into `/System/Libraries/` (never the bare dev symlink), and the
  public headers into `/System/Shared/Headers/curl/`.
- **Image budget — MEASURED, not assumed.** `.build/rootagfs.img` is **128 MB** (the mk
  rule packs `128`), and the last pack used 104 140 blocks of 65 536 addressable… i.e.
  **~21 MB free**, not the ~1.9 MB a stale note in this repo's memory claimed from a 64 MB
  image. libcurl's shared object — and even libssl + libcrypto when TLS arrives — fit. The
  stale figure is corrected here so the next session does not reason from it.

## 4. The slices, in order

1. **2a — the seam, no dependency.** `NSURLProtocol` + `NSURLProtocolClient`, plus the two
   types their signatures need (`NSCachedURLResponse`, `NSURLCacheStoragePolicy`). This is
   Apple's API **regardless of who owns the transport**: a `NSURLProtocol` subclass is how
   *any* protocol plugs in, and the curl bridge in 2c will itself be one. It is therefore
   first, and it is the only slice here with no build dependency.
2. **2b — vendor and build libcurl** (the pin above, plain HTTP), staged, with a smoke
   probe that proves the library **loads and runs on the guest**. **LANDED AND VERIFIED
   (2026-09-21): `curl_smoke` 6/6 probe checks and 6/6 case checks** — the library loads, it moves
   bytes over `file://`, it reaches `connect()` for `http://`, and it refuses `https://` with
   `CURLE_UNSUPPORTED_PROTOCOL` rather than degrading.

   **AND IT TOOK A KERNEL FACT TO GET THERE, WHICH IS THE PART WORTH CARRYING FORWARD.** The first run
   answered `CURLE_OUT_OF_MEMORY` for EVERY URL — `file://` and an IP literal included — while the same
   process could `malloc(8MB)`. Measured step by step: `curl_multi_init()` returned NULL and its `errno`
   was **38, `ENOSYS`**. The chain, read out of curl's own source and this kernel's:
   `curl_easy_perform` → `curl_multi_init` → `Curl_multi_handle` → `Curl_wakeup_init` → `wakeup_eventfd`
   → **`eventfd(0, EFD_CLOEXEC|EFD_NONBLOCK)`** — and **this kernel implements no `eventfd` at all**, so
   the call answers ENOSYS, the wakeup pair never initialises, and perform reports the failure as
   "out of memory".

   **A BUILD-TIME PROBE CANNOT SEE THAT**: musl's headers declare `eventfd`, so the link check passes and
   curl defines `USE_EVENTFD`. The fix is the measured alternative —
   `-DHAVE_EVENTFD=0 -DHAVE_SYS_EVENTFD_H=0`, which selects curl's `pipe2` path, verified working in the
   same run (`pipe2(O_NONBLOCK|O_CLOEXEC)=0`, alongside `pipe()=0` and `socketpair(AF_UNIX,STREAM)=0`).
   `curl_smoke` keeps `eventfd` and `timerfd` diagnostic lines so the kernel gap stays visible rather
   than becoming folklore. **The gap itself is RECORDED rather than silently worked around: FNX has
   neither `eventfd` nor `timerfd`, and any other program that assumes one will fail the same
   misleading way.**
3. **2c — the bridge and the session.** `FNCURLURLProtocol` (an `NSURLProtocol` subclass
   driving `curl_easy_*`), then `NSURLSession` + `NSURLSessionTask`/`NSURLSessionDataTask`
   + configuration/delegates — the surface a caller actually uses, and the last thing W7's
   ledger rows need.

## 5. Risks to settle during 2b, named rather than discovered later

- **curl's CMake build must configure with musl-clang without invoking autotools and
  without a host-language build step** (`self-hosting-packages.md` §4). If curl's CMake
  path needs Python or Perl, that is a **finding** to record — the same shape as the
  `llhttp` rejection (its TypeScript/Node codegen is why `llhttp` was not the pick).
- **Name resolution.** curl's threaded resolver needs pthreads (present), but its lookup
  path must go through musl's `getaddrinfo` against the FSH hosts domain. If the threaded
  resolver fights the test harness, `--disable-threaded-resolver` selects the synchronous
  path, which is a legitimate configuration and not a workaround.
- **A TLS-less build must not pretend otherwise.** With no TLS backend, `https://` URLs
  must FAIL with a clear error rather than silently degrading; that is a check in 2b's
  smoke probe, not a hope.
