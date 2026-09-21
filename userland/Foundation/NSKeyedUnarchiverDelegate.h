/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSKeyedUnarchiverDelegate — the five doors an unarchiver offers while it reads.
 * docs/design/foundation-plan.md §12.3 W9.
 *
 * THE INTERESTING ONE IS `-unarchiver:cannotDecodeObjectOfClassName:originalClasses:`, which is unlike every
 * other delegate door in the library: it exists because an archive NAMES ITS CLASSES AS STRINGS, and a reader
 * may not have one. The arity says why — the delegate is handed the class name AND the chain of classes the
 * archive authorised, and it may answer a CLASS to decode in its place. Answering nil is the refusal, and the
 * refusal is what makes the door a security boundary rather than a compatibility one: an archive that names a
 * class the reader does not know is exactly the case an attacker would choose.
 *
 * `-unarchiver:didDecodeObject:` may also SUBSTITUTE — it receives each object as it is built and answers
 * what to use instead, which is how a delegate swaps a decoded surrogate for the real thing.
 *
 * EVERY MEMBER IS OPTIONAL, and the unarchiver asks `-respondsToSelector:` before each door, as the archiver
 * does on the writing side.
 */

#ifndef FOUNDATION_NSKEYEDUNARCHIVERDELEGATE_H
#define FOUNDATION_NSKEYEDUNARCHIVERDELEGATE_H

#import <Foundation/NSObject.h>

@class NSKeyedUnarchiver;
@class NSArray;
@class NSString;

NS_ASSUME_NONNULL_BEGIN

@protocol NSKeyedUnarchiverDelegate <NSObject>

@optional

/* The class named by the archive is not available. THE ANSWER IS THE CLASS TO USE INSTEAD, and nil is the
 * refusal — see the header's note on why this door is a boundary.
 *
 * APPLE DECLARES THIS PARAMETER `NSArray<NSString *> *` AND THIS LIBRARY DECLARES IT `NSArray *`, because the
 * container classes here are not lightweight-generic-parameterized. That is a RECORDED GAP rather than a
 * preference: it is the one place in this header where Apple's spelling cannot be repeated, and a caller who
 * wants to write the generic spelling needs the containers annotated. */
- (nullable Class)unarchiver:(NSKeyedUnarchiver *)unarchiver
   cannotDecodeObjectOfClassName:(NSString *)name
	      originalClasses:(NSArray *)classNames;

/* Informs the delegate that a given object has been decoded. THE ANSWER REPLACES it. */
- (nullable id)unarchiver:(NSKeyedUnarchiver *)unarchiver didDecodeObject:(nullable id)object;

/* Informs the delegate that one object is being substituted for another BY THE UNARCHIVER. */
- (void)unarchiver:(NSKeyedUnarchiver *)unarchiver willReplaceObject:(id)object withObject:(id)newObject;

/* The two halves of the end, around the point the object graph is closed. */
- (void)unarchiverWillFinish:(NSKeyedUnarchiver *)unarchiver;
- (void)unarchiverDidFinish:(NSKeyedUnarchiver *)unarchiver;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSKEYEDUNARCHIVERDELEGATE_H */
