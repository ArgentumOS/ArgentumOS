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
 * WHAT W10 SLICE 1 DOES NOT CARRY, EACH WITH ITS GROUND, SO A CONFORMING PROGRAM STILL COMPILES: three of the
 * four file formats (RTFD, HTML and the doc format - §62.58 made the FOURTH, RTF, a real writer, so the doors
 * are DECLARED with bodies that either write the format or REFUSE BY NAME rather than doing nothing); the
 * AppKit/UIKit/TextKit half (`size`,
 * the `draw…` family, `boundingRectWithSize:`, text lists, rulers, attachments, word and line-break
 * questions) which belongs to the drawing frameworks and is EXCLUDED here - the probe asserts their ABSENCE;
 * -mutableString (a LIVE proxy over the store, not a copy, and a slice of its own); and the two coding
 * protocols, which Apple's page lists and slice 3 owes.
 */

#import <Foundation/NSObject.h>
#import <Foundation/NSCoding.h>
#import <Foundation/NSURLRequest.h>
#import <Foundation/NSFileWrapper.h>

NS_ASSUME_NONNULL_BEGIN




/* ---- THE MODERN ATTRIBUTE FAMILIES AND THEIR CONSTANTS (W10 slice 4) --------------------------------
 *
 * THE NAMES ARE APPLE'S AND THE VALUES ARE OURS, which is the recorded finding this library works under
 * (§11.6.1 D2): Apple publishes the CASE NAMES of every one of these enums and neither their numbers nor a
 * stable order, and the same is true of the attribute-name constants, whose strings are their own names here.
 * A reader who needs the numbers should read them here rather than hope they match another platform.
 *
 * AND THE BEHAVIOUR BEHIND THE FAMILIES IS NOT IMPLEMENTED, WHICH IS NAMED RATHER THAN IMPLIED: a markdown
 * importer, the grammar-agreement engine and the presentation-intent OBJECT graph are each a text subsystem
 * of their own, and this slice ships the vocabulary they are spoken with - the constants and the option bits
 * - so that a conforming program compiles and the absence is visible where it is asked for. */




@class NSString, NSDictionary, NSArray, NSData, NSURL, NSError;

/* "The type for the keys of the attribute dictionaries" - Apple's name, this library's spelling for a
 * typealias (§11.6.1 D2). */
typedef NSString *NSAttributedStringKey;

/* ---- THE MODERN ATTRIBUTE FAMILIES AND THEIR CONSTANTS (W10 slice 4) --------------------------------

 * THE NAMES ARE APPLE'S AND THE VALUES ARE OURS, which is the recorded finding this library works under
 * (§11.6.1 D2): Apple publishes the CASE NAMES of these enums and neither their numbers nor a stable order,
 * and the same is true of the attribute-name constants, whose strings are their own names here. A reader who
 * needs the numbers should read them here rather than hope they match another platform.
 *
 * AND THE BEHAVIOUR BEHIND THE FAMILIES IS NOT IMPLEMENTED, WHICH IS NAMED RATHER THAN IMPLIED: a markdown
 * importer, the grammar-agreement engine and the presentation-intent OBJECT graph are each a text subsystem
 * of their own, and this slice ships the vocabulary they are spoken with - the constants and the option bits
 * - so that a conforming program compiles and the absence is visible where it is asked for. */

typedef enum {
	NSAttributedStringFormattingApplyReplacementIndexAttribute = 1 << 0,
	NSAttributedStringFormattingInsertArgumentAttributesWithoutMerging = 1 << 1,
} NSAttributedStringFormattingOptions;
typedef enum {
	NSInlinePresentationIntentBlockHTML = 1 << 0,
	NSInlinePresentationIntentCode = 1 << 1,
	NSInlinePresentationIntentEmphasized = 1 << 2,
	NSInlinePresentationIntentInlineHTML = 1 << 3,
	NSInlinePresentationIntentLineBreak = 1 << 4,
	NSInlinePresentationIntentSoftBreak = 1 << 5,
	NSInlinePresentationIntentStrikethrough = 1 << 6,
	NSInlinePresentationIntentStronglyEmphasized = 1 << 7,
} NSInlinePresentationIntent;
typedef enum {
	NSPresentationIntentKindBlockQuote = 0,
	NSPresentationIntentKindCodeBlock = 1,
	NSPresentationIntentKindHeader = 2,
	NSPresentationIntentKindListItem = 3,
	NSPresentationIntentKindOrderedList = 4,
	NSPresentationIntentKindParagraph = 5,
	NSPresentationIntentKindTable = 6,
	NSPresentationIntentKindTableCell = 7,
	NSPresentationIntentKindTableHeaderRow = 8,
	NSPresentationIntentKindTableRow = 9,
	NSPresentationIntentKindThematicBreak = 10,
	NSPresentationIntentKindUnorderedList = 11,
} NSPresentationIntentKind;
typedef enum {
	NSPresentationIntentTableColumnAlignmentCenter = 0,
	NSPresentationIntentTableColumnAlignmentLeft = 1,
	NSPresentationIntentTableColumnAlignmentRight = 2,
} NSPresentationIntentTableColumnAlignment;

extern NSAttributedStringKey const NSAlternateDescriptionAttributeName;
extern NSAttributedStringKey const NSImageURLAttributeName;
extern NSAttributedStringKey const NSInflectionAgreementArgumentAttributeName;
extern NSAttributedStringKey const NSInflectionAgreementConceptAttributeName;
extern NSAttributedStringKey const NSInflectionAlternativeAttributeName;
extern NSAttributedStringKey const NSInflectionReferentConceptAttributeName;
extern NSAttributedStringKey const NSInflectionRuleAttributeName;
extern NSAttributedStringKey const NSInlinePresentationIntentAttributeName;
extern NSAttributedStringKey const NSLanguageIdentifierAttributeName;
extern NSAttributedStringKey const NSListItemDelimiterAttributeName;
extern NSAttributedStringKey const NSLocalizedNumberFormatAttributeName;
extern NSAttributedStringKey const NSMarkdownSourcePositionAttributeName;
extern NSAttributedStringKey const NSMorphologyAttributeName;
extern NSAttributedStringKey const NSPresentationIntentAttributeName;
extern NSAttributedStringKey const NSReplacementIndexAttributeName;

/* "The options for enumerating attributes" - the two members Apple publishes. Reverse walks the runs
 * BACKWARDS, and LongestEffectiveRangeNotRequired PERMITS a shorter range than the longest one: this
 * implementation reports the coalesced runs either way, which is compliant rather than optimal, and the probe
 * shows both that the option is accepted and that the coverage is the same. */
typedef enum {
	NSAttributedStringEnumerationLongestEffectiveRangeNotRequired = 1 << 0,
	NSAttributedStringEnumerationReverse = 1 << 1
} NSAttributedStringEnumerationOptions;

/* CODING: the class conforms on Apple's page (NSCoding + NSSecureCoding), and the payload travels as a
 * PROPERTY LIST - see the .m for why that is the decision rather than a shortcut. THE ENFORCEMENT IS THE
 * CODER'S GAP, NAMED IN NSCoding.h: it has no -decodeObjectOfClass:, so +supportsSecureCoding answers YES
 * while the archiver does not yet check. */
@interface NSAttributedString : NSObject <NSCopying, NSMutableCopying, NSCoding, NSSecureCoding>
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

/* ---- THE FILE-FORMAT DOORS (W10 slice 4): RTF WRITES, THE REST REFUSE BY NAME -----------------------
 *
 * -RTFFromRange:documentAttributes: is a REAL RTF writer (§62.58): a valid `{\rtf1…}` document carrying the
 * range's text with RTF's reserved characters escaped, non-ASCII as the format's own `\uN?` escape, and the
 * NSInlinePresentationIntent bits as the control words that mean them. IT IS NOT A STUB, AND WHAT IT DOES
 * NOT CARRY IS A BOUNDARY WITH A GROUND: the AppKit character attributes (font, colour, paragraph style,
 * underline) have NO TYPE IN THIS SYSTEM - there is no NSFont, NSColor or NSParagraphStyle anywhere in the
 * tree - so no caller can construct one and there is nothing to map.
 *
 * THE REST STILL REFUSE, EACH NAMING ITS FORMAT AND ITS REASON, rather than answering a silent nil that a
 * caller could not tell from an empty document: RTFD needs an attachment no class here defines; the doc
 * format is Word's binary container, whose specification this system does not carry; HTML import needs a
 * parser and a fetch; and the polymorphic doors pick their format from a document-type attribute whose value
 * vocabulary belongs to the AppKit half that is not here. Apple's own shape for a door that cannot do its
 * work is an NSError the caller must handle, and Apple itself discourages the synchronous HTML forms. */
- (nullable NSData *)dataFromRange:(NSRange)range
	       documentAttributes:(nullable NSDictionary *)dict
			    error:(NSError ** _Nullable)error;
- (nullable NSData *)RTFFromRange:(NSRange)range
	     documentAttributes:(nullable NSDictionary *)dict;
- (nullable NSData *)RTFDFromRange:(NSRange)range
	      documentAttributes:(nullable NSDictionary *)dict;
- (nullable NSFileWrapper *)RTFDFileWrapperFromRange:(NSRange)range
				documentAttributes:(nullable NSDictionary *)dict;
- (nullable NSDictionary *)fileWrapperFromRange:(NSRange)range
			   documentAttributes:(nullable NSDictionary *)dict
					error:(NSError ** _Nullable)error;
- (nullable NSData *)docFormatFromRange:(NSRange)range
		   documentAttributes:(nullable NSDictionary *)dict;
+ (void)loadFromHTMLWithRequest:(NSURLRequest *)request
			options:(nullable NSDictionary *)options
	      completionHandler:(void (^)(NSAttributedString * _Nullable, NSDictionary * _Nullable,
					  NSError * _Nullable))completionHandler;

- (void)encodeWithCoder:(NSCoder *)coder;
- (nullable instancetype)initWithCoder:(NSCoder *)coder;
+ (BOOL)supportsSecureCoding;

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
