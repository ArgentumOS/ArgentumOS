/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSTextCheckingResult — one occurrence found in text: a range, a kind, and the payload that kind carries.
 * docs/design/foundation-plan.md §62.18 (the class) and §62.19 (the vocabulary it needed).
 *
 * IT WAS BORN AS HALF A CLASS. F13.16 built it as "a regular expression's match": a ranges array and
 * `-range`/`-rangeAtIndex:`, which is what `NSRegularExpression` needed to answer with. Apple's class is the
 * GENERAL one — thirty-odd members — and every other result kind (a link, a date, a phone number, a
 * misspelling, a grammar error) is the same object with a different payload. This is that class, and
 * `NSDataDetector` is what produces the natural-language kinds.
 *
 * THE THIRTEEN TYPES AND THEIR VALUES ARE OURS (§11.6.1 D2), AND THAT IS A MEASURED FACT RATHER THAN A
 * PREFERENCE. Apple publishes the CASE NAMES on `NSTextCheckingResult.CheckingType` and, on each case's own
 * page, an abstract and NO NUMBER — in the ObjC view and in the Swift view alike (`variantOverrides` applied,
 * the recipe §62 recorded). The docs do not even order them alphabetically, so they are listed here in the
 * order Apple's page lists them and the bits follow that order; the three `All…` names are filed by Apple
 * under the `NSTextCheckingTypes` TYPEDEF rather than under the enum, which is why they are spelled at the
 * end of the same block here with a comment saying so. What the probe asserts is therefore only what is TRUE
 * of our own numbers: the thirteen bits are distinct, and the three masks bracket them.
 *
 * `-resultByAdjustingRangesWithOffset:` HAS NO PUBLISHED RULE for a shift that would invalidate a range: the
 * page carries a parameter note and a return value and nothing else. §11.6's first gate applies — where Apple
 * leaves behaviour undefined, any choice conforms — and the choice here is the one this library makes for a
 * caller error everywhere else (F4): `NSInvalidArgumentException`.
 *
 * `-rangeWithName:` IS REAL AND NOT A PLACEHOLDER, and it cost the `(?<name>…)` translation that
 * `NSRegularExpression` now performs before handing a pattern to POSIX — see that header for why a wrapper is
 * where the translation belongs, and for the ONE addition it needed: the name→index map lives on the
 * EXPRESSION, which is where a property of the pattern belongs, so there is no map ivar here and the result
 * asks the expression it already stores. A name that no group carries, and a group that did not participate,
 * both answer `NSMakeRange(NSNotFound, 0)`, which is Apple's spelling for "nothing here".
 *
 * WHAT IS NOT HERE, NAMED: `NSCoding`/`NSSecureCoding`. Apple's class conforms to both, and the conformance is
 * OWED rather than refused — but a `NSTextCheckingTypeRegularExpression` result must archive the PATTERN it
 * came from, and `NSRegularExpression` does not conform to `NSCoding` in this tree either (nor to
 * `NSSecureCoding`, which Apple's own class does declare). Two coder conformances in one step would mean
 * archiving a compiled pattern's options and rebuilding it on decode, which is its own unit; declaring the
 * conformance without it would be the silent lie this library refuses.
 */

#ifndef FOUNDATION_NSTEXTCHECKINGRESULT_H
#define FOUNDATION_NSTEXTCHECKINGRESULT_H

#import <Foundation/NSObject.h>
#import <Foundation/NSObjCRuntime.h>
#import <Foundation/NSDate.h>		/* NSTimeInterval, which a date result carries */

@class NSArray;
@class NSDate;
@class NSDictionary;
@class NSOrthography;
@class NSRegularExpression;
@class NSString;
@class NSTimeZone;
@class NSURL;

NS_ASSUME_NONNULL_BEGIN

/* THE THIRTEEN KINDS OF CHECKING, as a bitmask: a caller ORs them together to say what it wants looked for.
 * Apple's page lists them in this order and publishes no value for any of them (the note at the top of this
 * file), so the bit is the position in that list. */
typedef NS_OPTIONS(uint64_t, NSTextCheckingType) {
	NSTextCheckingTypeOrthography = 1ULL << 0,
	NSTextCheckingTypeSpelling = 1ULL << 1,
	NSTextCheckingTypeGrammar = 1ULL << 2,
	NSTextCheckingTypeDate = 1ULL << 3,
	NSTextCheckingTypeAddress = 1ULL << 4,
	NSTextCheckingTypeLink = 1ULL << 5,
	NSTextCheckingTypeQuote = 1ULL << 6,
	NSTextCheckingTypeDash = 1ULL << 7,
	NSTextCheckingTypeReplacement = 1ULL << 8,
	NSTextCheckingTypeCorrection = 1ULL << 9,
	NSTextCheckingTypeRegularExpression = 1ULL << 10,
	NSTextCheckingTypePhoneNumber = 1ULL << 11,
	NSTextCheckingTypeTransitInformation = 1ULL << 12,

	/* THE THREE MASKS, which Apple files under the typedef below rather than under the enum. THE SPLIT ITSELF
	 * IS OURS: the system half is the low 16 bits and the custom half is everything above it, which is the
	 * SHAPE Apple's prose describes - "the system supports its own internal types, and the user can extend
	 * those types by subclassing NSTextCheckingResult and adding their own custom types" - so a custom bit
	 * cannot collide with a system one, and three low bits stay free for system types this library has not
	 * needed yet. */
	NSTextCheckingAllSystemTypes = 0x0000FFFFULL,
	NSTextCheckingAllCustomTypes = 0xFFFFFFFFFFFF0000ULL,
	NSTextCheckingAllTypes = 0xFFFFFFFFFFFFFFFFULL
};

/* APPLE'S BITMASK TYPEDEF, beside the enum it masks: `NSTextCheckingType` is what ONE result reports and
 * `NSTextCheckingTypes` is what a detector is ASKED FOR, and both are the same 64-bit width on Apple's pages. */
typedef uint64_t NSTextCheckingTypes;

/* ---- THE KEYS OF THE COMPONENT DICTIONARIES --------------------------------------------------------
 *
 * `-components` carries whatever a result kind had to say in words, and these eleven names are the keys. The
 * STRING VALUES ARE OURS (§11.6.1 D2): Apple publishes the names and not the spellings, and a program compares
 * against the CONSTANT rather than the string, so the constant is the contract — the same reasoning
 * `NSURLError.h` records for the userInfo keys of an error. What the probe asserts is the property that would
 * actually hurt a caller: they are eleven DISTINCT strings, because two keys sharing one value would make a
 * component unaddressable.
 *
 * The nine address keys and the two transit keys are two families, and they are kept apart here with the
 * books Apple keeps them in. */
typedef NSString *NSTextCheckingKey;

extern NSTextCheckingKey const NSTextCheckingNameKey;
extern NSTextCheckingKey const NSTextCheckingJobTitleKey;
extern NSTextCheckingKey const NSTextCheckingOrganizationKey;
extern NSTextCheckingKey const NSTextCheckingStreetKey;
extern NSTextCheckingKey const NSTextCheckingCityKey;
extern NSTextCheckingKey const NSTextCheckingStateKey;
extern NSTextCheckingKey const NSTextCheckingZIPKey;
extern NSTextCheckingKey const NSTextCheckingCountryKey;
extern NSTextCheckingKey const NSTextCheckingPhoneKey;

extern NSTextCheckingKey const NSTextCheckingAirlineKey;
extern NSTextCheckingKey const NSTextCheckingFlightKey;

/* ONE OCCURRENCE. Every factory sets `resultType` to the kind it builds, so the class of a result is readable
 * from the object rather than from the door it came through — and a result that answers a payload of the wrong
 * kind answers NIL for it rather than a stray pointer, which is what the ivar table is for. */
@interface NSTextCheckingResult : NSObject <NSCopying>
{
	NSRange *_ranges;
	NSUInteger _count;
	NSTextCheckingType _resultType;
	NSString *_replacementString;
	NSArray *_alternativeStrings;
	NSRegularExpression *_regularExpression;
	NSDictionary *_components;
	NSURL *_URL;
	NSDictionary *_addressComponents;
	NSString *_phoneNumber;
	NSDate *_date;
	NSTimeZone *_timeZone;
	NSTimeInterval _duration;
	NSOrthography *_orthography;
	NSArray *_grammarDetails;
}

/* AN ADDITION, and it is the constructor the `NSRegularExpression` half of F13.16 already shipped: a result
 * that is nothing but ranges, of NO declared kind (`resultType` is 0, which is "no checker made this"). Most
 * callers want the Apple factories below; this one stays because it is public API in this tree and because a
 * subclass of a checker needs a way to speak in ranges. */
+ (instancetype)resultWithRanges:(const NSRange *)ranges count:(NSUInteger)count;

@property (readonly) NSTextCheckingType resultType;
@property (readonly) NSRange range;
@property (readonly) NSUInteger numberOfRanges;
- (NSRange)rangeAtIndex:(NSUInteger)idx;
- (NSRange)rangeWithName:(NSString *)name;

/* ---- THE FACTORIES, ONE PER KIND. Which type each one sets is the API's own definition (the section it is
 * documented under): a link result is NSTextCheckingTypeLink, and so on down the list.
 *
 * APPLE'S ELEMENT TYPES ARE STATED IN PROSE HERE, NOT AS GENERIC ARGUMENTS: this tree's `NSArray` and
 * `NSDictionary` carry no type parameters (the same deviation NSListFormatter.h, NSKeyedUnarchiverDelegate.h and
 * NSSecureUnarchiveFromDataTransformer.h each record), so Apple's `NSArray<NSString *> *` is `NSArray *` here.
 * What each one holds: `components` is a dictionary of strings keyed by the `NSTextCheckingKey` names above,
 * `details` is an array of dictionaries, and `alternativeStrings` is an array of strings. */
+ (NSTextCheckingResult *)replacementCheckingResultWithRange:(NSRange)range
					    replacementString:(NSString *)replacementString;
+ (NSTextCheckingResult *)regularExpressionCheckingResultWithRanges:(NSRangePointer)ranges
							      count:(NSUInteger)count
						   regularExpression:(NSRegularExpression *)regularExpression;
+ (NSTextCheckingResult *)linkCheckingResultWithRange:(NSRange)range URL:(NSURL *)url;
+ (NSTextCheckingResult *)addressCheckingResultWithRange:(NSRange)range
					      components:(NSDictionary *)components;
+ (NSTextCheckingResult *)transitInformationCheckingResultWithRange:(NSRange)range
							 components:(NSDictionary *)components;
+ (NSTextCheckingResult *)phoneNumberCheckingResultWithRange:(NSRange)range
						 phoneNumber:(NSString *)phoneNumber;
+ (NSTextCheckingResult *)dateCheckingResultWithRange:(NSRange)range date:(NSDate *)date;
+ (NSTextCheckingResult *)dateCheckingResultWithRange:(NSRange)range
						 date:(NSDate *)date
					     timeZone:(NSTimeZone *)timeZone
					     duration:(NSTimeInterval)duration;
+ (NSTextCheckingResult *)dashCheckingResultWithRange:(NSRange)range
				      replacementString:(NSString *)replacementString;
+ (NSTextCheckingResult *)quoteCheckingResultWithRange:(NSRange)range
				       replacementString:(NSString *)replacementString;
+ (NSTextCheckingResult *)spellCheckingResultWithRange:(NSRange)range;
+ (NSTextCheckingResult *)correctionCheckingResultWithRange:(NSRange)range
					    replacementString:(NSString *)replacementString;
+ (NSTextCheckingResult *)correctionCheckingResultWithRange:(NSRange)range
					    replacementString:(NSString *)replacementString
					   alternativeStrings:(NSArray *)alternativeStrings;
+ (NSTextCheckingResult *)orthographyCheckingResultWithRange:(NSRange)range
						 orthography:(NSOrthography *)orthography;
+ (NSTextCheckingResult *)grammarCheckingResultWithRange:(NSRange)range
						 details:(NSArray *)details;

/* ---- THE PAYLOADS, one accessor per kind. A result answers nil (or 0) for the payloads it does not carry. */
@property (readonly, copy, nullable) NSString *replacementString;
@property (readonly, copy, nullable) NSArray *alternativeStrings;
@property (readonly, copy, nullable) NSRegularExpression *regularExpression;
@property (readonly, copy, nullable) NSDictionary *components;
@property (readonly, copy, nullable) NSURL *URL;
@property (readonly, copy, nullable) NSDictionary *addressComponents;
@property (readonly, copy, nullable) NSString *phoneNumber;
@property (readonly, copy, nullable) NSDate *date;
@property (readonly, copy, nullable) NSTimeZone *timeZone;
@property (readonly) NSTimeInterval duration;
@property (readonly, copy, nullable) NSOrthography *orthography;
@property (readonly, copy, nullable) NSArray *grammarDetails;

- (NSTextCheckingResult *)resultByAdjustingRangesWithOffset:(NSInteger)offset;

- (BOOL)isEqual:(nullable id)other;
- (NSUInteger)hash;
- (NSString *)description;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSTEXTCHECKINGRESULT_H */
