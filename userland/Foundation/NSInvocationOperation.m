/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSInvocationOperation.m — the implementation, MANUAL OWNERSHIP. docs/design/foundation-plan.md §62.22.
 *
 * TWO METHODS OF SUBSTANCE: `-main` runs the invocation and STASHES what it raised, and `-result` is Apple's
 * five-rule paragraph implemented in the order its own sentences imply (the header lists all five). Everything else
 * is a constructor or an accessor.
 *
 * WHY THE EXCEPTION IS STASHED RATHER THAN THROWN: `-start` is the caller's frame, and Apple's sentence says where
 * the failure is seen — "if an exception was raised during the execution of the method or invocation, accessing this
 * property raises that exception again". So the run captures it, the operation finishes (a unit of work that failed
 * is still a unit of work that is DONE), and the caller meets the exception at the door that asks for the value.
 */

#import <Foundation/NSInvocationOperation.h>
#import <Foundation/NSException.h>
#import <Foundation/NSInvocation.h>
#import <Foundation/NSMethodSignature.h>
#import <Foundation/NSString.h>
#import <Foundation/NSValue.h>

#include <stdlib.h>

@implementation NSInvocationOperation

/* THE DESIGNATED INITIALIZER (Apple says so). The invocation is RETAINED and told to RETAIN ITS ARGUMENTS, which is
 * Apple's sentence at this door — without it an argument that was a stack object would be gone by the time the
 * operation ran. */
- (instancetype)initWithInvocation:(NSInvocation *)inv
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	if (inv == nil) {
		[self release];
		return nil;
	}
	_invocation = [inv retain];
	[_invocation retainArguments];
	return self;
}

- (nullable instancetype)initWithTarget:(id)target selector:(SEL)sel object:(nullable id)arg
{
	NSInvocation *invocation;
	NSMethodSignature *signature;
	/* THE BUILD HAPPENS BEFORE `self` IS TOUCHED, so a failure here needs no half-built receiver: the release below
	 * is on the object `+alloc` answered, which is what MRC asks of an initializer that has not yet delegated. */
	if (target == nil || sel == NULL || ![target respondsToSelector:sel]) {
		/* APPLE'S DOCUMENTED NIL: "an initialized object or nil if the target object does not implement the
		 * specified selector" — the target is data that can be wrong, so this door answers rather than raises. */
		[self release];
		return nil;
	}
	signature = [target methodSignatureForSelector:sel];
	invocation = [NSInvocation invocationWithMethodSignature:signature];
	if (invocation == nil) {
		[self release];
		return nil;
	}
	[invocation setTarget:target];
	[invocation setSelector:sel];
	/* THE ARGUMENT IS SET AS AN OBJECT, AND ONLY WHEN THE SELECTOR TAKES ONE: `NSMethodSignature` counts `self` and
	 * `_cmd`, so "takes one" is more than two. See the header for why a non-object parameter needs the other door. */
	if (signature != nil && [signature numberOfArguments] > 2) {
		[invocation setArgument:&arg atIndex:2];
	}
	/* AND THE DESIGNATED DOOR IS THE ONE THAT STORES IT, so `-retainArguments` has exactly one home. */
	return [self initWithInvocation:invocation];
}

- (void)dealloc
{
	NSInvocation *invocation = _invocation;
	id result = _result;
	NSException *raised = _exception;

	_invocation = nil;
	_result = nil;
	_exception = nil;
	[invocation release];
	[result release];
	[raised release];
	[super dealloc];
}

- (NSInvocation *)invocation
{
	return _invocation;
}

/* THE RUN: invoke, and remember a failure rather than letting it out of `-start`. */
- (void)main
{
	NSMethodSignature *signature = [_invocation methodSignature];
	const char *returnType = signature != nil ? [signature methodReturnType] : NULL;
	NSUInteger returnLength = signature != nil ? [signature methodReturnLength] : 0;

	@try {
		[_invocation invoke];
	} @catch (NSException *raised) {
		_exception = [raised retain];
		return;
	}
	if (returnType == NULL || returnType[0] == 'v' || returnLength == 0) {
		return;		/* nothing to carry: -result raises for this case, by Apple's sentence */
	}
	if (returnType[0] == '@') {
		id object = nil;

		[_invocation getReturnValue:&object];
		_result = [object retain];
		return;
	}
	/* ANYTHING THAT IS NOT AN OBJECT IS CARRIED AS ITS BYTES: Apple's sentence is "an NSValue object containing the
	 * return value if it is not an object", and the return LENGTH is what makes the copy safe. */
	{
		void *bytes = malloc(returnLength);

		if (bytes != NULL) {
			[_invocation getReturnValue:bytes];
			_result = [[NSValue valueWithBytes:bytes objCType:returnType] retain];
			free(bytes);
		}
	}
}

- (nullable id)result
{
	NSMethodSignature *signature;
	const char *returnType;

	/* 1. NOT FINISHED ANSWERS NIL — Apple's first sentence, and it is checked first because a cancelled operation
	 * that never ran IS finished, so both of the next two rules still hold. */
	if (![self isFinished]) {
		return nil;
	}
	/* 2. A CANCELLED OPERATION RAISES, which is what NSInvocationOperationCancelledException is for. */
	if ([self isCancelled]) {
		[NSException raise:NSInvocationOperationCancelledException
			    format:@"-[NSInvocationOperation result]: the operation was cancelled"];
	}
	/* 3. WHAT THE RUN RAISED IS RAISED AGAIN. */
	if (_exception != nil) {
		[_exception raise];
	}
	/* 4. A VOID RETURN TYPE RAISES, which is what NSInvocationOperationVoidResultException is for: there is no
	 * value, and nil would be indistinguishable from a method that answered nil. */
	signature = [_invocation methodSignature];
	returnType = signature != nil ? [signature methodReturnType] : NULL;
	if (returnType == NULL || returnType[0] == 'v') {
		[NSException raise:NSInvocationOperationVoidResultException
			    format:@"-[NSInvocationOperation result]: the method returns void"];
	}
	/* 5. AND OTHERWISE THE VALUE — the object itself, or the NSValue `-main` boxed. */
	return _result;
}

@end
