# CoreFoundation — an independent package this tree OWNS

**This is not a submodule.** It was forked from Apple's `swift-corelibs-foundation`
(`Sources/CoreFoundation`) at commit `44cd6163`, and from that commit onward it is this project's own
source: buildable from this directory, patchable in place, and free to diverge.

## Provenance and licence

* Upstream: `swift-corelibs-foundation`, `Sources/CoreFoundation` — fetched at `44cd6163`.
* Licence: **Apache-2.0 with the Runtime Library Exception** — see `LICENSE`, kept verbatim from upstream
  as the licence requires. The exception is what makes linking this into a non-Swift tree permissible.
* Original work in this package — the seam, the bridge header, the build, and every modification to an
  upstream file — is Copyright © 2026 Kyle J. Cardoza, MIT (see the repository's `LICENSE`).

## The modifications, stated

Every modification is marked IN PLACE with a comment naming it and citing the clause — the form is
`/* FNX LOCAL MODIFICATION <n> (Apache-2.0 §4(b)): ... */`, which is what the licence's section 4(b) asks a
modified file to carry.

`fnx-modifications.patch` is the frozen record of the FIRST SIX — **1, 3, 4, 5, 6, 7**, six modifications
across five files — taken before the fork was frozen, and still applied idempotently by `build.sh`. The
modifications made after the freeze — **8, 9, 10, 11** — are marked in place in the sources and listed below;
they are NOT in that patch, and it cannot be regenerated here because the upstream tree
(`third_party/swift-corelibs-foundation`) is no longer vendored. The patch and the in-place comments are the
same notice stated twice, once as a diff and once where a reader of the file will actually be standing.
(There is no modification 2: no file carries that marker.)

* **1** `include/ForSwiftFoundationOnly.h` — `<fts.h>` guarded by `__has_include` (this tree has no fts,
  and there are zero fts call sites).
* **3, 5** `internalInclude/CFInternal.h` — `ForSwiftFoundationOnly.h` included outside the
  `DEPLOYMENT_RUNTIME_SWIFT` guard (the thread API is declared only there and five files use it
  unguarded); the four dispatch macros made real, and `CF_IS_OBJC(typeID, obj)` defined as an **ISA
  comparison** — a CF-native object has `_cfisa == __CFISAForTypeID(typeID) == 0`, so any non-zero first
  word is an Objective-C object. That single definition is what makes CF dispatch into this tree's classes.
* **4** `CFRuntime.c` — `__kCFAllocatorTypeID_CONST`, which is defined nowhere in the whole tree, becomes
  `CFAllocatorGetTypeID()`.
* **6** `CFURLAccess.c` — a `typedef struct __NSString__ *NSString;` guarded so it does not collide with
  the real Objective-C class when `INCLUDE_OBJC` is on.
* **7** `CFArray.c` — `isKindOfClass:[NSMutableArray class]` becomes `objc_getClass("NSMutableArray")`,
  which removes the package's only symbolic reference to Foundation (the link no longer needs it).
* **8** `CFRuntime.c` — **THE OWNERSHIP ARM**, and the one modification made for the NEW Foundation rather
  than for building the old one. CF's type-specific doors already reach this tree's Objective-C objects
  (`CF_IS_OBJC` is an ISA comparison and our classes pass it). Its type-AGNOSTIC doors cannot: `CFRetain`
  and `CFRelease` have no type in hand, so they read a CFRuntimeBase header an Objective-C object does not
  have — measured, not reasoned, because a CFArray built with `kCFTypeArrayCallBacks` was DROPPED an object
  of Foundation's. The arm makes `CFRetain(obj)` and `[obj retain]` the same operation on the same word,
  which is the one-lifetime rule the new library is designed around. Upstream names the hole it fills: its
  own note about "a race between CFRetain / CFRelease (which call CF_IS_OBJC) and _CFRuntimeBridgeClasses".
* **9** `CFRuntime.c` — **THE BRIDGING DOOR**, the other half of free casting: `CFNXBridgeClassToType(Class,
  CFTypeID)` registers a class for a CF type, so CF-native objects are created with that class as their isa
  and `(NSString *)cfStr` becomes a messageable object while CF's own isa-comparing doors keep their C path.
* **10** `CFRuntime.c` — **THE HEADER WRITER**, exported: `CFNXSetInstanceTypeIDAndIsa` is
  `_CFRuntimeSetInstanceTypeIDAndIsa` (hidden-visibility upstream) made reachable, so Foundation writes the
  type-ID bits into an object's header through CF's own `__CFRuntimeSetValue` rather than packing them by
  hand. It deliberately does NOT route through `_CFRuntimeSetInstanceTypeID`, whose "is this transition
  legal" guard returns before its own write on an object that has no header yet.
* **11** `CFRuntime.c` — **THE CLASSES MUST EXIST BEFORE AN ISA IS COMPUTED**: CF asks Foundation to register
  its classes from INSIDE `_CFRuntimeCreateInstance` (weakly, so this package keeps NO dependency on
  Foundation), gated on `__CFInitialized` so the ask cannot arrive while the runtime is still being built.

## What is ours

* `build.sh` — the guest build. Upstream uses CMake; this is 86 translation units with no generated
  sources, so a third build system was not worth it, and the script carries the measurements that
  identified each thing CMake's `CMakeLists.txt` was contributing.
* `fnx-seam.m` — the host-environment seam (`_CFGetCurrentDirectory`, `_CFThreadSetName`).
* `fnx-bridge.h` — force-included after `CoreFoundation_Prefix.h`: the Objective-C declarations the
  bridged dispatch sites need, so CF's sources can name the classes they send messages to.

## Building

    ./corefoundation/build.sh          # leaves the library in .build/corefoundation-prefix

The output is `libcorefoundation.so.1` with a versioned SONAME. It links ICU, libdispatch,
libBlocksRuntime, libobjc and libc — and, deliberately, **not** libc++ and **not** libunwind.
