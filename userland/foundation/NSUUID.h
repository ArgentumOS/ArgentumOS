/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUUID.h — a 128-bit identifier (W2h, docs/design/foundation-plan.md §14).
 *
 * A VALUE, and the only thing that makes one useful is that it is UNIQUE: `+UUID` takes its bytes
 * from the kernel's entropy source rather than from a clock or a counter, which is the difference
 * between an identifier and a coincidence. Two bits inside the 128 are not random at all — the
 * version and the variant — and the class sets them as a RULE, which is why a UUID's string form
 * always has a `4` leading its third group and a `8`/`9`/`a`/`b` leading its fourth.
 *
 * WHAT IS NOT, named: `NSSecureCoding` conformance (the protocol is not in this library yet, and
 * this class does not pretend to it), and `-copy` returning a new object — a UUID is immutable, so
 * the copy IS the receiver, which is what Cocoa's own `-copyWithZone:` does.
 */
#ifndef FOUNDATION_NSUUID_H
#define FOUNDATION_NSUUID_H

#import <foundation/NSObject.h>
#import <foundation/NSString.h>

NS_ASSUME_NONNULL_BEGIN

@interface NSUUID : NSObject <NSCopying>
{
	unsigned char _bytes[16];	/* in the order the string form prints them */
}

+ (instancetype)UUID;

/* NOT nullable, because the bytes cannot be wrong: the parameter is a fixed-size array, so a
 * shorter buffer is a compile error rather than a runtime doubt. */
- (instancetype)initWithUUIDBytes:(const unsigned char [_Nonnull 16])bytes;

/* NULLABLE, and that is Apple's contract too: a malformed string answers nil rather than a
 * half-built identifier. */
- (nullable instancetype)initWithUUIDString:(NSString *)string;

- (void)getUUIDBytes:(unsigned char [_Nonnull 16])uuid;
- (NSString *)UUIDString;
- (NSString *)description;

@end

NS_ASSUME_NONNULL_END
#endif /* FOUNDATION_NSUUID_H */
