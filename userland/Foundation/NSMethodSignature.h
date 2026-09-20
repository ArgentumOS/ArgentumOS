/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSMethodSignature — the types a method takes and returns.
 * docs/design/foundation-plan.md, the dependency queue (stage F, first half).
 *
 * It is a PARSER over the runtime's type encoding and nothing else: no
 * invocation, no ABI. That is why it can ship on its own, and why it is the half
 * NSInvocation will be built on — an invocation must know each argument's TYPE
 * and SIZE, and every one of those answers comes from here.
 *
 * WHAT THE STRING IS. A method's encoding is the return type followed by the
 * arguments, with self (@) and _cmd (:) written explicitly, so
 * `-setObject:forKey:` is "@@:@@". +signatureWithObjCTypes: therefore takes the
 * FIRST type as the return and EVERY LATER type as an argument, which is also
 * what makes a bare @encode string usable. The grammar understood is the one
 * clang and the runtime emit: the primitive codes, the qualifiers (r n N o O R,
 * with V read as oneway), `^` pointers, `[n type]` arrays, `{name=...}` structs,
 * `(name=...)` unions, `bN` bitfields, `@"Class"` quoted object names and `@?`
 * blocks. Anything unrecognised is skipped as ONE opaque type, so an unknown
 * extension cannot desynchronise the argument walk.
 *
 * THE SIZES ARE OURS, stated rather than borrowed: this target's primitive
 * widths (LP64 — `l` is 8 bytes, `long double` 16), struct fields summed with
 * natural alignment, a bitfield contributing its declared bits rounded to a
 * byte, and a type the parser cannot measure contributing 0 (the type string is
 * still reported verbatim, which is the honest answer and one an invocation can
 * refuse on). -frameLength is the word-aligned total of the arguments, including
 * self and _cmd: Cocoa's is architecture-specific and unspecified, this one is
 * written down.
 *
 * NOT HERE: NSInvocation, and with it -forwardInvocation: and
 * -forwardingTargetForSelector: on NSObject — stage F's second half, where the
 * x86-64 argument marshalling lives.
 */

#ifndef FOUNDATION_NSMETHODSIGNATURE_H
#define FOUNDATION_NSMETHODSIGNATURE_H

#import <Foundation/NSObject.h>

/* NULLABILITY (F6, slice 4): NONNULL by default. The ONE exception is the factory,
 * which is a measured `return nil;` site in nmethodsignature.m — it refuses an
 * encoding it cannot parse rather than answering a half-built signature. The
 * accessors are not nullable: an index past the end RAISES NSRangeException, and
 * the type strings are owned copies the parser always fills in. */
NS_ASSUME_NONNULL_BEGIN

@interface NSMethodSignature : NSObject
{
	char *_types;			/* our own copy of the encoding */
	const char **_argumentTypes;	/* one owned string PER argument */
	NSUInteger _argumentCount;
	const char *_returnType;	/* an owned string of its own */
	NSUInteger _returnLength;
	NSUInteger _frameLength;
	BOOL _oneway;
}

+ (nullable NSMethodSignature *)signatureWithObjCTypes:(const char *)types;

- (NSUInteger)numberOfArguments;	/* self and _cmd included, as Cocoa counts them */
- (const char *)getArgumentTypeAtIndex:(NSUInteger)index;
- (const char *)methodReturnType;
- (NSUInteger)methodReturnLength;	/* 0 for void */
- (NSUInteger)frameLength;		/* our definition: word-aligned argument bytes */
- (BOOL)isOneway;

NS_ASSUME_NONNULL_END

@end

/*
 * THE PRIVATE HALF, folded in from fnmethodsignature.h: the declarations this library shares internally.
 * They are HERE because the public headers are now the only headers - what used to be a
 * private file two units imported is a section of the class's own header. The region is its
 * own one only when this point in the header is outside the header's own (a nested region
 * does not compile, and neither does an unclosed one).
 */
NS_ASSUME_NONNULL_BEGIN
@interface NSMethodSignature (FNPrivate)

/* One argument's width in bytes, 0 when the type cannot be measured (or is
 * absent); and whether it travels in an SSE register rather than an integer one. */
- (NSUInteger)fnSizeOfArgumentAtIndex:(NSUInteger)index;
- (BOOL)fnArgumentIsFloatingAtIndex:(NSUInteger)index;

/* The same two questions for the return value. */
- (NSUInteger)fnSizeOfReturnValue;
- (BOOL)fnReturnIsFloating;

@end
NS_ASSUME_NONNULL_END


#endif /* FOUNDATION_NSMETHODSIGNATURE_H */
