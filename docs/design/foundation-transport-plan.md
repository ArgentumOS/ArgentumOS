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
**only** backend left standing. **THE FLIP HAS HAPPENED (2026-09-22, with LibreSSL-plan §5/L2):**
`tools/curl-build.sh` now asserts the POSITIVE (`Enabled SSL backends: LibreSSL`, `Protocols: file http
https`, `NEEDED` = `libcrypto.so.57` + `libssl.so.60`), and the smoke probe's checks are
`curl-has-the-libressl-backend` and `curl-https-reaches-connect` — the same two checks that used to pin
the negative (`Enabled SSL backends:` empty, `https://` answering `CURLE_UNSUPPORTED_PROTOCOL`). That
is the second time a check has been flipped rather than deleted; the rule that makes it work is that the
check asserts the SPECIFIC fact that changes, so the flip is visible in the diff.

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

   **LANDED AND VERIFIED (2026-09-22): `foundation_urlprotocol` 19/19 probe checks and 6/6 case checks.**
   `userland/Foundation/NSURLProtocol.{h,m}` (the base plus `NSURLProtocolClient`) and
   `NSCachedURLResponse.{h,m}` (the value plus `NSURLCacheStoragePolicy`), the probe and its case, the
   `mk/20-userland.mk` + `Foundation.h` wiring, and the ledger's eleven rows moved to `shipped`. No
   transport, no session, no socket: it is the plug-in point and a value.

   **THE DECISIONS IT STATES OUT LOUD, each pinned by a check:**
   * **The base is abstract, and its defaults ARE its behaviour:** `+canInitWithRequest:` answers NO,
     `+canonicalRequestForRequest:` returns the request unchanged, `+requestIsCacheEquivalent:toRequest:`
     is value equality, and `-startLoading`/`-stopLoading` do nothing — asserted as "the client was told
     nothing", which is stronger than "it did not crash".
   * **The request-property table is keyed by IDENTITY and RETAINS its key.** A request is an immutable
     value, so identity is the only handle a caller can name twice; the retention is load-bearing, because
     without it a freed request's address could be recycled into a new one that would then answer the DEAD
     request's properties — a wrong answer rather than a crash, which is the worse kind. The rule a caller
     can depend on: **per instance, and a copy starts empty.**
   * **Registration order is a definite rule, stated rather than implied:** most-recently-registered first,
     only a subclass may register (the base refuses itself, or a class whose every override point is the
     default would shadow every real protocol behind it), and unregistration COMPACTS rather than
     tombstoning.
   * **`-URLProtocolDidFinishLoading:` carries no `protocol:` argument** — Apple's own inconsistency, kept
     and asserted rather than "fixed", since fixing it would produce a selector no existing implementation
     implements.
   * **`+fnProtocolClassForRequest:` is a first-party door, and it is what makes the slice testable:**
     Apple dispatches a request inside its loader and does not publish that step, but 2c's loader must do
     exactly this, and a registry whose ORDER cannot be observed cannot be pinned by a check.
   * **Refused by name, and asserted ABSENT by `urlprotocol-api-inventory`:** the two authentication
     callbacks (they need `NSURLAuthenticationChallenge`, its own family and its own slice) and the coder
     doors (a keyed archive format whose keys Apple does not publish).

   **AND THE GATE CAUGHT A REAL MISTAKE WHILE THIS LANDED, which is worth keeping:** `NSURLProtocol.m`
   imported `<Foundation/NSMutableDictionary.h>`, a header that does not exist in this tree —
   `NSMutableDictionary` is declared in `NSDictionary.h`. The clean-room gate refused the build and named
   the line, which is the check doing what it was written for rather than a formality. The ledger's own
   count block and the plan's family table are GENERATED, so the four `shipped` rows had to be followed by
   `tools/foundation-sweep.py --refresh` and `--families --write`; the build is what tells you.

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

   **ITS FIRST HALF HAS LANDED: THE BRIDGE (2026-09-22), `foundation_urlprotocol_curl` 6/6 probe checks
   and 6/6 case checks.** `userland/Foundation/FNCURLURLProtocol.{h,m}`, the probe and its case, and the
   build change below. NO SESSION IS INVOLVED, which is the point of splitting here: the bridge is an
   ordinary `NSURLProtocol` subclass, so a probe can start one by hand with a client and watch it — and
   the plan's own sentence already ordered it that way ("`FNCURLURLProtocol` … then `NSURLSession`").

   **THE DECISIONS IT STATES OUT LOUD:**
   * **It claims exactly the schemes curl speaks here** — `file`, `http`, `https`; `ftp` is NOT claimed,
     so the seam answers "no protocol handles this" instead of a protocol failing halfway.
   * **curl owns the transfer and the bridge reports it:** `didReceiveResponse:` once, `didLoadData:`
     per chunk (streamed, not accumulated), then EXACTLY ONE of finished/failed. The probe asserts the
     ORDER, because a probe that only checked "the bytes arrived" would pass for a bridge that reported
     them wrongly.
   * **A redirect is REPORTED, not followed** (`CURLOPT_FOLLOWLOCATION=0`): the seam has
     `wasRedirectedToRequest:redirectResponse:` for precisely this, and whether to follow is the
     caller's/loader's decision — which is where 2c's session will put "follow by default".
   * **The transfer runs on its own thread** (`curl_easy_perform` blocks, and the seam's contract is that
     `-startLoading` returns), so the callbacks arrive from that thread. **`-stopLoading` stops at the
     next chunk** rather than interrupting a blocked read — the honest v1, since a cross-thread
     `curl_easy` call is undefined behaviour — and the header says so instead of implying otherwise.
   * **The cache advice is `NSURLCacheStorageNotAllowed`**, a decision rather than a default: there is no
     cache in this library yet (`NSURLCache` is its own slice), so "allowed" would promise what nothing
     keeps.

   **AND IT MADE THE LIBRARY DEPEND ON LIBCURL, which is the build half of this slice and is stated as
   a cost:** `libfoundation.so` now links `-lcurl -lssl -lcrypto` **in that order** (curl NEEDs LibreSSL's
   entry points, the same fact `curl_smoke` records), with no RPATH — the guest loader resolves
   `libcurl.so.4` out of `/System/Libraries`, where 2b stages it. Precedent rather than a new kind of
   dependency: the library already needs `libz` and ICU. The transitive chain then has to be findable at
   LINK time for every executable, which surfaced as `libssl.so.60, needed by libfoundation.so, not found
   (try using -rpath or -rpath-link)` on all 41 probe links; the fix is the precise one the warning names
   — `-Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib` beside each
   `-L$(FNXLIB) -lfoundation`, rather than every executable growing three `-l` flags it never calls.

   **TWO BUILD-LEVEL TRAPS WORTH CARRYING, both hit and both fixed here:**
   * **A comment inside a backslash-continued recipe is not a makefile comment** — it becomes part of the
     SHELL command, so the `#` commented out every flag after it and the link silently failed with
     `Error 127`. Recipe comments go ABOVE the command.
   * **`-lcurl` alone is not enough at link time** when the executable pulls libcurl: its undefined
     `SSL_*`/`X509_*` references need `-lssl -lcrypto` AFTER it. The dependency order was already written
     in this very plan's comment when it was hit; quoting it is not the same as applying it.

   **AND THE SESSION HALF IS ITSELF SPLIT, BECAUSE ITS FAMILY IS THE LARGEST IN THE LEDGER:** measured, it
   is **11 classes + 6 protocols + 46 cases + 10 enums**, so "the session" is not one landing. The order
   follows the one that worked twice already — a value, then the model, then the behaviour:

   1. **the configuration** (LANDED 2026-09-22, `foundation_urlsession_config` 7/7 probe checks and 6/6
      case checks): `NSURLSessionConfiguration`, the class a session cannot be created without. Its
      documented defaults live in ONE place, `-copy` is a REAL copy (every property is readwrite, so the
      `-retain` the immutable classes answer with would hand back something a caller could not
      distinguish from the original), and `protocolClasses` is carried — the one property that reaches
      slice 2a's registry.
      **AND THE HONEST HALF IS THE EPHEMERAL DOOR:** Apple documents it as keeping no persistent caches,
      cookies or credentials, and THIS LIBRARY SHIPS NONE OF THOSE CLASSES YET — so in this tree the two
      built-in doors differ only by identity, which the header states and the probe PINS rather than
      letting the door imply behaviour it cannot have. `URLCache`, `HTTPCookieStorage`,
      `URLCredentialStorage` and the cookie family's enum are refused BY NAME for the same reason: each
      is its own ledger row.
   2. **the session and the task model**: `NSURLSession`, `NSURLSessionTask`/`NSURLSessionDataTask` and
      the three delegate protocols a session references — creation, identity, the task factories and the
      state machine, with NO EXECUTION (next).
   3. **the execution**: a task run through `FNCURLURLProtocol` and reported to its delegate and its
      completion handler — the half that makes the model do something.

   **ROW 3, FIRST HALF LANDED (2026-09-22): `foundation_urlsession_task` 7/7 probe checks and 6/6 case
   checks, plus `fn_block_mrc` 3/3.** A resumed session task now RUNS: the session picks a protocol class
   (the configuration's `protocolClasses` first, then slice 2a's registry), starts it, and reports the
   ending into the task's state, its response, its byte count and its completion handler. The DELEGATE
   callbacks are the other half of this row and land with the task/data delegate protocols.

   **TWO THINGS IT COST, AND BOTH ARE WORTH MORE THAN THE ROW:**

   * **`[block copy]` IS A MESSAGE SEND, AND THAT WAS A CRASH.** Storing a caller's completion handler with
     `[completionHandler copy]` made the runtime read the BLOCK'S ISA to find its class, and that read
     faulted with a null page — reproducibly, from an ARC caller and an MRC twin alike, while a NIL
     handler survived (a send to nil is a no-op on this runtime). `Block_copy()`/`Block_release()` — the
     runtime entry points — perform the same stack-to-heap copy with NO message send and cannot depend on
     the isa. The library now uses them. **The measurement that found it was a TWIN that called the same
     method twice, once with nil and once with a block**: that turned a large diff into a single argument.
     `fn_block_mrc` is kept as the regression guard for it.
   * **A SESSION DOES NOT KNOW A TRANSPORT EXISTS UNLESS THE PROCESS SAYS SO.** The first green-attempt
     run reported a completion handler that fired with **zero bytes and an error**: the session had
     consulted an empty `protocolClasses` and an empty registry, and answered
     `NSURLErrorUnsupportedURL` — which is exactly what the probe's own no-protocol-class check pins.
     Registering `FNCURLURLProtocol` (as any real client must) is what makes a fetch a fetch. **A default
     registration is a design question for the session's second half, not an omission here**, and the
     probe's header says so.

   **ROW 2 LANDED (2026-09-22), `foundation_urlsession` 12/12 probe checks and 6/6 case checks:**
   `NSURLSessionTask`/`NSURLSessionDataTask` (plus `NSURLSessionTaskState`) and `NSURLSession` with its own
   `NSURLSessionDelegate` — the session and the task model, with NOTHING TRANSFERRING. Four ledger rows
   moved to `shipped`. **The task/data delegate protocols are deferred to row 3 on purpose:** they are
   about REPORTING a transfer, so they land where there is something to report.

   **IT CAUGHT A REAL BUG IN ITSELF, WHICH IS THE ARGUMENT FOR THE CHECK THAT FOUND IT:** the first run was
   11/12, and the failure was `shared-session-is-a-singleton` — `+sharedSession` was built with plain
   `-init`, which this class does not define, so the shared session answered a **nil configuration**. The
   check that caught it is the one asserting the shared session's DEFAULTS, not merely its identity; an
   identity-only check would have passed. `+sharedSession` now goes through the same initializer as every
   other door.

   **THE DECISIONS THIS ROW PINS:**
   * **a session SNAPSHOTS its configuration** — a caller still editing the configuration it handed over
     cannot reach a running session (the same discipline the request headers and the cached response keep,
     and the one behaviour here that is observed rather than declared);
   * **a new task is SUSPENDED** and `-resume` is an explicit act; a COMPLETED task is never resumed;
   * **`-cancel` goes straight to Completed carrying `NSURLErrorCancelled` (-999)** — a simplification
     stated where it happens, because with no transfer running there is no Canceling period to pass
     through, and `task-cancel-completes-and-records-the-error` is the check that will HAVE to change when
     execution lands;
   * **an invalidated session refuses a new task with nil** rather than handing back one that could never
     run (Apple documents only that a session must not be used after invalidating; nil is our answer);
   * **refused by name, asserted absent**: the completion-handler and download/upload/stream/websocket
     factories (execution and their own ledger rows), the challenge member (`NSURLAuthenticationChallenge`
     is its own family) and the coder doors.


## 4b. WHERE W7 STANDS, AND WHAT IS LEFT (measured 2026-09-22)

**SLICE 2c IS COMPLETE, AND ITS EVIDENCE IS THE TIER:** `foundation_urlsession_config` 7/7,
`foundation_urlsession` 12/12, `foundation_urlsession_task` 11/11, `fn_block_mrc` 3/3, whole fast tier
56/56 cases and 343/343 checks. A session is made from a configuration it snapshots, hands out tasks
that carry unique identifiers and a state machine, makes them RUN through `FNCURLURLProtocol`, reports the
ending into the task, its response, its byte count and its completion handler, and reports to its task and
data delegates ON THE SESSION'S OWN QUEUE — with a completion-handler task and a delegate-driven task both
driven in one file. There is no deviation left in the header to apologise for.

**WHAT REMAINS, IN THE ORDER THE DEPENDENCIES FORCE, and this list is the unit's own survey rather than a
plan:**

1. **THE FOUR SIBLING TASK KINDS** — `NSURLSessionUploadTask`, `NSURLSessionDownloadTask`,
   `NSURLSessionStreamTask`, `NSURLSessionWebSocketTask`. Each is its own ledger row and none is started.
   **`DownloadTask` is the one to take first**: it is the only one whose transfer the bridge already
   performs (a `file://` fetch lands the bytes), so its new surface is the DESTINATION (a temporary file
   the delegate is handed a URL for) rather than a new transport.
2. **THE RESPONSE-DISPOSITION DOOR** — `-URLSession:dataTask:didReceiveResponse:completionHandler:` —
   **and it is BLOCKED ON (1) RATHER THAN ON WORK**: it takes an `NSURLSessionResponseDisposition`, whose
   `BecomeDownload` and `BecomeStream` cases name the sibling kinds, so shipping it now would ship half an
   enum with cases pointing at classes that do not exist. That is the whole reason it is refused in
   `NSURLSession.h`, and it is why (1) comes first.
3. **THE CREDENTIAL FAMILY** — `NSURLAuthenticationChallenge`, `NSURLCredential`, `NSURLCredentialStorage`,
   `NSURLProtectionSpace` — plus the authentication members of every delegate protocol, all refused by name
   today because they share one dependency: **the credential store's decision is the Keychain question**
   (`keychain-plan.md`, which is a plan and not a started unit).
4. **THE COOKIE FAMILY** — `NSHTTPCookie`, `NSHTTPCookieStorage` and `NSHTTPCookieAcceptPolicy`, refused by
   name in `NSURLSessionConfiguration` because a property typed with a class nothing can create is a
   signature with no implementation behind it.
5. **`NSURLCache`, THE STORE ITSELF** — `NSCachedURLResponse` and `NSURLCacheStoragePolicy` shipped with
   2a, so this is the thing that would hold one. Its absence is why the bridge advises
   `NSURLCacheStorageNotAllowed`: there is no store to be honest with yet.
6. **THE CONSTANT MASSES** — `NSURLError*` (146 rows, the largest single family in W7) and the
   `NSURLSessionAuthChallenge*` / `NSURLRequest*` cases. They are cheap individually and belong with the
   classes they describe rather than in a batch of their own.

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
