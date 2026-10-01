/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSPropertyListSerialization — property lists, in the Cocoa API's shape.
 * docs/design/foundation-plan.md; the sharing decision is recorded there.
 *
 * THE THIN SKIN OVER THE C CORE (include/plist.h). This class is conversion and
 * nothing else: it turns a `plist_value_t` tree into Foundation objects and back,
 * and the parsing and serialising — the parts where a config format can be got
 * wrong — live in the C core that libconfig also consumes. One implementation is
 * the point: a file written by a C consumer and read here must mean the same
 * thing, and two parsers would have to be kept in step by hand.
 *
 * WHAT IS SUPPORTED: XML v1.0, which is the format the core speaks, and is also
 * the one that TYPES its values. NSPropertyListOpenStepFormat and
 * NSPropertyListBinaryFormat_v1_0 are NAMED (so calls compile and mean what they
 * say) and REJECTED rather than half-honoured — a config file read with the wrong
 * reader is worse than one that refuses to open.
 */

#ifndef FOUNDATION_NSPROPERTYLISTSERIALIZATION_H
#define FOUNDATION_NSPROPERTYLISTSERIALIZATION_H

#import <Foundation/NSObject.h>
/* The convenience forms below are CATEGORIES, and a category needs the class's
 * FULL interface — a forward declaration will not carry one. None of these three
 * headers imports this one, so the dependency runs one way. */
#import <Foundation/NSString.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSDictionary.h>
#include <stddef.h>

@class NSData;
@class NSError;

/* NULLABILITY (F6, slice 4): NONNULL by default, and the exceptions are MEASURED
 * (NSPropertyListSerialization.m) or inherited by PROPAGATION:
 *   - both serialisation entry points answer nil on failure — a parse that failed
 *     and a tree that cannot be written are exactly what the NSError out-parameter
 *     is for, and the class simply returns nil when the caller passed NULL for it;
 *   - the category conveniences inherit that: -propertyList (the receiver is the
 *     document and there is no error out-parameter, so a parse failure answers
 *     nil), -initWithContentsOfFile: and the two +...WithContentsOfFile: factories,
 *     which are `[[self alloc] initWithContentsOfFile:]`;
 *   - the format out-parameter of +propertyListWithData: is nullable because Cocoa
 *     lets a caller pass NULL for "I do not care which format it was";
 *   - the NSError out-parameters are nullable at BOTH levels, as everywhere else. */
@class NSURL;

NS_ASSUME_NONNULL_BEGIN

/* Apple's raw values, so a numeric comparison in existing code still holds. */
typedef enum {
	NSPropertyListOpenStepFormat = 1,
	NSPropertyListXMLFormat_v1_0 = 100,
	NSPropertyListBinaryFormat_v1_0 = 200
} NSPropertyListFormat;

typedef enum {
	NSPropertyListImmutable = 0,
	NSPropertyListMutableContainers = 1,
	NSPropertyListMutableContainersAndLeaves = 2
} NSPropertyListMutabilityOptions;

typedef NSUInteger NSPropertyListReadOptions;
typedef NSUInteger NSPropertyListWriteOptions;

@interface NSPropertyListSerialization : NSObject

/*
 * Parse `data` as a property list. On failure returns nil and, when errorPtr is
 * non-NULL, leaves an NSError whose description is the core's own message —
 * which names the offset and the offending text, because a config file's reader
 * should say WHERE it gave up.
 */
+ (nullable id)propertyListWithData:(NSData *)data
		   options:(NSPropertyListReadOptions)options
		    format:(NSPropertyListFormat * _Nullable)format
		     error:(NSError * _Nullable * _Nullable)errorPtr;

/* Serialise `plist` (strings, numbers, dates, data, arrays and dictionaries). */
+ (nullable NSData *)dataWithPropertyList:(id)plist
			  format:(NSPropertyListFormat)format
			 options:(NSPropertyListWriteOptions)options
			   error:(NSError * _Nullable * _Nullable)errorPtr;

/* Whether every object in the tree is one this serialiser can write, and whether
 * every integer/real/boolean/dictionary KEY can be spelled in the format. */
+ (BOOL)propertyList:(id)plist isValidForFormat:(NSPropertyListFormat)format;

@end

/*
 * THE CONVENIENCE FORMS, as CATEGORIES rather than additions to the classes.
 * Cocoa declares them this way, and it keeps NSString, NSArray and NSDictionary
 * untouched while making the selectors EXIST — which is what the audited
 * inventories check. They are implemented in the skin, because they are plist
 * behaviour and nothing else, and they sit on NSData's own file methods so the
 * atomic-write semantics stay in one place.
 *
 * -propertyList is Cocoa's odd one: the RECEIVER is the document. It has no
 * error out-parameter, so a parse failure answers nil; use
 * NSPropertyListSerialization when the reason matters.
 */

@interface NSString (NSPropertyListAdditions)
- (nullable id)propertyList;
@end

@interface NSArray<ObjectType> (NSPropertyListAdditions)
+ (nullable NSArray<ObjectType> *)arrayWithContentsOfFile:(NSString *)path;
+ (nullable NSArray<ObjectType> *)arrayWithContentsOfURL:(NSURL *)url;
- (nullable NSArray<ObjectType> *)initWithContentsOfFile:(NSString *)path;
- (nullable NSArray<ObjectType> *)initWithContentsOfURL:(NSURL *)url;
- (BOOL)writeToFile:(NSString *)path atomically:(BOOL)useAuxiliaryFile;
- (BOOL)writeToURL:(NSURL *)url atomically:(BOOL)useAuxiliaryFile;

@end

/* ⚠ §63.45'S THREE ERROR-CARRYING FORMS MOVED OUT OF THIS CATEGORY AND ONTO THE CLASS ITSELF (§63.54), because
 * THAT IS WHERE APPLE DECLARES THEM — the modern `…error:` constructors are `NSArray`'s own methods, not this
 * property-list category's — and because declaring them here produced `-Wincomplete-implementation` the moment
 * a FULL rebuild happened: this category's `@implementation` (in `NSPropertyListSerialization.m`) does not
 * define them, and they are implemented on the class in `NSArray.m`. A declaration and its body in two
 * different blocks is exactly the shape that warning exists to catch, and an incremental build had been hiding
 * it. They are in `NSArray.h` now, with their bodies. */

/* THE MUTABLE FOUR ARE DECLARED ON THE MUTABLE CLASS, WHICH IS APPLE'S OWN ARRANGEMENT: its NSArray.h
 * carries this category as well, the four there answering NSMutableArray<ObjectType> * while NSArray's
 * answer NSArray<ObjectType> *. That is what makes `[NSMutableArray arrayWithContentsOfFile:]` answer the
 * MUTABLE type; under a single `instancetype` declaration on the immutable class it was right only by
 * coincidence of instancetype's rule. THE IMPLEMENTATION IS STILL NSARRAY'S, ONCE, in
 * NSPropertyListSerialization.m: a subclass inherits it, and that inherited definition is what the
 * declared-not-implemented gate finds. */
@interface NSMutableArray<ObjectType> (NSPropertyListAdditions)
+ (nullable NSMutableArray<ObjectType> *)arrayWithContentsOfFile:(NSString *)path;
+ (nullable NSMutableArray<ObjectType> *)arrayWithContentsOfURL:(NSURL *)url;
- (nullable NSMutableArray<ObjectType> *)initWithContentsOfFile:(NSString *)path;
- (nullable NSMutableArray<ObjectType> *)initWithContentsOfURL:(NSURL *)url;
@end

@interface NSDictionary<KeyType, ObjectType> (NSPropertyListAdditions)
+ (nullable NSDictionary<KeyType, ObjectType> *)dictionaryWithContentsOfFile:(NSString *)path;
+ (nullable NSDictionary<KeyType, ObjectType> *)dictionaryWithContentsOfURL:(NSURL *)url;
- (nullable NSDictionary<KeyType, ObjectType> *)initWithContentsOfFile:(NSString *)path;
- (nullable NSDictionary<KeyType, ObjectType> *)initWithContentsOfURL:(NSURL *)url;
- (BOOL)writeToFile:(NSString *)path atomically:(BOOL)useAuxiliaryFile;
- (BOOL)writeToURL:(NSURL *)url atomically:(BOOL)useAuxiliaryFile;
@end

/* THE MUTABLE FOUR, DECLARED ON THE MUTABLE CLASS, for the same reason the array family's are: the answer
 * must be the MUTABLE type. THE IMPLEMENTATION IS NSDICTIONARY'S, ONCE, in NSPropertyListSerialization.m,
 * and a subclass inherits it. */
@interface NSMutableDictionary<KeyType, ObjectType> (NSPropertyListAdditions)
+ (nullable NSMutableDictionary<KeyType, ObjectType> *)dictionaryWithContentsOfFile:(NSString *)path;
+ (nullable NSMutableDictionary<KeyType, ObjectType> *)dictionaryWithContentsOfURL:(NSURL *)url;
- (nullable NSMutableDictionary<KeyType, ObjectType> *)initWithContentsOfFile:(NSString *)path;
- (nullable NSMutableDictionary<KeyType, ObjectType> *)initWithContentsOfURL:(NSURL *)url;
@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSPROPERTYLISTSERIALIZATION_H */
