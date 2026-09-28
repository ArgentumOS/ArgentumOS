/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSCoder — the abstract base of the archiving family. F13.12.
 *
 * IT IS ABSTRACT IN THE ONLY WAY A CODER CAN BE: every keyed door RAISES in this class, because a
 * coder that cannot encode has no sensible answer to give. `NSKeyedArchiver` and
 * `NSKeyedUnarchiver` are where the doors mean something.
 *
 * AND THE OLDER, SEQUENTIAL DOORS ARE HERE TOO — WHICH IS A CORRECTION OF THIS FILE'S FIRST NOTE
 * (2026-09-26, §62.86). That note said the non-keyed doors were ABSENT, and it was right about this
 * header and wrong about the library: the classic `NSArchiver`/`NSUnarchiver` pair is built on them, so
 * they are declared here and implemented by that pair. The two families are now both present and
 * neither is the other's fallback: **`NSKeyedArchiver`/`NSKeyedUnarchiver` answer the KEYED doors and
 * raise in the SEQUENTIAL ones; `NSArchiver`/`NSUnarchiver` answer the SEQUENTIAL doors and raise in the
 * KEYED ones.** Each family's abstract doors raise `NSInvalidArgumentException` and name the family that
 * answers them, which is what makes a wrong-door call say what to do instead.
 */

#ifndef FOUNDATION_NSCODER_H
#define FOUNDATION_NSCODER_H

#import <Foundation/NSObject.h>

@class NSString;
@class NSData;

NS_ASSUME_NONNULL_BEGIN

@interface NSCoder : NSObject

/* Objects — nil is a VALUE here, not an absence: an archive records "there was nothing". */
- (void)encodeObject:(nullable id)object forKey:(NSString *)key;
- (nullable id)decodeObjectForKey:(NSString *)key;

/* The scalars, each with its reading door. A key that was never written is a programming error on
 * the reading side, so these raise rather than answering a zero that looks like data. */
- (void)encodeBool:(BOOL)value forKey:(NSString *)key;
- (BOOL)decodeBoolForKey:(NSString *)key;
- (void)encodeInt:(int)value forKey:(NSString *)key;
- (int)decodeIntForKey:(NSString *)key;
- (void)encodeInteger:(NSInteger)value forKey:(NSString *)key;
- (NSInteger)decodeIntegerForKey:(NSString *)key;
- (void)encodeDouble:(double)value forKey:(NSString *)key;
- (double)decodeDoubleForKey:(NSString *)key;
- (void)encodeFloat:(float)value forKey:(NSString *)key;
- (float)decodeFloatForKey:(NSString *)key;

- (void)encodeBytes:(const void *)bytes length:(NSUInteger)length forKey:(NSString *)key;
- (nullable const void *)decodeBytesForKey:(NSString *)key
			    returnedLength:(nullable NSUInteger *)lengthp;

- (BOOL)containsValueForKey:(NSString *)key;

/* --- THE SEQUENTIAL DOORS: ORDER AND TYPE ARE THE PROTOCOL (§62.86) --------------------------------
 *
 * A value written with `-encodeValueOfObjCType:at:` must be READ with the same type code, in the same
 * order — there are no names to look up and no coercion, which is the whole difference from the keyed
 * doors above. `NSArchiver`/`NSUnarchiver` answer these; the keyed pair raises here. */
- (void)encodeValueOfObjCType:(const char *)valueType at:(const void *)address;
- (void)decodeValueOfObjCType:(const char *)valueType at:(void *)data;

/* The TYPE-less object doors, in order: the sequential counterpart of `-encodeObject:forKey:`. */
- (void)encodeObject:(nullable id)object;
- (nullable id)decodeObject;

/* The bytes doors the sequential format needs (the keyed one uses `-encodeBytes:length:forKey:`). */
- (void)encodeDataObject:(NSData *)data;
- (nullable NSData *)decodeDataObject;
- (void)encodeBytes:(nullable const void *)bytesp length:(NSUInteger)length;
- (nullable const void *)decodeBytesWithReturnedLength:(NSUInteger *)lengthp;

/* THE VERSION DOOR IS THE ONE SEQUENTIAL DOOR THIS LIBRARY DOES NOT ANSWER, and the ground is in the
 * wire: our stream records no class versions (Apple's classic stream did), so `NSUnarchiver` raises
 * rather than answering a number that was never written. Declared because Apple declares it on the
 * base. */
- (NSInteger)versionForClassName:(NSString *)className;

@end

/* How a decoder answers a failure it cannot report as a value (2026-09-20): raise,
 * or hand the caller an NSError. Names from Apple's documentation index; values are
 * ours — §11.6.1 D2, see the note in NSFileManager.h. */
typedef enum {
	NSDecodingFailurePolicyRaiseException = 0,
	NSDecodingFailurePolicySetErrorAndReturn = 1
} NSDecodingFailurePolicy;

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSCODER_H */
