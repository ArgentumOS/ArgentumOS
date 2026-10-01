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
 * THE COLLECTIONS THE CODEC ITSELF KNOWS are the arrays, the dictionaries and the SETS — `NSSet`,
 * `NSMutableSet` and `NSCountedSet`, plus `NSOrderedSet`/`NSMutableOrderedSet`, whose class entry
 * names the public class because `-classForCoder` answers it. A set's members ride under `NS.objects`,
 * the same key an array uses, because the members ARE what a set is — and for an ORDERED set that same
 * sequence IS its order, so there is nothing extra to write. A COUNTED set is the one exception, and it
 * is a second payload rather than a second shape: `-allObjects` and `-count` describe it by DISTINCT
 * members (adding `"x"` twice is ONE member), so the multiplicities are written beside them under
 * `"NS.counts"`, index-aligned with `"NS.objects"`. That key is OURS — Apple publishes no name for
 * it, so there is no value to match (§11.6.1 D2's ground) — and writing the members alone would
 * silently lose the one thing the class exists to carry.
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
 * WHAT IS ABSENT, named: class-name substitution (`-setClassName:forClass:`/`-classNameForClass:`) and the
 * non-keyed doors. An archive written here is readable here; it is NOT byte-compatible with Cocoa's, because
 * of the reference spelling above.
 *
 * DELEGATES ARRIVED IN W9, and two things about them are worth knowing here: they are asked before they are
 * assumed (every call is guarded by `-respondsToSelector:`, because every member is optional), and they are
 * NOT RETAINED — the archiver holds its delegate weakly, as `assign`, so a delegate that owns the archiver
 * does not keep it alive and an archiver does not resurrect a delegate that has gone away.
 */

#ifndef FOUNDATION_NSKEYEDARCHIVER_H
#define FOUNDATION_NSKEYEDARCHIVER_H

#include <stdint.h>

#import <Foundation/NSCoder.h>
#import <Foundation/NSKeyedArchiverDelegate.h>
#import <Foundation/NSKeyedUnarchiverDelegate.h>

@class NSData;
@class NSArray;
@class NSMutableData;
@class NSMutableDictionary;
@class NSMutableArray;

NS_ASSUME_NONNULL_BEGIN

/* THE KEY THE ROOT OBJECT IS STORED UNDER (§62.103), for a caller that reads an archive's own plist. */
extern NSString * const NSKeyedArchiveRootObjectKey;

@interface NSKeyedArchiver : NSCoder
{
	NSMutableArray *_objects;	/* the $objects table */
	NSMutableArray *_memo;		/* FNMemo pairs: which object has which index */
	NSMutableArray *_stack;		/* the entries being filled, innermost last */
	id <NSKeyedArchiverDelegate> _delegate;	/* NOT retained: see the note above */
	NSMutableData *_data;		/* the caller's buffer, which -finishEncoding fills */
	NSMutableDictionary *_top;	/* the $top keys: what was encoded OUTSIDE -encodeWithCoder: */
}

+ (nullable NSData *)archivedDataWithRootObject:(id)rootObject;
+ (BOOL)archiveRootObject:(id)rootObject toFile:(NSString *)path;

/* COCOA'S INSTANCE FLOW, IMPLEMENTED: the archive is written into `data` by `-finishEncoding`. The keys
 * encoded OUTSIDE `-encodeWithCoder:` — which is to say, between this initialiser and the finish — are the
 * archive's TOP-LEVEL ones, and `+archivedDataWithRootObject:` names its root `"root"` the same way. Before
 * this existed the initialiser accepted its buffer and ignored it, which was a registered deviation; it is
 * implemented rather than registered because nothing about the format made it necessary. */
- (instancetype)initForWritingWithMutableData:(NSMutableData *)data;

/* The delegate, held weakly. */
- (nullable id <NSKeyedArchiverDelegate>)delegate;
- (void)setDelegate:(nullable id <NSKeyedArchiverDelegate>)delegate;

- (void)encodeObject:(nullable id)object forKey:(NSString *)key;
- (void)encodeBool:(BOOL)value forKey:(NSString *)key;
- (void)encodeInt:(int)value forKey:(NSString *)key;
- (void)encodeInteger:(NSInteger)value forKey:(NSString *)key;
- (void)encodeDouble:(double)value forKey:(NSString *)key;
- (void)encodeFloat:(float)value forKey:(NSString *)key;
- (void)encodeBytes:(const void *)bytes length:(NSUInteger)length forKey:(NSString *)key;
/* The fixed-width pair, and the conditional door (which writes a reference only when the object is
 * already in the archive — see NSCoder.h). */
- (void)encodeInt32:(int32_t)value forKey:(NSString *)key;
- (void)encodeInt64:(int64_t)value forKey:(NSString *)key;
- (void)encodeConditionalObject:(nullable id)object forKey:(NSString *)key;

/* ⚠ THE `CG`-SPELLED KEYED GEOMETRY DOORS ARE NOT DECLARED HERE ANY MORE (§63.54) — they are the CoreGraphics
 * tier's, in `userland/CoreGraphics/NSCoderCGGeometry.h` as a category on `NSCoder`, which every coder class
 * inherits. Leaving the declarations here turned them into `-Wincomplete-implementation` the moment their
 * implementations moved, which is how this was found. The `NS`-spelled pair (`-encodePoint:forKey:`) stays: it
 * is Foundation's, and this class is where Apple declares it too. */
- (void)encodePoint:(NSPoint)point forKey:(NSString *)key;
- (void)encodeSize:(NSSize)size forKey:(NSString *)key;
- (void)encodeRect:(NSRect)rect forKey:(NSString *)key;

- (void)finishEncoding;

@end

@interface NSKeyedUnarchiver : NSCoder
{
	NSArray *_table;		/* the $objects table, as read */
	id _top;			/* the $top dictionary, which names the root */
	NSMutableArray *_memo;		/* index -> the object built for it (NSNull = not yet built) */
	NSMutableArray *_stack;		/* the entries being read, innermost last */
	id _root;			/* the decoded top-level object, once asked for */
	id <NSKeyedUnarchiverDelegate> _delegate;	/* NOT retained: see the note above */
}

+ (nullable id)unarchiveObjectWithData:(NSData *)data;
+ (nullable id)unarchiveObjectWithFile:(NSString *)path;
- (instancetype)initForReadingWithData:(NSData *)data;

/* The delegate, held weakly. The class methods above have no delegate to offer, so a caller who needs one
 * builds the unarchiver and asks it directly. */
- (nullable id <NSKeyedUnarchiverDelegate>)delegate;
- (void)setDelegate:(nullable id <NSKeyedUnarchiverDelegate>)delegate;

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

/* THE READER'S HALF OF THE TYPE-CHECKED AND ERROR-REPORTING DOORS (NSCoder.h declares them on the base
 * and says why). `-decodeInt32ForKey:`/`-decodeInt64ForKey:` keep the width; the `-decodeObjectOfClass:`
 * family gates by class; the `-decodeTopLevelObject...error:` family answers an NSError instead of
 * raising. */
- (int32_t)decodeInt32ForKey:(NSString *)key;
- (int64_t)decodeInt64ForKey:(NSString *)key;
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
- (nullable const void *)decodeBytesForKey:(NSString *)key minimumLength:(NSUInteger)minimumLength;
- (nullable id)decodePropertyListForKey:(NSString *)key;
/* ⚠ THE `CG`-SPELLED READING HALF IS NOT DECLARED HERE ANY MORE EITHER (§63.54), for the same reason as the
 * writing half above: it is the CoreGraphics tier's. The `NS`-spelled readers stay. */
- (NSPoint)decodePointForKey:(NSString *)key;
- (NSSize)decodeSizeForKey:(NSString *)key;
- (NSRect)decodeRectForKey:(NSString *)key;
- (nullable id)decodeTopLevelObjectAndReturnError:(NSError * _Nullable * _Nullable)error;
- (nullable id)decodeTopLevelObjectForKey:(NSString *)key
				    error:(NSError * _Nullable * _Nullable)error;
- (nullable id)decodeTopLevelObjectOfClass:(Class)cls
				    forKey:(NSString *)key
				     error:(NSError * _Nullable * _Nullable)error;
- (nullable id)decodeTopLevelObjectOfClasses:(nullable NSSet *)classes
				      forKey:(NSString *)key
				       error:(NSError * _Nullable * _Nullable)error;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSKEYEDARCHIVER_H */
