/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSJSONSerialization.h — JSON to Foundation objects and back (W2h, §14).
 *
 * THE RULES ARE APPLE'S AND THEY ARE THE CONTRACT: a top-level ARRAY or DICTIONARY (unless the
 * fragments option says otherwise), leaves from NSString/NSNumber/NSArray/NSDictionary/NSNull, keys
 * that are all NSString, and no NaN or infinity. `+isValidJSONObject:` answers exactly that question,
 * and it exists because **`+dataWithJSONObject:options:error:` THROWS on an invalid object** rather
 * than filling in the error - Apple's own caveat, and the reason a caller checks first.
 *
 * THE OPTION VALUES ARE APPLE'S, and unlike most constants in this library those ARE published -
 * Apple's own `NSJSONSerialization.h` gives every one of them: mutableContainers 1<<0, mutableLeaves
 * 1<<1, fragmentsAllowed 1<<2, json5Allowed 1<<3, topLevelDictionaryAssumed 1<<4; prettyPrinted 1<<0,
 * sortedKeys 1<<1.
 *
 * THE READING SIDE OF THAT LIST SHIPPED ON 2026-09-26 (§62.93) AND THIS PARAGRAPH IS WHAT IT REPLACED.
 * It used to say that `json5Allowed`, `topLevelDictionaryAssumed` and the writing-side
 * `withoutEscapingSlashes` were REFUSED, "because a value invented for them would be a difference a
 * program could see", and that the deprecated `allowFragments` name was gone because §11.5's second
 * exclusion kept deprecated API out. BOTH HALVES OF THAT WERE WRONG, and each was wrong for its own
 * reason:
 *
 *   - THE VALUES ARE NOT INVENTED. Apple publishes all five, so a name carrying Apple's own bit is a
 *     fidelity gain rather than a risk, and refusing it was refusing a spelling rather than a
 *     behaviour.
 *   - THE DEPRECATION GROUND WAS RETIRED (foundation-plan.md row D7, §62.24, 2026-09-26): deprecated
 *     API is IN SCOPE now, because this library's whole purpose is to run programs that were written
 *     for the old names. So `allowFragments` is declared beside the modern spelling, as Apple does,
 *     and it carries the same bit.
 *
 * BOTH BEHAVIOURS BEHIND THE NEW BITS ARE IMPLEMENTED, which is the only thing that makes declaring
 * them honest: `json5Allowed` reads JSON5 (comments, two quote characters, its own escapes and
 * numbers, unquoted keys, a trailing comma) and `topLevelDictionaryAssumed` reads a document with no
 * enclosing braces as an object body. Two boundaries are STATED in the parser and asserted by the
 * probe rather than hidden: an UNQUOTED KEY is ASCII plus \u escapes (this library has no ES5
 * identifier table, and guessing one would be inventing a grammar), and `topLevelDictionaryAssumed`
 * only ADDS the brace-less form - a document that begins with `{` or `[` parses exactly as it did.
 *
 * AND THE STRICT PATH WAS TIGHTENED IN THE SAME UNIT, because it had to be: it accepted a missing
 * comma (`[1 2]`), a trailing comma and "+1", so the two grammars were not distinguishable and
 * JSON5's rules could not have been observed at all. Those inputs now fail, which is what Apple's
 * parser does.
 *
 * WHAT IS STILL NOT, named: the two WRITING options (`writingFragmentsAllowed` and
 * `withoutEscapingSlashes`, whose second is a change to the default output rather than an addition),
 * and the two STREAM forms (`+writeJSONObject:toStream:options:error:` and
 * `+JSONObjectWithStream:options:error:`), which need NSStream - a class this library does not have.
 * The data forms are the whole surface a program without streams can use.
 */
#ifndef FOUNDATION_NSJSONSERIALIZATION_H
#define FOUNDATION_NSJSONSERIALIZATION_H

#import <Foundation/NSObject.h>
#import <Foundation/NSData.h>
#import <Foundation/NSError.h>

NS_ASSUME_NONNULL_BEGIN

typedef unsigned long NSJSONReadingOptions;
typedef unsigned long NSJSONWritingOptions;

enum {
	NSJSONReadingMutableContainers = (1UL << 0),
	NSJSONReadingMutableLeaves = (1UL << 1),
	NSJSONReadingFragmentsAllowed = (1UL << 2),
	/* JSON5 IS A SUPERSET OF JSON, so this flag turns deviations ON rather than switching readers. */
	NSJSONReadingJSON5Allowed = (1UL << 3),
	/* A DOCUMENT WITH NO ENCLOSING BRACES IS AN OBJECT BODY (see the parser's note on the boundary:
	 * the flag only ADDS that form). */
	NSJSONReadingTopLevelDictionaryAssumed = (1UL << 4),

	/* APPLE'S DEPRECATED SPELLING OF fragmentsAllowed, declared beside it as Apple declares it,
	 * carrying the same bit - the modern name and the old one are the same behaviour, and a program
	 * written against either one gets the same answer. Apple marks it
	 * API_DEPRECATED_WITH_REPLACEMENT; this library's availability macros are inert by design
	 * (foundation-plan.md row D12), so the record is this comment. */
	NSJSONReadingAllowFragments = NSJSONReadingFragmentsAllowed
};

enum {
	NSJSONWritingPrettyPrinted = (1UL << 0),
	NSJSONWritingSortedKeys = (1UL << 1)
};

@interface NSJSONSerialization : NSObject

+ (nullable id)JSONObjectWithData:(NSData *)data
			  options:(NSJSONReadingOptions)options
			    error:(NSError * _Nullable * _Nullable)errorPtr;

/* THROWS on an object that cannot be represented, which is Apple's contract rather than an oversight:
 * a caller with an arbitrary object asks +isValidJSONObject: first. */
+ (nullable NSData *)dataWithJSONObject:(id)object
				options:(NSJSONWritingOptions)options
				  error:(NSError * _Nullable * _Nullable)errorPtr;

+ (BOOL)isValidJSONObject:(nullable id)object;

@end

NS_ASSUME_NONNULL_END
#endif /* FOUNDATION_NSJSONSERIALIZATION_H */
