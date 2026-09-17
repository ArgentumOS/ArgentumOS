/*
 * NSString — the immutable string, with its concrete and constant subclasses.
 * docs/design/foundation-plan.md, F1.
 *
 * UTF-8 IS THE STORAGE (the user's decision, 2026-09-17) and `-length` counts
 * BYTES. `-characterCount` counts Unicode characters and `-characterAtIndex:`
 * indexes THOSE — the honest reading of the name, and a documented difference
 * from Cocoa, whose `-length` counts UTF-16 code units.
 *
 * NSString HAS NO INSTANCE VARIABLES, AND THAT IS NOT AN OVERSIGHT. The
 * compiler emits `@"..."` as an object whose fields sit at FIXED offsets from
 * the object pointer — measured, and pinned by our runtime's own Test/Test.h,
 * whose NSConstantString subclasses a BARE root class rather than a
 * storage-carrying one. A subclass therefore cannot inherit storage: its own
 * ivars would be pushed past the offsets the runtime fills, and every constant
 * string would read foreign words. That is exactly how F1's first two attempts
 * failed (a SIGSEGV on a message send to a string literal), and it is why the
 * storage lives in the concrete subclasses here, as it does in Cocoa:
 *
 *   NSString            the contract and the value semantics (no ivars)
 *   NSOwnedString       a string that owns a UTF-8 buffer
 *   NSMutableString     NSOwnedString plus mutation
 *   NSConstantString    the compiler's layout, filled in by the runtime
 */

#ifndef FOUNDATION_NSSTRING_H
#define FOUNDATION_NSSTRING_H

#import <foundation/NSObject.h>
#include <stddef.h>

@interface NSString : NSObject <NSCopying>

/*
 * Creation. The `init` family returns +1, as ARC decides BY NAME; the
 * convenience constructor is +0 and ARC autoreleases it for us (an explicit
 * -autorelease is forbidden under ARC).
 */
+ (id)stringWithUTF8String:(const char *)utf8;
- (id)initWithUTF8String:(const char *)utf8;

/* The primitives every concrete subclass implements. */
- (const char *)UTF8String;
- (size_t)length;		/* BYTES */
- (size_t)characterCount;	/* Unicode characters */
- (unsigned short)characterAtIndex:(size_t)index;	/* by CHARACTER */

- (BOOL)isEqualToString:(NSString *)other;

@end

@interface NSOwnedString : NSString
{
	char *_bytes;		/* owned, NUL-terminated, UTF-8 */
	size_t _length;		/* bytes, excluding the terminating NUL */
}
+ (id)stringWithUTF8String:(const char *)utf8;
- (id)initWithUTF8String:(const char *)utf8;
@end

@interface NSMutableString : NSOwnedString
- (void)setString:(NSString *)other;
- (void)appendString:(NSString *)other;
- (void)appendUTF8String:(const char *)utf8;
@end

/*
 * NSConstantString — the class `@"..."` names, pinned by
 * `-fconstant-string-class=NSConstantString` in tools/musl-clang-objc64.sh.
 *
 * THE IVARS ARE THE RUNTIME'S. The compiler emits these objects and the runtime
 * fills them in, so the layout is fixed by our runtime's own Test/Test.h
 * (NEW_ABI): flags, length (UTF-16 code units), size (BYTES), hash, then a
 * POINTER to the characters — not trailing character data, which is what
 * loader.c's older `struct nsstr` comment suggests and what the first
 * experiment assumed.
 *
 * Instances are marked permanent by the runtime (class_table.c special-cases
 * this name), so they take no part in reference counting.
 */
@interface NSConstantString : NSString
{
	unsigned int _rflags;
	unsigned int _rlength;	/* UTF-16 code units */
	unsigned int _rsize;	/* bytes */
	unsigned int _rhash;
	const char * const _rstr;
}
@end

#endif /* FOUNDATION_NSSTRING_H */
