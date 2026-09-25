/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * NSAttributedString — W10's core (plan §61). THE STORE IS OURS (§11.6.1 D2) AND ITS CONTRACT IS WHAT IS
 * OBSERVABLE: an attributed string is an ORDERED LIST OF RUNS - an NSRange in UTF-16 units (W1's rule: an
 * attribute range is an NSRange, which is why W10 cannot precede W1) and a dictionary of attributes - WITH
 * ADJACENT RUNS OF EQUAL ATTRIBUTES COALESCED. That last clause is not an optimisation: Apple's contract for
 * -attributesAtIndex:effectiveRange: is the LONGEST range over which the attributes are the same, so a store
 * that kept every insertion as its own run would answer a SHORT range and be wrong without any code being
 * wrong. -attribute:atIndex:effectiveRange: is the same question about ONE attribute, so its range is the
 * longest range over which THAT attribute's value holds - which can be longer than the run.
 *
 * THE RULES APPLE DOES NOT PUBLISH ARE CHOSEN HERE AND WRITTEN DOWN (§11.6.1 D2), because a probe has to
 * assert something and an undocumented choice asserted silently is how a difference becomes invisible:
 *
 *   - a replacement takes the attributes IN FORCE AT THE START of the replaced range; an EMPTY range takes
 *     the attributes at that index, and at the end of the string the last run's;
 *   - -addAttribute:value:range: with a nil value REMOVES the attribute (Apple documents the parameter and
 *     not the nil case);
 *   - -beginEditing/-endEditing NEST (a counter, not a flag), mutations are observable between them, and the
 *     coalescing an edit group implies is done at the OUTERMOST -endEditing;
 *   - -fixAttributesInRange: drops runs that fall outside the string and coalesces what is left; there are no
 *     attribute defaults to install, because no attribute in this library has a rendering default.
 *
 * WHAT W10 SLICE 1 DOES NOT CARRY, EACH WITH ITS GROUND, SO A CONFORMING PROGRAM STILL COMPILES: the file
 * formats (RTF/RTFD/HTML/docFormat - no writer or parser of those formats exists here, and the doors are
 * DECLARED with bodies that REFUSE BY NAME rather than doing nothing); the AppKit/UIKit/TextKit half (`size`,
 * the `draw…` family, `boundingRectWithSize:`, text lists, rulers, attachments, word and line-break
 * questions) which belongs to the drawing frameworks and is EXCLUDED here - the probe asserts their ABSENCE;
 * -mutableString (a LIVE proxy over the store, not a copy, and a slice of its own); and the two coding
 * protocols, which Apple's page lists and slice 3 owes.
 */

#import <Foundation/NSObject.h>

NS_ASSUME_NONNULL_BEGIN

@class NSString, NSDictionary, NSArray, NSData, NSURL, NSError;

/* "The type for the keys of the attribute dictionaries" - Apple's name, this library's spelling for a
 * typealias (§11.6.1 D2). */
typedef NSString *NSAttributedStringKey;

/* "The options for enumerating attributes" - the two members Apple publishes. Reverse walks the runs
 * BACKWARDS, and LongestEffectiveRangeNotRequired PERMITS a shorter range than the longest one: this
 * implementation reports the coalesced runs either way, which is compliant rather than optimal, and the probe
 * shows both that the option is accepted and that the coverage is the same. */
typedef enum {
	NSAttributedStringEnumerationLongestEffectiveRangeNotRequired = 1 << 0,
	NSAttributedStringEnumerationReverse = 1 << 1
} NSAttributedStringEnumerationOptions;

@interface NSAttributedString : NSObject <NSCopying, NSMutableCopying>
{
@protected		/* the mutable subclass works on the same store, which is the whole point of the split */
	NSString *_string;
	void *_runs;			/* struct fn_run *, owned; the .m defines the layout */
	NSUInteger _runCount;
	NSUInteger _runCapacity;
	NSInteger _editDepth;
}

- (instancetype)initWithString:(NSString *)str;
- (instancetype)initWithString:(NSString *)str attributes:(nullable NSDictionary *)attrs;
- (instancetype)initWithAttributedString:(NSAttributedString *)attrStr;

/* "The character data of the string." */
@property (readonly, copy) NSString *string;
/* "The length of the string's character data" - UTF-16 units, as everywhere in this library. */
@property (readonly) NSUInteger length;

- (NSDictionary *)attributesAtIndex:(NSUInteger)location effectiveRange:(nullable NSRangePointer)range;
- (nullable id)attribute:(NSAttributedStringKey)attrName
		  atIndex:(NSUInteger)location
	   effectiveRange:(nullable NSRangePointer)range;
- (NSDictionary *)attributesAtIndex:(NSUInteger)location
		longestEffectiveRange:(nullable NSRangePointer)range
			      inRange:(NSRange)rangeLimit;
- (nullable id)attribute:(NSAttributedStringKey)attrName
		  atIndex:(NSUInteger)location
    longestEffectiveRange:(nullable NSRangePointer)range
		  inRange:(NSRange)rangeLimit;

- (NSAttributedString *)attributedSubstringFromRange:(NSRange)range;
- (BOOL)isEqualToAttributedString:(NSAttributedString *)other;

- (void)enumerateAttributesInRange:(NSRange)enumerationRange
			   options:(NSAttributedStringEnumerationOptions)opts
			usingBlock:(void (^)(NSDictionary *attrs, NSRange range, BOOL *stop))block;
- (void)enumerateAttribute:(NSAttributedStringKey)attrName
		   inRange:(NSRange)enumerationRange
		   options:(NSAttributedStringEnumerationOptions)opts
		usingBlock:(void (^)(_Nullable id value, NSRange range, BOOL *stop))block;

@end

@interface NSMutableAttributedString : NSAttributedString

- (instancetype)initWithString:(NSString *)str;
- (instancetype)initWithString:(NSString *)str attributes:(nullable NSDictionary *)attrs;

- (void)replaceCharactersInRange:(NSRange)range withString:(NSString *)str;
- (void)replaceCharactersInRange:(NSRange)range withAttributedString:(NSAttributedString *)attrString;
- (void)insertAttributedString:(NSAttributedString *)attrString atIndex:(NSUInteger)loc;
- (void)appendAttributedString:(NSAttributedString *)attrString;
- (void)deleteCharactersInRange:(NSRange)range;
- (void)setAttributedString:(NSAttributedString *)attrString;

- (void)addAttribute:(NSAttributedStringKey)name value:(id)value range:(NSRange)range;
- (void)addAttributes:(NSDictionary *)attrs range:(NSRange)range;
- (void)removeAttribute:(NSAttributedStringKey)name range:(NSRange)range;
- (void)setAttributes:(nullable NSDictionary *)attrs range:(NSRange)range;

/* "Nesting is supported": this is a counter, and the coalescing happens at the outermost -endEditing. */
- (void)beginEditing;
- (void)endEditing;
- (void)fixAttributesInRange:(NSRange)range;

@end

NS_ASSUME_NONNULL_END
