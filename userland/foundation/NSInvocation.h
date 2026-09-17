/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSInvocation — a method call as an object.
 * docs/design/foundation-plan.md, the dependency queue (stage F, second half).
 *
 * It is the half of the forwarding trio that has to MARSHAL: the runtime's
 * __objc_msg_forward2 hook is handed a call with a method's own arguments and must
 * answer with an IMP that arrives WITH THEM, so the arguments have to be captured
 * from the register file (ninvoke_amd64.S), stored (here), and put back
 * (fn_call_image) when -invoke runs the method for real.
 *
 * THE ABI SUBSET IS THE DOCUMENTED BOUNDARY, and it is refused loudly rather than
 * half-supported: arguments and the return value travel in the REGISTERS —
 * integers and pointers up to six, floats and doubles up to eight, with self and
 * _cmd taking the first two integer registers as they do for every method. An
 * argument that needs the STACK, a struct or union passed by value, and a
 * `long double` all RAISE NSInvalidArgumentException from -setArgument:atIndex:
 * and -invoke. That covers the ordinary method surface; it is not the whole ABI,
 * and it does not pretend to be.
 *
 * -retainArguments is honoured for the OBJECT arguments only: they are retained
 * while the invocation lives, as Cocoa documents. The stored bytes are copies, so
 * they are already the invocation's own.
 */

#ifndef FOUNDATION_NSINVOCATION_H
#define FOUNDATION_NSINVOCATION_H

#import <foundation/NSObject.h>

@class NSMethodSignature;

@interface NSInvocation : NSObject
{
	NSMethodSignature *_signature;
	id _target;
	SEL _selector;
	char *_argumentStorage;		/* the argument slots, in order */
	char *_returnValue;
	NSUInteger _returnLength;
	BOOL _argumentsRetained;
}

+ (NSInvocation *)invocationWithMethodSignature:(NSMethodSignature *)signature;

- (NSMethodSignature *)methodSignature;

- (void)retainArguments;
- (BOOL)argumentsRetained;

- (id)target;
- (void)setTarget:(id)target;
- (SEL)selector;
- (void)setSelector:(SEL)selector;

- (void)getArgument:(void *)argumentLocation atIndex:(NSInteger)index;
- (void)setArgument:(void *)argumentLocation atIndex:(NSInteger)index;
- (void)getReturnValue:(void *)retLoc;
- (void)setReturnValue:(void *)retLoc;

/* -invoke uses the selector and target already set; -invokeWithTarget: sets the
 * target first. Both raise when the call cannot be expressed in the subset. */
- (void)invoke;
- (void)invokeWithTarget:(id)target;

@end

#endif /* FOUNDATION_NSINVOCATION_H */
