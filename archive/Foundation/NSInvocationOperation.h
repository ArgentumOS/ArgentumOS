/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSInvocationOperation — an operation that runs ONE INVOCATION: a target, a selector and its argument, held as an
 * object. §62.22 of docs/design/foundation-plan.md, and the second half of the App Support / Operations family.
 *
 * IT IS THE BRIDGE THE FAMILY WAS MISSING: `NSOperation` needs a subclass with a `-main`, `NSInvocation` can call a
 * method on a target it was told about, and this class is the two joined — which is also what makes the two
 * EXCEPTION NAMES that have been in `NSException.h` since F4 finally have a raiser (`-result` is the door that
 * raises both, and the header they live in pointed at a class that did not exist).
 *
 * `-result` IS THE INTERESTING HALF OF THE CLASS, and Apple's paragraph for it is five rules rather than one, all
 * implemented in its own order:
 *
 *   1. **NOT FINISHED YET ANSWERS NIL** — "nil if the method or invocation is not finished executing";
 *   2. **A CANCELLED OPERATION RAISES** `NSInvocationOperationCancelledException`, which is what that name is for.
 *      The check is after the first one because a cancelled operation that never ran IS finished (that is what
 *      `NSOperation`'s `-start` does), so both sentences hold without contradicting each other;
 *   3. **AN EXCEPTION RAISED DURING THE RUN IS RAISED AGAIN** — so the failure is not swallowed and not turned into
 *      a nil a caller would read as "no value". `-main` STASHES it rather than letting it out of `-start`, because
 *      Apple's sentence says when the caller sees it;
 *   4. **A VOID RETURN TYPE RAISES** `NSInvocationOperationVoidResultException` — there is no value to answer, and
 *      answering nil would be indistinguishable from a method that returned nil;
 *   5. **AND OTHERWISE THE VALUE**: the object itself when the return type is an object, and an `NSValue` carrying
 *      the bytes when it is not — Apple's sentence ("the object returned by the method or an `NSValue` object
 *      containing the return value if it is not an object"), which is why `NSMethodSignature`'s return TYPE and
 *      return LENGTH are both read here.
 *
 * THE TWO INITIALIZERS, AND THE ONE THAT CAN ANSWER NIL: `-initWithInvocation:` is the DESIGNATED one (Apple says
 * so) and it tells the invocation to RETAIN ITS ARGUMENTS, so an argument that was a stack object is still there
 * when the operation runs. `-initWithTarget:selector:object:` answers NIL WHEN THE TARGET DOES NOT IMPLEMENT THE
 * SELECTOR — Apple's documented return value rather than a raise, since the target is data that can be wrong.
 *
 * THE ONE THING THE CONVENIENCE INITIALIZER DOES NOT GUESS: the argument is set as AN OBJECT at index 2, which is
 * what Apple's `object:` parameter is, and only when the selector takes one (`NSMethodSignature` counts `self` and
 * `_cmd`, so "takes one" is `numberOfArguments > 2`). A selector whose first parameter is a number, a struct or a
 * pointer needs `-initWithInvocation:` and its own `-setArgument:atIndex:` — stated rather than papered over,
 * because there is no way to know what the bytes meant.
 */

#ifndef FOUNDATION_NSINVOCATIONOPERATION_H
#define FOUNDATION_NSINVOCATIONOPERATION_H

#import <Foundation/NSOperation.h>

@class NSException;
@class NSInvocation;

NS_ASSUME_NONNULL_BEGIN

@interface NSInvocationOperation : NSOperation
{
	NSInvocation *_invocation;
	id _result;		/* the value the invocation returned, once it has run */
	NSException *_exception;	/* what the invocation raised, re-raised by -result */
}

/* APPLE'S CONVENIENCE DOOR, and its documented nil: a target that does not implement the selector has no method to
 * invoke. */
- (nullable instancetype)initWithTarget:(id)target selector:(SEL)sel object:(nullable id)arg;

/* THE DESIGNATED INITIALIZER, which retains the invocation's arguments for the run ahead of it. */
- (instancetype)initWithInvocation:(NSInvocation *)inv;

@property (readonly, retain) NSInvocation *invocation;

/* FIVE RULES IN ONE PROPERTY, listed in the file comment above. `nullable` because one of them answers nil. */
@property (readonly, retain, nullable) id result;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSINVOCATIONOPERATION_H */
