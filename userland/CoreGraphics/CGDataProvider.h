/*
 * CGDataProvider — a block of bytes, with a lifetime.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * WHY THIS EXISTS AT ALL, AND WHY IT IS THE DOOR: Apple's `CGColorSpaceCreateICCBased` takes a
 * `CGDataProviderRef` for the profile, and a data provider is a CORE GRAPHICS type — not a Core
 * Foundation one. That is the whole reason the ICC half of C4 can be built without resolving
 * the Foundation question plan §6 leaves open: `CGDataProviderCreateWithCFData` and
 * `CGDataProviderCreateWithURL` are CF-typed and stay absent, while these two forms carry the
 * same bytes with nothing but C in the signature.
 *
 * WHAT A PROVIDER OWNS IS THE POINT OF THE TYPE. `CreateWithData` ADOPTS the caller's bytes:
 * they must stay valid until the provider is released, and then the caller's `releaseData`
 * callback — if one was given — is invoked with the same `info` that was handed in. That is
 * Apple's contract, and it is the reason a provider is not just a struct with a pointer in it:
 * the lifetime is the interface. `CreateWithFilename` reads the file and owns the bytes itself.
 *
 * AND THE ACCESSOR IS INTERNAL. `CGDataProviderCopyData` hands back a CFDataRef in Apple's
 * signature and is absent here; the library's own consumers ask through
 * `CGDataProvider_internal.h`, which is not installed and not included by anything public.
 */
#ifndef CORE_GRAPHICS_CGDATAPROVIDER_H
#define CORE_GRAPHICS_CGDATAPROVIDER_H

#include <CoreGraphics/CGBase.h>
/* `off_t` IS NAMED BY THE CALLBACK FAMILY BELOW — Apple's own header takes it from CoreFoundation's
 * includes, and this library says where it comes from instead of inheriting a coincidence. */
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CGDataProvider *CGDataProviderRef;

/* Called when the provider lets go of data it was given. `info` is what the creator passed,
 * `data` and `size` are the same two the creator passed. A NULL callback means "the bytes are
 * not mine to release", which is the usual case for a static array. */
typedef void (*CGDataProviderReleaseDataCallback)(void *info, const void *data, size_t size);

/* ADOPTS `data` — it is not copied — so it must stay valid until the last release, which is
 * when `releaseData` is called. NULL data is REFUSED rather than kept: a provider with no
 * bytes cannot answer the only question it exists to answer, and every consumer would have to
 * check for it anyway. */
CGDataProviderRef CGDataProviderCreateWithData(void *info, const void *data, size_t size,
					       CGDataProviderReleaseDataCallback releaseData);

/* READS THE WHOLE FILE, and OWNS what it read, so the caller need not keep anything alive. A
 * file that cannot be opened, or cannot be read to its end, gives NULL and says why: a profile
 * that is silently truncated would be parsed as a different profile. */
CGDataProviderRef CGDataProviderCreateWithFilename(const char *filename);

CGDataProviderRef CGDataProviderRetain(CGDataProviderRef provider);
void CGDataProviderRelease(CGDataProviderRef provider);

/* THE FOUNDATION-OBJECT FORM: `NSData *` WHERE APPLE'S NAME SAYS `CFData`. The name is Apple's
 * and stays — this layer duplicates the API, corners included — and the argument is a Foundation
 * object, which is the binding §1 records.
 *
 * THE BYTES ARE NOT COPIED. The provider takes a reference to the NSData and lets go of it when
 * the provider is released, so the object's lifetime is the provider's; that is Apple's contract
 * for this form and the reason the C half can keep treating "a buffer plus a callback" as the one
 * shape a provider has. */
CGDataProviderRef CGDataProviderCreateWithCFData(NSData *data);

/* THE BYTES AS A FOUNDATION OBJECT — the read side of the same door, and A COPY: the caller owns
 * the result, so a provider may be released the moment its caller is done. Handing back a view of
 * the provider's memory would tie the two lifetimes together for no reason a caller asked for.
 * `CG_RETURNS_RETAINED` SAYS THAT OWNERSHIP OUT LOUD, which is what an ARC caller needs to hear:
 * without it, ARC would take the +1 return for +0 and leak the copy. */
NSData *CGDataProviderCopyData(CGDataProviderRef provider) CG_RETURNS_RETAINED;

#ifdef __cplusplus
}
#endif

/* THE TYPE IDENTITY OF THIS CLASS: a `CFTypeID`, the same for every object of the class and
 * different from every other class's. The value is THIS LIBRARY'S (Apple's are runtime-assigned and
 * published nowhere), which is why the header says so rather than implying a constant someone could
 * port; identity is the whole of what the door promises. See CGTypeID_internal.h. */
CGTypeID CGDataProviderGetTypeID(void);

/* ------------------------------------------------------------------------- */
/* The callback forms: a provider whose bytes come from somewhere else        */
/* ------------------------------------------------------------------------- */

/* APPLE'S TWO FAMILIES, TRANSCRIBED WITH THEIR FIELD NAMES AND THEIR ORDER (version first, then the
 * callbacks as the header lists them), because THE STRUCT LAYOUT IS THE INTERFACE here: a caller fills one
 * of these in and hands it over. `version` is 0, in the 10.6 header's own words. */
typedef size_t (*CGDataProviderGetBytesCallback)(void *info, void *buffer, size_t count);
typedef off_t (*CGDataProviderSkipForwardCallback)(void *info, off_t count);
typedef void (*CGDataProviderRewindCallback)(void *info);
typedef void (*CGDataProviderReleaseInfoCallback)(void *info);
typedef const void *(*CGDataProviderGetBytePointerCallback)(void *info);
typedef void (*CGDataProviderReleaseBytePointerCallback)(void *info, const void *pointer);
typedef size_t (*CGDataProviderGetBytesAtPositionCallback)(void *info, void *buffer, off_t position,
							   size_t count);

/* SEQUENTIAL: A CURSOR. getBytes reads forward from it, skipForward moves it, rewind sends it back. THIS
 * LIBRARY CANNOT ASK SUCH A SOURCE FOR BYTES IT HAS ALREADY PASSED, so the provider READS IT ONCE into its
 * own buffer the first time anything asks for the bytes and every later reader sees that buffer — which is
 * what makes `CGDataProviderCopyData` and the decoders work on a sequential source at all. `rewind` is
 * called after that read, so the caller's source is left usable. */
typedef struct CGDataProviderSequentialCallbacks {
	unsigned int version;
	CGDataProviderGetBytesCallback getBytes;
	CGDataProviderSkipForwardCallback skipForward;
	CGDataProviderRewindCallback rewind;
	CGDataProviderReleaseInfoCallback releaseInfo;
} CGDataProviderSequentialCallbacks;

/* DIRECT: RANDOM ACCESS, `size` bytes, and AT LEAST ONE of the two access callbacks — Apple's header says so
 * and this library refuses a provider made with neither. `getBytePointer` returns the WHOLE block, and this
 * library BORROWS it for the provider's life (returning it through `releaseBytePointer` at the end), because
 * that is what the pointer is for; without it, `getBytesAtPosition` is called as bytes are needed. */
typedef struct CGDataProviderDirectCallbacks {
	unsigned int version;
	CGDataProviderGetBytePointerCallback getBytePointer;
	CGDataProviderReleaseBytePointerCallback releaseBytePointer;
	CGDataProviderGetBytesAtPositionCallback getBytesAtPosition;
	CGDataProviderReleaseInfoCallback releaseInfo;
} CGDataProviderDirectCallbacks;

/* A SEQUENTIAL SOURCE WITH NO `getBytes` IS REFUSED: a provider that cannot answer the only question it
 * exists for answers NULL later, far from the call that was wrong. */
CGDataProviderRef CGDataProviderCreateSequential(void *info,
						 const CGDataProviderSequentialCallbacks *callbacks);
CGDataProviderRef CGDataProviderCreateDirect(void *info, off_t size,
					     const CGDataProviderDirectCallbacks *callbacks);

/* A PROVIDER OVER A URL'S BYTES: FILE URLs only, everything else REFUSED BY NAME. The reading is the
 * filename form's — the same code that already reads a font or a profile — so this door is a URL-to-path
 * step and a refusal, not a second file reader. Declared here and implemented with the Foundation object it
 * takes, like the CF-typed form above. */
CGDataProviderRef CGDataProviderCreateWithURL(NSURL *url);

#endif /* CORE_GRAPHICS_CGDATAPROVIDER_H */
