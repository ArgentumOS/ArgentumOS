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
 *
 * THE THREE CONVENTIONS THAT ARE NOT A FAMILY'S OWN. `-encodeBycopyObject:`, `-encodeByrefObject:` and
 * `-encodeConditionalObject:` are classic-archiver ANNOTATIONS whose whole contract is "equivalent to
 * `-encodeObject:` on whatever coder you sent them to" (Apple says so in words). They are implemented
 * ONCE, on this base, as exactly that equivalence — so they are correct for both families at the same
 * time: to a keyed coder they reach its `-encodeObject:`, which raises (rightly — the keyed family has
 * no such doors), and to a sequential coder they reach the real one. Implementing them here rather than
 * in `NSArchiver` is the reason `NSArchiver` needs no edit to gain them.
 */

#ifndef FOUNDATION_NSCODER_H
#define FOUNDATION_NSCODER_H

#include <stdint.h>

#import <Foundation/NSObject.h>
/* THE GEOMETRY TYPES ARE THIS HEADER'S OWN, because the keyed geometry doors below NAME them: NSGeometry.h
 * brings NSPoint/NSSize/NSRect AND the CG value types (CGPoint/CGSize/CGRect/CGVector), and the transform
 * has its own CoreGraphics header. Imported ABOVE the nullability region, which is where every other
 * `#import` in this tree lives — a `#import` inside `NS_ASSUME_NONNULL_BEGIN` is refused by the compiler. */
#import <Foundation/NSGeometry.h>
#import <CoreGraphics/CGAffineTransform.h>

@class NSString;
@class NSData;
@class NSArray;
@class NSDictionary;
@class NSSet;
@class NSError;

NS_ASSUME_NONNULL_BEGIN

/* How a decoder answers a failure it cannot report as a value (2026-09-20): raise,
 * or hand the caller an NSError. Names from Apple's documentation index; values are
 * ours — §11.6.1 D2, see the note in NSFileManager.h.
 *
 * MOVED ABOVE THE INTERFACE by the type-checked-doors work (2026): `-decodingFailurePolicy` is a
 * property OF THIS TYPE, and the declaration could not name a type defined below it. */
typedef enum {
	NSDecodingFailurePolicyRaiseException = 0,
	NSDecodingFailurePolicySetErrorAndReturn = 1
} NSDecodingFailurePolicy;

@interface NSCoder : NSObject
{
@protected
	/* THE DECODE-ERROR STATE, on the base because the doors that read it (`-error`,
	 * `-decodingFailurePolicy`) are the base's own and both families answer them. Zero-initialised to
	 * the DEFAULTS the getters promise: raise-on-failure, no allow-list, no error recorded. */
	NSDecodingFailurePolicy _decodingFailurePolicy;
	BOOL _requiresSecureCoding;
	NSSet *_allowedClasses;	/* retained; nil means "no list given" */
	NSError *_error;	/* retained; the last failure, under SetErrorAndReturn */
}

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

/* THE FIXED-WIDTH INTEGER PAIR, WHICH `-encodeInt:` IS NOT. A 32-bit value written as 32 bits and read
 * back as 64 (or the reverse) is a silent coercion, and these doors are how a caller says "the width
 * is part of the data". The width travels as the `NSNumber`'s own type, so the reader keeps it. */
- (void)encodeInt32:(int32_t)value forKey:(NSString *)key;
- (int32_t)decodeInt32ForKey:(NSString *)key;
- (void)encodeInt64:(int64_t)value forKey:(NSString *)key;
- (int64_t)decodeInt64ForKey:(NSString *)key;

- (void)encodeBytes:(const void *)bytes length:(NSUInteger)length forKey:(NSString *)key;
- (nullable const void *)decodeBytesForKey:(NSString *)key
			    returnedLength:(nullable NSUInteger *)lengthp;

/* THE SAME READING, WITH A FLOOR UNDER THE LENGTH: the byte run must be at least this long, and the
 * door answers the pointer alone (the caller knows the length it asked for). A run that is present but
 * SHORT is a corrupt archive, not a shorter value, so it raises. */
- (nullable const void *)decodeBytesForKey:(NSString *)key minimumLength:(NSUInteger)minimumLength;

/* A PROPERTY LIST, BY NAME. Apple spells this door for the values its own plist serialiser can carry,
 * which is the reader's way of saying "this key held a plist, not an object". */
- (nullable id)decodePropertyListForKey:(NSString *)key;

/* --- THE KEYED GEOMETRY DOORS: A C STRUCT BOXED AS AN `NSValue` ------------------------------------
 *
 * A keyed archive carries objects, not C structures, so each door here writes its structure WRAPPED IN AN
 * `NSValue` under the key and reads the box back out — the spelling Apple's own keyed archive uses, and
 * the reason these are KEYED doors: the value travels BY NAME like every other keyed value. The CG
 * spellings and the Foundation spellings are both present because this tree's `NSPoint`/`NSSize`/`NSRect`
 * ARE the CG types (NSGeometry.h typedefs them), so `-encodePoint:forKey:` and `-encodeCGPoint:forKey:`
 * box the same bytes under `@encode(CGPoint)`. This is why they are implementable while the type-checked
 * tree has no `UIEdgeInsets`/`CMTime`/`SCN` substrate: the boxes they need exist (`NSValue`'s geometry
 * value-with doors and value readers landed with NSValue itself).
 *
 * THE UNKEYED COUNTERPARTS (`-encodePoint:`/`-decodePoint`, `-encodeSize:`/`-decodeSize`,
 * `-encodeRect:`/`-decodeRect`, `-encodeValueOfObjCType:at:`/`-decodeValueOfObjCType:at:size:`,
 * `-encodeArrayOfObjCType:count:at:`/`-decodeArrayOfObjCType:count:at:`) BELONG TO THE OTHER FAMILY
 * (the sequential one), and they are declared below: Apple's own note is that they "invoke
 * -encodeValueOfObjCType:at: and must be matched by a -decodePoint in order", which is the sequential
 * contract. Each one is a BASE implementation expressed over the type-code door, so the keyed family
 * reaches its abstract `-encodeValueOfObjCType:at:` and raises the sequential refusal, exactly as the
 * two families do everywhere else.
 *
 * ⚠ THE SENTENCE THIS PARAGRAPH USED TO CARRY WAS A REASON, NOT A FACT (§63.43, 2026-10-01): it said
 * the six geometry doors were "NOT declared here" because "this library's sequential wire has no struct
 * spelling at all". THE SECOND HALF WAS TRUE AND THE CONCLUSION DID NOT FOLLOW — a wire that cannot
 * spell a struct is a wire to EXTEND, not a door to omit, and the doors were owed the whole time. The
 * wire now carries `{…}`, `(…)` and `[…]` (FNArchiverWire.h), so the six are real sequential doors. */
- (void)encodePoint:(NSPoint)point forKey:(NSString *)key;
- (NSPoint)decodePointForKey:(NSString *)key;
- (void)encodeSize:(NSSize)size forKey:(NSString *)key;
- (NSSize)decodeSizeForKey:(NSString *)key;
- (void)encodeRect:(NSRect)rect forKey:(NSString *)key;
- (NSRect)decodeRectForKey:(NSString *)key;

- (BOOL)containsValueForKey:(NSString *)key;

/* --- THE TYPE-CHECKED OBJECT DOORS: `NSSecureCoding`'s READING HALF ---------------------------------
 *
 * `-decodeObjectForKey:` answers whatever the archive NAMED, and an archive names its classes as
 * STRINGS — which makes "read data somebody else supplied" the classic object-injection door. These
 * doors answer an object only when it IS one of the classes the caller names, and report the refusal as
 * the decoder's failure (`-failWithError:`). They are the point of `NSSecureCoding`, and they are why
 * `NSCoding.h` could stop saying the enforcement was missing. The KEYED pair implements them; the
 * abstract base raises, like every other keyed door. */
- (nullable id)decodeObjectOfClass:(Class)aClass forKey:(NSString *)key;
- (nullable id)decodeObjectOfClasses:(nullable NSSet *)classes forKey:(NSString *)key;
- (nullable NSArray *)decodeArrayOfObjectsOfClass:(Class)cls forKey:(NSString *)key;
- (nullable NSArray *)decodeArrayOfObjectsOfClasses:(nullable NSSet *)classes forKey:(NSString *)key;
- (nullable NSDictionary *)decodeDictionaryWithKeysOfClass:(Class)keyClass
					    objectsOfClass:(Class)objectClass
						    forKey:(NSString *)key;
- (nullable NSDictionary *)decodeDictionaryWithKeysOfClasses:(nullable NSSet *)keyClasses
					     objectsOfClasses:(nullable NSSet *)objectClasses
						      forKey:(NSString *)key;

/* THE CONDITIONAL KEYED DOOR: it writes the reference only when the object is ALREADY in the archive,
 * so a class may point at something (a delegate, an owner) without forcing it in. A nil object is the
 * ordinary nil; an object not yet written writes nothing, so the graph is not dragged in by it. */
- (void)encodeConditionalObject:(nullable id)object forKey:(NSString *)key;

/* --- DECODING A TOP-LEVEL OBJECT, WITH THE FAILURE AS A VALUE --------------------------------------
 *
 * The doors a caller who reads SOMEONE ELSE'S archive uses: they answer nil and fill in an NSError
 * instead of raising, so a corrupt or hostile archive is a VALUE to inspect rather than an exception to
 * catch. The `-decodeObjectForKey:` family keeps its raising contract; these are the doors where "no"
 * is data. `-decodeTopLevelObjectAndReturnError:` is the root-object spelling of the same idea. */
- (nullable id)decodeTopLevelObjectAndReturnError:(NSError * _Nullable * _Nullable)error;
- (nullable id)decodeTopLevelObjectForKey:(NSString *)key
				    error:(NSError * _Nullable * _Nullable)error;
- (nullable id)decodeTopLevelObjectOfClass:(Class)cls
				    forKey:(NSString *)key
				     error:(NSError * _Nullable * _Nullable)error;
- (nullable id)decodeTopLevelObjectOfClasses:(nullable NSSet *)classes
				      forKey:(NSString *)key
				       error:(NSError * _Nullable * _Nullable)error;

/* --- THE SEQUENTIAL OBJECT-CONVENTIONS, AT THE BASE (see the file note) ----------------------------- */
- (void)encodeBycopyObject:(nullable id)object;
- (void)encodeByrefObject:(nullable id)object;
- (void)encodeConditionalObject:(nullable id)object;

/* --- THE DECODE-ERROR AND INSPECTION SURFACE -------------------------------------------------------
 *
 * `-failWithError:` is how a decoder reports a failure it cannot return as a value; what it DOES is
 * `-decodingFailurePolicy`'s business — raise, or record the error for `-error`. The base answers the
 * DEFAULTS and stores them; the keyed unarchiver is where the policy is honoured. */
- (void)failWithError:(NSError *)error;

@property (readonly) BOOL allowsKeyedCoding;
@property BOOL requiresSecureCoding;
@property (copy, nullable) NSSet *allowedClasses;
@property (readonly, copy, nullable) NSError *error;
@property NSDecodingFailurePolicy decodingFailurePolicy;

/* The system version the archive was written on. This library records none, so the base answers nil —
 * the honest form of "unknown", which is not the same as a version. Apple publishes the door and not a
 * value, so there is nothing to match (§11.6.1 D2). */
- (nullable NSString *)systemVersion;

/* --- THE SEQUENTIAL DOORS: ORDER AND TYPE ARE THE PROTOCOL (§62.86) --------------------------------
 *
 * A value written with `-encodeValueOfObjCType:at:` must be READ with the same type code, in the same
 * order — there are no names to look up and no coercion, which is the whole difference from the keyed
 * doors above. `NSArchiver`/`NSUnarchiver` answer these; the keyed pair raises here.
 *
 * ⚠ AND THERE ARE TWO SPELLINGS OF THE READING DOOR, WHICH IS APPLE'S OWN ARRANGEMENT (§63.43):
 * `-decodeValueOfObjCType:at:size:` is the MODERN one — it tells the reader how big the caller's buffer
 * is — and Apple's header marks `-decodeValueOfObjCType:at:` `API_DEPRECATED_WITH_REPLACEMENT` for it,
 * "unsafe because it could potentially cause buffer overruns". Here the un-sized door is a BASE
 * implementation that SIZES THE TYPE CODE ITSELF (`NSGetSizeAndAlignment`) and funnels into the sized
 * one — which is exactly the shape Apple deprecates, with the overrun turned into a named refusal. */
- (void)encodeValueOfObjCType:(const char *)valueType at:(const void *)address;
- (void)decodeValueOfObjCType:(const char *)valueType at:(void *)data size:(NSUInteger)size;
- (void)decodeValueOfObjCType:(const char *)valueType at:(void *)data;

/* THE ARRAY DOORS, and they are BASE implementations for a stated reason: Apple's own documentation says
 * "NSCoder's implementation invokes -encodeValueOfObjCType:at: to encode the entire array of items" and
 * "subclasses that implement the -encodeValueOfObjCType:at: method do not need to override this method".
 * So they are a loop over the type-code door, `NSGetSizeAndAlignment`'s size being the stride an array of
 * the type has in memory — which is what makes `-encodeValuesOfObjCTypes:` above and these two the same
 * rule applied to a buffer instead of to arguments. */
- (void)encodeArrayOfObjCType:(const char *)type count:(NSUInteger)count at:(const void *)array;
- (void)decodeArrayOfObjCType:(const char *)itemType count:(NSUInteger)count at:(void *)array;

/* THE UNKEYED GEOMETRY DOORS (see the note above the keyed ones): the type IS the encoding, and each of
 * the six is the type-code door applied to its struct — `@encode(NSPoint)` is `"{CGPoint=dd}"` here
 * because NSGeometry.h makes NSPoint a typedef of the CG type. */
- (void)encodePoint:(NSPoint)point;
- (NSPoint)decodePoint;
- (void)encodeSize:(NSSize)size;
- (NSSize)decodeSize;
- (void)encodeRect:(NSRect)rect;
- (NSRect)decodeRect;

/* The TYPE-less object doors, in order: the sequential counterpart of `-encodeObject:forKey:`. */
- (void)encodeObject:(nullable id)object;
- (nullable id)decodeObject;

/* The bytes doors the sequential format needs (the keyed one uses `-encodeBytes:length:forKey:`). */
- (void)encodeDataObject:(NSData *)data;
- (nullable NSData *)decodeDataObject;
- (void)encodeBytes:(nullable const void *)bytesp length:(NSUInteger)length;
- (nullable const void *)decodeBytesWithReturnedLength:(NSUInteger *)lengthp;
/* THE SEQUENTIAL TWIN OF `-decodeBytesForKey:minimumLength:` — Apple documents both, one per family —
 * and it is the ONE door here whose refusal goes through `-failWithError:` rather than raising directly,
 * which is what Apple's own text says: "If the result exists, but is of insufficient length, then the
 * decoder uses -failWithError: to fail the entire decode operation. The result of that is configurable on
 * a per-NSCoder basis using -decodingFailurePolicy." A nil answer means the same as it does everywhere
 * else here: under SetErrorAndReturn the failure is a VALUE, and `-error` carries it. */
- (nullable const void *)decodeBytesWithMinimumLength:(NSUInteger)minimumLength;

/* --- THE LEGACY SEQUENTIAL PLIST AND BULK DOORS -----------------------------------------------------
 *
 * `-encodePropertyList:`/`-decodePropertyList` are the OLD name for "this value is a property list", and a
 * property list IS an object graph of strings, numbers, data and the collections — so they are implemented
 * ONCE here as exactly that equivalence (Apple's own contract), which is what makes them correct for BOTH
 * families at the same time: to a sequential coder they reach the real `-encodeObject:`/`-decodeObject`,
 * and to a keyed coder they reach the keyed family's no-key `-encodeObject:`, which raises, as every
 * sequential door does there.
 *
 * `-encodeValuesOfObjCTypes:`/`-decodeValuesOfObjCTypes:` write and read a RUN of values whose type codes
 * are concatenated into ONE string — the caller's adjacent string literals `@encode(int) @encode(double)`
 * spell `"id"` — so each code names one argument and every argument is the ADDRESS of its value. The door
 * is therefore exactly `-encodeValueOfObjCType:at:` applied left to right, with `NSGetSizeAndAlignment` (the
 * type-code reader NSValue uses) stepping from one code to the next; a code this library cannot spell
 * raises in the underlying door rather than being silently skipped. */
- (void)encodePropertyList:(nullable id)aPropertyList;
- (nullable id)decodePropertyList;
- (void)encodeValuesOfObjCTypes:(const char *)types, ...;
- (void)decodeValuesOfObjCTypes:(const char *)types, ...;

/* THE VERSION DOOR IS THE ONE SEQUENTIAL DOOR THIS LIBRARY DOES NOT ANSWER, and the ground is in the
 * wire: our stream records no class versions (Apple's classic stream did), so `NSUnarchiver` raises
 * rather than answering a number that was never written. Declared because Apple declares it on the
 * base. */
- (NSInteger)versionForClassName:(NSString *)className;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSCODER_H */
