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

#import <foundation/NSObject.h>
#include <stddef.h>

@class NSData;
@class NSError;
@class NSString;
@class NSArray;
@class NSDictionary;

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
+ (id)propertyListWithData:(NSData *)data
		   options:(NSPropertyListReadOptions)options
		    format:(NSPropertyListFormat *)format
		     error:(NSError **)errorPtr;

/* Serialise `plist` (strings, numbers, dates, data, arrays and dictionaries). */
+ (NSData *)dataWithPropertyList:(id)plist
			  format:(NSPropertyListFormat)format
			 options:(NSPropertyListWriteOptions)options
			   error:(NSError **)errorPtr;

/* Whether every object in the tree is one this serialiser can write, and whether
 * every integer/real/boolean/dictionary KEY can be spelled in the format. */
+ (BOOL)propertyList:(id)plist isValidForFormat:(NSPropertyListFormat)format;

@end

#endif /* FOUNDATION_NSPROPERTYLISTSERIALIZATION_H */
