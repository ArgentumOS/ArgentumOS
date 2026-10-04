/*
 * CGDataProviderFoundation — the provider's FOUNDATION-OBJECT forms.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * APPLE'S NAME SAYS `CFData` AND THE ARGUMENT IS AN `NSData`: the name stays, because this tree
 * duplicates Apple's API including its spelling, and the argument is a Foundation object because
 * that is the binding the retracted CoreFoundation plan settled (coregraphics-plan.md §1).
 *
 * AND THERE IS NO SECOND IMPLEMENTATION HERE. A provider made from an NSData IS a provider made
 * from a caller's bytes — the same constructor, the same release callback, the same ownership
 * rule — with the NSData as the thing the callback lets go of. `CGDataProviderCreateWithData`
 * already adopts a buffer and calls back when the provider dies; that is exactly what an NSData
 * needs, so this file is a bridge and not a second provider.
 *
 * NO ARC, matching the library's policy: ARC is for everything that USES Foundation, and using
 * it in the library itself is unimportant so long as it works. The retains and releases below
 * are therefore explicit, and they are the whole of the ownership story.
 */
#import <Foundation/Foundation.h>

#include <CoreGraphics/CGDataProvider.h>
#include <CoreGraphics/CGDataProvider_internal.h>

#include <stdio.h>

/* THE CALLBACK THAT MAKES THE PROVIDER'S LIFETIME HOLD THE NSDATA UP: `info` is the object the
 * creator retained, and `data` — the pointer taken from `-bytes` — is valid for exactly as long
 * as that object lives. Letting the pointer outlast the object is the bug this function exists
 * to prevent. */
static void cg_release_data_object(void *info, const void *data, size_t size)
{
	(void)data;
	(void)size;
	[(NSData *)info release];
}

CGDataProviderRef CGDataProviderCreateWithCFData(NSData *data)
{
	CGDataProviderRef provider;

	if (data == nil) {
		fprintf(stderr, "CG-REFUSE: CGDataProviderCreateWithCFData needs data\n");
		return NULL;
	}
	/* `-bytes` IS BORROWED AND `-length` IS THE SIZE, so the retain above the call is what makes
	 * the borrow live as long as the provider. IF THE CONSTRUCTOR REFUSES, the retain has to be
	 * undone here — it is the only place that knows the object was taken. */
	provider = CGDataProviderCreateWithData((void *)[data retain], [data bytes],
						(size_t)[data length], cg_release_data_object);
	if (provider == NULL) {
		[data release];
	}
	return provider;
}

NSData *CGDataProviderCopyData(CGDataProviderRef provider)
{
	const void *bytes;
	size_t size = 0;

	bytes = cg_dataprovider_bytes(provider, &size);
	if (bytes == NULL) {
		return nil;
	}
	/* A COPY, AND THE COPY IS THE CALLER'S: `-initWithBytes:length:` is a +1 method, so returning
	 * it directly is correct under this library's manual memory management — no autorelease, and
	 * nothing borrowed. Handing back a view of the provider's own memory would be the other
	 * choice, and it is not this one: a provider can be released the moment its caller is done
	 * with it, and the bytes would go with it. */
	return [[NSData alloc] initWithBytes:bytes length:size];
}
