/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSPurgeableData — AN NSMutableData WHOSE BYTES THE OWNER MAY THROW AWAY. W13c.
 *
 * IT IS A MUTABLE DATA FIRST, and that is what makes the protocol's handshake useful here: discarding means
 * "become empty", and BUILDING IT AGAIN IS JUST WRITING AGAIN — which is why `-beginContentAccess` answering
 * NO is not an error state but an instruction ("your bytes are gone; fill me in"). A write clears the
 * discarded state, so the object needs no separate "recreate" call.
 *
 * THE ACCESS COUNT IS WHAT MAKES IT SAFE: `-discardContentIfPossible` does nothing while a caller is inside
 * `-beginContentAccess`/`-endContentAccess`, so a cache cannot free the bytes out from under a reader. A
 * caller that never accesses and never writes is telling the owner the bytes may go.
 */

#ifndef FOUNDATION_NSPURGEABLEDATA_H
#define FOUNDATION_NSPURGEABLEDATA_H

#import <Foundation/NSData.h>
#import <Foundation/NSDiscardableContent.h>

NS_ASSUME_NONNULL_BEGIN

@interface NSPurgeableData : NSMutableData <NSDiscardableContent>
{
	NSUInteger _accessCount;	/* outstanding accesses: the reason a discard may be refused */
	BOOL _contentDiscarded;		/* the bytes are gone; a WRITE clears this */
}

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSPURGEABLEDATA_H */
