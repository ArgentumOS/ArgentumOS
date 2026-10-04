/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSInvocation.m — the invocation, the register classification, the forwarding
 * handler, and the runtime hooks that make forwarding happen at all.
 *
 * MANUAL OWNERSHIP: it owns C buffers and implements no -retain/-release.
 */

#import <Foundation/NSInvocation.h>
#import <Foundation/NSMethodSignature.h>
#import <Foundation/NSString.h>
#import <Foundation/NSException.h>
#import "NSMethodSignature.h"
#import "NSInvocation.h"

#include <stdlib.h>
#include <string.h>

#import <objc/hooks.h>

/* The most arguments fn_classify can place: 6 integer + 8 SSE, self and _cmd
 * included in the first count. */
#define FN_MAX_ARGUMENTS	14

/* ------------------------------------------------------- the classification */

/* Where one argument travels. FN_STACK is the boundary: v1 refuses it. */
enum { FN_INT, FN_STACK, FN_FP };

typedef struct fn_placement {
	int kind;
	size_t offset;			/* into the register image */
} fn_placement_t;

/* Map the signature's arguments onto the register file, and REFUSE what does not
 * fit. self and _cmd land in gpr[0]/gpr[1] because that is where the ABI puts
 * them — they are arguments like any other, which is why this walk starts at
 * index 0. */
static int fn_classify(NSMethodSignature *signature, fn_placement_t *places,
		       NSUInteger *bad)
{
	NSUInteger count = [signature numberOfArguments];
	NSUInteger gpr = 0, sse = 0, i;

	if (count > FN_MAX_ARGUMENTS) {
		*bad = FN_MAX_ARGUMENTS;
		return 0;
	}
	for (i = 0; i < count; i++) {
		NSUInteger size = [signature fnSizeOfArgumentAtIndex:i];
		BOOL floating = [signature fnArgumentIsFloatingAtIndex:i];

		if (size == 0 || size > sizeof(uint64_t)) {
			*bad = i;
			return 0;	/* a value this v1 does not marshal */
		}
		if (floating) {
			if (sse >= 8) {
				*bad = i;
				return 0;
			}
			places[i].kind = FN_FP;
			places[i].offset = FN_OFF_SSE + sse * 16;
			sse++;
		} else {
			if (gpr >= 6) {
				*bad = i;
				return 0;
			}
			places[i].kind = FN_INT;
			places[i].offset = FN_OFF_GPR + gpr * 8;
			gpr++;
		}
	}
	return 1;
}

/* The SSE count the callee is told about (al): the floating arguments, capped at
 * the eight registers that can hold them. */
static NSUInteger fn_sse_count(NSMethodSignature *signature)
{
	NSUInteger count = [signature numberOfArguments];
	NSUInteger sse = 0, i;

	for (i = 0; i < count; i++) {
		if ([signature fnArgumentIsFloatingAtIndex:i] && sse < 8) {
			sse++;
		}
	}
	return sse;
}

/* The byte offset of an argument's slot inside _argumentStorage. */
static size_t fn_argument_offset(NSMethodSignature *signature, NSUInteger index)
{
	size_t offset = 0;
	NSUInteger i;

	for (i = 0; i < index; i++) {
		NSUInteger size = [signature fnSizeOfArgumentAtIndex:i];

		offset += (size != 0) ? size : 1;
	}
	return offset;
}

/* The width of the argument slots: at least one byte each, so two zero-width
 * arguments cannot share a slot. */
static size_t fn_argument_storage_size(NSMethodSignature *signature)
{
	NSUInteger count = [signature numberOfArguments];
	size_t total = 0, i;

	for (i = 0; i < count; i++) {
		NSUInteger size = [signature fnSizeOfArgumentAtIndex:i];

		total += (size != 0) ? size : 1;
	}
	return total;
}

/* The runtime's own retain/release entry points: this file is ARC, and ARC will
 * not manage a reference that lives in the argument BYTES — so -retainArguments
 * says +1 and -dealloc says -1 explicitly, which is exactly what that pair is for.
 * (NSObject.m reaches for the same two, from the other side of the seam.) */
extern id objc_retain(id obj);
extern void objc_release(id obj);

/* Is this type an object? -retainArguments only owns the OBJECT arguments, and
 * "eight bytes wide" is not the same question: a `long` is eight bytes too. */
static int fn_type_is_object(const char *type)
{
	while (type != NULL && (*type == 'r' || *type == 'n' || *type == 'N' ||
				*type == 'o' || *type == 'O' || *type == 'R' ||
				*type == 'A')) {
		type++;
	}
	return type != NULL && *type == '@';
}

/* A selector as an integer, and back: a cast between them is deprecated, and a
 * union is the honest way to say "the same bits". */
static uint64_t fn_selector_bits(SEL selector)
{
	union { SEL sel; uintptr_t raw; } pun;

	pun.raw = 0;
	pun.sel = selector;
	return (uint64_t)pun.raw;
}

static SEL fn_selector_from_bits(uint64_t bits)
{
	union { SEL sel; uintptr_t raw; } pun;

	pun.raw = (uintptr_t)bits;
	return pun.sel;
}

static id fn_proxy_lookup(id receiver, SEL op);	/* installed by +load below */

/*
 * The runtime asks THIS for the IMP to call with a forwarded method's arguments,
 * and the answer is the capture trampoline — which is signature-agnostic, because
 * it saves the WHOLE register file and the C half reads the signature out of the
 * receiver.
 */
static IMP fn_forward_for(id receiver, SEL selector)
{
	(void)receiver;
	(void)selector;
	return (IMP)fn_forward_entry;
}

/*
 * THE INSTALLATION ITSELF, and it is done TWICE ON PURPOSE. +load is the runtime's
 * contract for this (below, inside the implementation), and a CONSTRUCTOR is the
 * belt-and-braces that does not depend on that contract being honoured for a
 * SHARED LIBRARY — which is how this library is loaded, and which is why the first
 * version of this file forwarded nothing at all: every un-implemented message went
 * straight to -doesNotRecognizeSelector: (measured on the guest).
 *
 * Both do the same two assignments, so running both is harmless.
 */
static void fn_install_forwarding(void)
{
	objc_proxy_lookup = fn_proxy_lookup;
	__objc_msg_forward2 = fn_forward_for;
}

__attribute__((constructor))
static void fn_install_forwarding_at_load(void)
{
	fn_install_forwarding();
}

/* ------------------------------------------------------------ the invocation */

/* Private: the marshalling paths the forwarding handler needs, and the copy of an
 * image's arguments into the receiver's slots. Not in the header, because it is
 * not Cocoa's surface. */
@interface NSInvocation (FNImage)
- (void)fnInvokeWithTarget:(id)target imp:(IMP)imp;
- (void)fnSetSelector:(SEL)selector;			/* the handler's setter */
- (void)fnSetArgumentsFromImage:(const fn_regs_t *)regs;
- (void)fnStoreReturnIntoImage:(fn_regs_t *)regs;
@end

@interface NSInvocation ()
- (id)initWithMethodSignature:(NSMethodSignature *)signature;
@end

@implementation NSInvocation

+ (NSInvocation *)invocationWithMethodSignature:(NSMethodSignature *)signature
{
	if (signature == nil) {
		return nil;
	}
	return [[self alloc] initWithMethodSignature:signature];
}

- (id)initWithMethodSignature:(NSMethodSignature *)signature
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_signature = signature;
	_returnLength = [signature fnSizeOfReturnValue];
	if (_returnLength > 0) {
		_returnValue = (char *)calloc(1, _returnLength);
	}
	{
		size_t bytes = fn_argument_storage_size(signature);

		if (bytes > 0) {
			_argumentStorage = (char *)calloc(1, bytes);
		}
	}
	return self;
}

- (void)dealloc
{
	if (_argumentsRetained && _argumentStorage != NULL) {
		NSUInteger count = [_signature numberOfArguments];
		NSUInteger i;

		for (i = 0; i < count; i++) {
			if (fn_type_is_object([_signature getArgumentTypeAtIndex:i])) {
				id *slot = (id *)(_argumentStorage +
						  fn_argument_offset(_signature, i));

				objc_release(*slot);
			}
		}
	}
	free(_argumentStorage);
	free(_returnValue);
	[super dealloc];	/* NSObject's -dealloc is what frees the instance */
}

- (NSMethodSignature *)methodSignature
{
	return _signature;
}

- (void)retainArguments
{
	/* The OBJECT arguments, which is what Cocoa's contract is about: the bytes are
	 * already this invocation's own copy (see -setArgument:atIndex:), but an object
	 * IN them would not survive its owner. So the objects get an explicit +1 here,
	 * and -dealloc gives it back. */
	NSUInteger count = [_signature numberOfArguments];
	NSUInteger i;

	if (_argumentsRetained || _argumentStorage == NULL) {
		return;
	}
	_argumentsRetained = YES;
	for (i = 0; i < count; i++) {
		if (fn_type_is_object([_signature getArgumentTypeAtIndex:i])) {
			id *slot = (id *)(_argumentStorage +
					  fn_argument_offset(_signature, i));

			*slot = objc_retain(*slot);
		}
	}
}

- (BOOL)argumentsRetained
{
	return _argumentsRetained;
}

- (id)target
{
	return _target;
}

- (void)setTarget:(id)target
{
	_target = target;
}

- (SEL)selector
{
	return _selector;
}

- (void)setSelector:(SEL)selector
{
	_selector = selector;
}

- (void)fnSetSelector:(SEL)selector
{
	_selector = selector;
}

- (void)getArgument:(void *)argumentLocation atIndex:(NSInteger)index
{
	if (argumentLocation == NULL || index < 0 ||
	    (NSUInteger)index >= [_signature numberOfArguments]) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSInvocation: -getArgument:atIndex: %ld is out of range (%lu argument(s))",
				   (long)index,
				   (unsigned long)[_signature numberOfArguments]];
	}
	memcpy(argumentLocation,
	       _argumentStorage + fn_argument_offset(_signature, (NSUInteger)index),
	       [self fnSlotSizeAtIndex:(NSUInteger)index]);
}

- (void)setArgument:(void *)argumentLocation atIndex:(NSInteger)index
{
	NSUInteger size;

	if (argumentLocation == NULL || index < 0 ||
	    (NSUInteger)index >= [_signature numberOfArguments]) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSInvocation: -setArgument:atIndex: %ld is out of range (%lu argument(s))",
				   (long)index,
				   (unsigned long)[_signature numberOfArguments]];
	}
	size = [self fnSlotSizeAtIndex:(NSUInteger)index];
	if (size == 0 || size > sizeof(uint64_t)) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSInvocation: argument %ld is a %lu-byte value — v1 marshals register arguments only",
				   (long)index, (unsigned long)size];
	}
	memcpy(_argumentStorage + fn_argument_offset(_signature, (NSUInteger)index),
	       argumentLocation, size);
}

/* One argument's slot width, and never zero: a zero-width type still owns a byte,
 * so two of them cannot alias. */
- (NSUInteger)fnSlotSizeAtIndex:(NSUInteger)index
{
	NSUInteger size = [_signature fnSizeOfArgumentAtIndex:index];

	return (size != 0) ? size : 1;
}

- (void)getReturnValue:(void *)retLoc
{
	if (retLoc == NULL) {
		return;
	}
	if (_returnLength > 0 && _returnValue != NULL) {
		memcpy(retLoc, _returnValue, _returnLength);
	}
}

- (void)setReturnValue:(void *)retLoc
{
	if (retLoc == NULL || _returnLength == 0 || _returnValue == NULL) {
		return;
	}
	memcpy(_returnValue, retLoc, _returnLength);
}

- (void)invoke
{
	[self invokeWithTarget:_target];
}

- (void)invokeUsingIMP:(IMP)imp
{
	/* NO METHOD LOOKUP AT ALL: the caller's IMP IS the implementation, which is what this door means. */
	[self fnInvokeWithTarget:_target imp:imp];
}

- (void)invokeWithTarget:(id)target
{
	Method method;

	if (target == nil || _selector == NULL) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSInvocation: -invoke needs a target and a selector"];
	}
	method = class_getInstanceMethod(object_getClass(target), _selector);
	if (method == NULL) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSInvocation: -[%s %s] is not implemented",
				   class_getName(object_getClass(target)),
				   sel_getName(_selector)];
	}
	[self fnInvokeWithTarget:target imp:method_getImplementation(method)];
}

- (void)fnInvokeWithTarget:(id)target imp:(IMP)imp
{
	fn_placement_t places[FN_MAX_ARGUMENTS];
	fn_regs_t regs;
	NSUInteger bad = 0, count, i;
	size_t returnSize = [self fnReturnSlotSize];
	int returnIsFloat = [self fnReturnSlotIsFloat];

	if (target == nil || _selector == NULL) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSInvocation: -invoke needs a target and a selector"];
	}
	if (!fn_classify(_signature, places, &bad)) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSInvocation: argument %lu does not fit the register subset (v1 marshals up to six integers and eight floats, no stack arguments, no value types)",
				   (unsigned long)bad];
	}
	if (returnSize > sizeof(uint64_t)) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSInvocation: a %lu-byte return value is not in the register subset",
				   (unsigned long)returnSize];
	}
	count = [_signature numberOfArguments];
	memset(&regs, 0, sizeof regs);
	/* self and _cmd first, as the ABI has them — the classification put them in
	 * gpr[0]/gpr[1], and here they are the CALL's target and selector. */
	regs.gpr[0] = (uint64_t)(uintptr_t)target;
	regs.gpr[1] = fn_selector_bits(_selector);
	for (i = 2; i < count; i++) {
		const char *source = _argumentStorage + fn_argument_offset(_signature, i);
		uint64_t value = 0;

		memcpy(&value, source, places[i].kind == FN_FP
						 ? 8 : [self fnSlotSizeAtIndex:i]);
		if (places[i].kind == FN_FP) {
			memcpy(&regs.sse[(places[i].offset - FN_OFF_SSE) / 16], source,
			       [self fnSlotSizeAtIndex:i]);
		} else {
			regs.gpr[(places[i].offset - FN_OFF_GPR) / 8] = value;
		}
	}
	regs.sse_count = fn_sse_count(_signature);
	fn_call_image(imp, &regs);
	if (returnSize > 0 && _returnValue != NULL) {
		if (returnIsFloat) {
			memcpy(_returnValue, &regs.return_fp, returnSize);
		} else {
			memcpy(_returnValue, &regs.return_int, returnSize);
		}
	}
}

/* The return value's slot: its width, capped so the memcpy below can never write
 * past the 8-byte register image field. */
- (size_t)fnReturnSlotSize
{
	size_t size = [_signature fnSizeOfReturnValue];

	return (size > sizeof(uint64_t)) ? (sizeof(uint64_t) + 1) : size;
}

- (BOOL)fnReturnSlotIsFloat
{
	return [_signature fnReturnIsFloating];
}

- (void)fnSetArgumentsFromImage:(const fn_regs_t *)regs
{
	fn_placement_t places[FN_MAX_ARGUMENTS];
	NSUInteger bad = 0, count, i;

	if (!fn_classify(_signature, places, &bad)) {
		return;			/* refused: -invoke will say why */
	}
	count = [_signature numberOfArguments];
	for (i = 0; i < count; i++) {
		NSUInteger size = [self fnSlotSizeAtIndex:i];
		const void *source;

		if (places[i].kind == FN_FP) {
			source = &regs->sse[(places[i].offset - FN_OFF_SSE) / 16];
		} else {
			source = &regs->gpr[(places[i].offset - FN_OFF_GPR) / 8];
		}
		memcpy(_argumentStorage + fn_argument_offset(_signature, i), source, size);
	}
}

- (void)fnStoreReturnIntoImage:(fn_regs_t *)regs
{
	size_t size = [self fnReturnSlotSize];

	if (size == 0 || size > sizeof(uint64_t) || _returnValue == NULL) {
		return;
	}
	if ([self fnReturnSlotIsFloat]) {
		memcpy(&regs->return_fp, _returnValue, size);
	} else {
		memcpy(&regs->return_int, _returnValue, size);
	}
}

/*
 * THE INSTALLATION. The runtime's defaults answer "no forwarding here" (a nil
 * slot, a nil proxy), so a library that wants to forward installs both — and this
 * is where the forwarding trio stops being three declarations and becomes a
 * mechanism. libobjc2 exports them exactly for this (objc/hooks.h).
 *
 * IT LIVES INSIDE THE IMPLEMENTATION, before @end: a method may not follow the
 * file-scope C functions below, which clang reports as "missing context for method
 * declaration" (measured).
 */
+ (void)load
{
	fn_install_forwarding();
}

@end

/* -------------------------------------------------------- the runtime hooks */

/*
 * -forwardingTargetForSelector: is ARGUMENT-FREE forwarding, and it is the fast
 * path the runtime offers BEFORE it hands over a call: objc_proxy_lookup's answer
 * makes the runtime retry the lookup on the new object, so no invocation is built
 * at all. The hook runs for EVERY failed lookup, so the guard matters — posting
 * that message to an object that does not implement it would forward it, forever.
 */
static id fn_proxy_lookup(id receiver, SEL op)
{
	if (receiver == nil) {
		return nil;
	}
	/* The guard is asked the ORDINARY way, through -respondsToSelector:, rather
	 * than by comparing sel_registerName's untyped selector against the method's
	 * typed one: a lookup that goes through the class is the one the rest of this
	 * library trusts. */
	if ([receiver respondsToSelector:@selector(forwardingTargetForSelector:)]) {
		return [receiver forwardingTargetForSelector:op];
	}
	return nil;
}

/*
 * The C half of the capture. fn_forward_regs is copied OUT FIRST, because the
 * trampoline's image is a single buffer: nothing may call back into the trampoline
 * while an image is still being read.
 */
void fn_forward_handler(void)
{
	fn_regs_t regs = fn_forward_regs;
	id receiver = (id)(uintptr_t)regs.gpr[0];
	SEL selector = fn_selector_from_bits(regs.gpr[1]);
	NSMethodSignature *signature;
	NSInvocation *invocation;

	if (receiver == nil) {
		return;
	}
	signature = [receiver methodSignatureForSelector:selector];
	if (signature == nil) {
		[receiver doesNotRecognizeSelector:selector];
		return;
	}
	invocation = [NSInvocation invocationWithMethodSignature:signature];
	[invocation setTarget:receiver];
	[invocation fnSetSelector:selector];
	[invocation fnSetArgumentsFromImage:&regs];
	[receiver forwardInvocation:invocation];
	[invocation fnStoreReturnIntoImage:&regs];
	fn_forward_regs.return_int = regs.return_int;
	fn_forward_regs.return_fp = regs.return_fp;
}
