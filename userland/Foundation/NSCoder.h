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
 * THE KEYED DOORS ARE THE API. Cocoa also has the older NON-keyed ones (`-encodeObject:`,
 * `-encodeValueOfObjCType:at:`) and the byte doors; the keyed form is what `NSKeyedArchiver` is
 * built around and what every NSCoding class in the world implements, so it is the one that ships
 * here. NAMED: the non-keyed and byte doors are absent, and `-encodeBytes:length:forKey:` is the one
 * byte door that IS here.
 */

#ifndef FOUNDATION_NSCODER_H
#define FOUNDATION_NSCODER_H

#import <Foundation/NSObject.h>

@class NSString;

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
