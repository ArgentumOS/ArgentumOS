/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSKeyedArchiver / NSKeyedUnarchiver — what an NSCoding class is archived INTO. F13.12.
 *
 * THE ARCHIVE IS A PROPERTY LIST WITH COCOA'S STRUCTURE:
 *
 *   { "$version"  : 1,
 *     "$archiver" : "NSKeyedArchiver",
 *     "$objects"  : [ "$null", <entry>, ... ],      index 0 is the empty slot
 *     "$top"      : { "root" : {"$ref": 1} } }
 *
 * and an `<entry>` is either a PLAIN plist value (a string, number, date or data), an OBJECT
 * (`{"$class": {"$ref": n}, "key": value, ...}` — what `-encodeWithCoder:` filled in), or a
 * COLLECTION (`{"$class": ..., "NS.objects": [...]}` or `"NS.keys"` with `"NS.objects"`).
 *
 * ONE DEPARTURE FROM COCOA, named because it is the only one: a REFERENCE is `{"$ref": n}` rather
 * than Cocoa's `UID` property-list type, which this library's plist reader and writer cannot
 * express. Everything else is Cocoa's design — the objects table, `$null` at index 0, `$class` with
 * a `$classes` chain, `NS.objects`/`NS.keys` for collections — so the SHAPE of the archive is the
 * familiar one and only the reference's spelling differs.
 *
 * OBJECT IDENTITY IS PRESERVED WITHIN ONE ARCHIVE: an object reachable twice is written once and
 * referenced twice, and an object that refers to itself comes back referring to itself, because an
 * index is RESERVED before its contents are written. Immutable VALUE types (strings, numbers,
 * dates, data, NSNull) are written inline instead, so their identity is not part of the archive —
 * which is the same promise Cocoa makes about them.
 *
 * WHAT IS ABSENT, named: `NSSecureCoding`, class-name substitution
 * (`-setClassName:forClass:`/`-classNameForClass:`), delegates, and the non-keyed doors. An
 * archive written here is readable here; it is NOT byte-compatible with Cocoa's, because of the
 * reference spelling above.
 */

#ifndef FOUNDATION_NSKEYEDARCHIVER_H
#define FOUNDATION_NSKEYEDARCHIVER_H

#import <Foundation/NSCoder.h>

@class NSData;
@class NSArray;
@class NSMutableData;
@class NSMutableDictionary;
@class NSMutableArray;

NS_ASSUME_NONNULL_BEGIN

@interface NSKeyedArchiver : NSCoder
{
	NSMutableArray *_objects;	/* the $objects table */
	NSMutableArray *_memo;		/* FNMemo pairs: which object has which index */
	NSMutableArray *_stack;		/* the entries being filled, innermost last */
}

+ (nullable NSData *)archivedDataWithRootObject:(id)rootObject;
+ (BOOL)archiveRootObject:(id)rootObject toFile:(NSString *)path;
- (instancetype)initForWritingWithMutableData:(NSMutableData *)data;

- (void)encodeObject:(nullable id)object forKey:(NSString *)key;
- (void)encodeBool:(BOOL)value forKey:(NSString *)key;
- (void)encodeInt:(int)value forKey:(NSString *)key;
- (void)encodeInteger:(NSInteger)value forKey:(NSString *)key;
- (void)encodeDouble:(double)value forKey:(NSString *)key;
- (void)encodeFloat:(float)value forKey:(NSString *)key;
- (void)encodeBytes:(const void *)bytes length:(NSUInteger)length forKey:(NSString *)key;

- (void)finishEncoding;

@end

@interface NSKeyedUnarchiver : NSCoder
{
	NSArray *_table;		/* the $objects table, as read */
	id _top;			/* the $top dictionary, which names the root */
	NSMutableArray *_memo;		/* index -> the object built for it (NSNull = not yet built) */
	NSMutableArray *_stack;		/* the entries being read, innermost last */
	id _root;			/* the decoded top-level object, once asked for */
}

+ (nullable id)unarchiveObjectWithData:(NSData *)data;
+ (nullable id)unarchiveObjectWithFile:(NSString *)path;
- (instancetype)initForReadingWithData:(NSData *)data;

- (nullable id)decodeObjectForKey:(NSString *)key;
- (BOOL)decodeBoolForKey:(NSString *)key;
- (int)decodeIntForKey:(NSString *)key;
- (NSInteger)decodeIntegerForKey:(NSString *)key;
- (double)decodeDoubleForKey:(NSString *)key;
- (float)decodeFloatForKey:(NSString *)key;
- (nullable const void *)decodeBytesForKey:(NSString *)key
			    returnedLength:(nullable NSUInteger *)lengthp;

- (BOOL)containsValueForKey:(NSString *)key;
- (void)finishDecoding;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSKEYEDARCHIVER_H */
