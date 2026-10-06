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

#endif /* CORE_GRAPHICS_CGDATAPROVIDER_H */
