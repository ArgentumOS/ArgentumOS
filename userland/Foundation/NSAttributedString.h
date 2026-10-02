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
 * AppKit/UIKit/TextKit half (`size`, the `draw…` family, `boundingRectWithSize:`, text lists, rulers and
 * attachments) which belongs to the drawing frameworks and is EXCLUDED here - the probe asserts their
 * ABSENCE; ⚠⚠ AND THE ATTACHMENTS HALF OF THAT SENTENCE IS **MEASURED, DO NOT RE-CHASE IT** (§63.81):
 * `+attributedStringWithAttachment:`, `+attributedStringWithAttachment:attributes:`, `-containsAttachmentsInRange:`,
 * `-containsAttachments` and the two constants `NSAttachmentAttributeName`/`NSAttachmentCharacter` appear
 * NOWHERE in the macOS 14.5 Foundation corpus (170 headers) **or** the iOS 16.5 Foundation corpus (125) — they
 * are declared by AppKit/UIKit, in `NSTextAttachment.h`, which neither corpus carries. **A LEDGER ROW'S OWNER IS
 * NOT ITS DECLARER:** the derived surface groups a class's EXTENSIONS under the class, so these sit under
 * `NSAttributedString` while being the drawing frameworks' declarations. **AND THE TEST THAT SEPARATES THE TWO
 * CASES IS NOT THE PARAMETER'S CLASS** — `NSNumberFormatter`'s `-roundingBehavior` takes an
 * `NSDecimalNumberHandler` and is Foundation's (§63.80) — **IT IS WHETHER THE DECLARATION IS ABSENT FROM
 * FOUNDATION'S OWN HEADERS**, and here it is absent from both; -mutableString (a LIVE proxy over the store, not a copy, and a slice of its own); and the two
 * coding protocols, which Apple's page lists and slice 3 owes.
 *
 * AND THE WORD AND LINE-BREAK QUESTIONS ARE FOUNDATION'S OWN AND SHIP HERE (2026-09-30). An earlier draft of
 * this prose filed `doubleClickAtIndex:`, `nextWordFromIndex:forward:` and `lineBreakBeforeIndex:withinRange:`
 * under "the excluded AppKit half", which was STALE: those are the "Calculating linguistic units" group,
 * they are answered over `FNTextBreaking` - the word/line-break substrate this library already ships (§62.42,
 * the engine `-enumerateSubstringsInRange:options:` and `NSLinguisticTagger` sit on) - and the ledger owns
 * them. Only the HYPHENATING break (`lineBreakByHyphenatingBeforeIndex:withinRange:`) stays deferred, because
 * it needs a hyphenation resource this system does not carry.
 */

#import <Foundation/NSObject.h>
#import <Foundation/NSLocale.h>
#include <stdarg.h>
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

/* THE TYPE OF A FORMATTING-CONTEXT KEY (§62.105), and the ONE key this library declares for it. Together
 * they are the vocabulary of the attributed-string FORMATTING CONTEXT — the dictionary a localized format
 * is built with — which is the same kind of surface as the option bits below: names a conforming program
 * compiles against, with the behaviour that consumes them named where it lives.
 *
 * AND THE NAME IS A TRAP WORTH RECORDING: `NSInflectionConceptsKey` sounds like an NSError userInfo key and
 * is NOT one — it is a key in this context dictionary (measured on Apple's page, which files it under
 * `NSAttributedStringFormattingContextKey`). A guess from the name would have put it in the wrong header. */
typedef NSString *NSAttributedStringFormattingContextKey;
extern NSAttributedStringFormattingContextKey const NSInflectionConceptsKey;

/* ⚠⚠ THESE TWO VALUES WERE THE WRONG WAY ROUND UNTIL §63.82, AND THE CORPUS PUBLISHES THEM (§63.81's test
 * applied first: all nine of the formatting family's rows ARE in Foundation's own headers, unlike the attachments).
 * Apple's macOS 14.5 declaration reads InsertArgumentAttributesWithoutMerging = 1 << 0 and
 * ApplyReplacementIndexAttribute = 1 << 1; this header had them SWAPPED. **THE BUG THAT MAKES IS AN ABI ONE AND
 * NOT A COSMETIC ONE:** a caller compiled against Apple's header passes `1 << 0` meaning "insert the argument's
 * attributes without merging" and this library would read it as "apply the replacement index" — the OPPOSITE
 * request, silently. AND THE ORIGINAL ASSERTION OF THE VALUES HAD STOOD IN A PROBE, which is why it survived:
 * **a check can preserve a defect exactly as faithfully as it can preserve a contract.** */
typedef enum {
	NSAttributedStringFormattingInsertArgumentAttributesWithoutMerging = 1 << 0,
	NSAttributedStringFormattingApplyReplacementIndexAttribute = 1 << 1,
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
/* §62.107: THE ONE ATTRIBUTE NAME APPLE'S FOUNDATION INDEX DOES NOT FILE UNDER FOUNDATION — its ledger
 * has no row for this name because Apple documents it on an AppKit page — and the markdown importer needs it,
 * because Apple conveys a link with EXACTLY this attribute and there is no inline intent for one. So it is
 * declared here, in the Foundation header that owns every other attribute name in this library, and the
 * deviation is on the record rather than hidden behind a home the tree does not have. */
extern NSAttributedStringKey const NSLinkAttributeName;
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
/* ===================================================================================================
 * NSATTRIBUTEDSTRING AND §C.3 (2026-09-29, M7). A FRONT: the front IS the concrete class, and
 * NSMutableAttributedString is its PUBLIC subclass - which is why the two answer the archiver differently.
 *
 * THE PRIMITIVES, and the doors written over them:
 *
 *     -string      -attributesAtIndex:effectiveRange:
 *
 * -length, -hash, -attribute:atIndex:effectiveRange:, -isEqualToAttributedString: and the other
 * attribute-reading doors are over those two, so a class answering them is correct through the family. THE RUN
 * MACHINERY IS NOT (fnRunAt, fnInsertRun, fnCoalesce and their neighbours ARE this class's storage), and
 * neither are the mutators of the mutable subclass: those ARE the storage implementation, exactly as the array
 * family's mutable class is.
 * =================================================================================================== */

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

/* THE THREE SIBLINGS OF THE REQUEST DOOR (2026-09-30), EACH WITH THE SAME ERROR-CHANNEL CONTRACT: a caller
 * that has the HTML as bytes, as a file URL or as a string hands it over directly instead of wrapping it in
 * an NSURLRequest, and each door runs the completion handler ONCE with the refusal - exactly as
 * +loadFromHTMLWithRequest:options:completionHandler: does - because HTML import needs a parser this system
 * does not carry, and Apple's own guidance is that the handler always runs. */
+ (void)loadFromHTMLWithData:(NSData *)data
                     options:(nullable NSDictionary *)options
           completionHandler:(void (^)(NSAttributedString * _Nullable, NSDictionary * _Nullable,
				      NSError * _Nullable))completionHandler;
+ (void)loadFromHTMLWithFileURL:(NSURL *)fileURL
                        options:(nullable NSDictionary *)options
              completionHandler:(void (^)(NSAttributedString * _Nullable, NSDictionary * _Nullable,
					NSError * _Nullable))completionHandler;
+ (void)loadFromHTMLWithString:(NSString *)string
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

/* ---- THE WORD AND LINE-BREAK QUESTIONS (2026-09-30, plan §61) --------------------------------------
 *
 * THESE ARE THE "Calculating linguistic units" GROUP, AND THEY ARE FOUNDATION'S OWN API HERE RATHER THAN THE
 * EXCLUDED APPKIT HALF the header's opening once called them: the questions "which word is at this index",
 * "where does the next word start" and "where does this line begin" are answered over `FNTextBreaking`, the
 * word/line-break substrate this library already ships (§62.42), exactly as `-enumerateSubstringsInRange:`
 * and `NSLinguisticTagger` are. The ledger files them under `NSAttributedString`, so they belong here.
 *
 * THE CONTRACTS ARE APPLE'S (AppKit's NSAttributedString additions, measured 2026-09-30), and each body says
 * which reading of an Apple phrase this library took, because a probe has to assert something:
 *   - `-doubleClickAtIndex:` answers the RANGE of the word (or other linguistic unit) the index sits in -
 *     the range a text system selects on a double-click;
 *   - `-nextWordFromIndex:forward:` answers the index of the next word's first character when `forward` is
 *     YES and of the nearest previous word's first character otherwise, WITHOUT regard to whether the index
 *     is inside a word, and it answers the index UNCHANGED when the walk would pass either end;
 *   - `-lineBreakBeforeIndex:withinRange:` answers the index where the line containing `index` begins (the
 *     nearest character at or before `index`, within the range, that stands at the start of a line), or
 *     `NSNotFound` when the range holds no such index.
 * ALL THREE RAISE NSRangeException for an out-of-bounds index, which is Apple's own precondition. */
- (NSRange)doubleClickAtIndex:(NSUInteger)location;
- (NSUInteger)nextWordFromIndex:(NSUInteger)location forward:(BOOL)isForward;
- (NSUInteger)lineBreakBeforeIndex:(NSUInteger)index withinRange:(NSRange)aRange;

/* THE DEPRECATED URL DOOR, AND THE ONE ROW OF ITS GROUP THAT NEEDS NO DRAWING LAYER: Apple deprecated it in
 * 10.11 (there is no replacement; `NSDataDetector` is suggested) and published the SHAPE and not the
 * tokenizer, so the tokenizer here is this library's own and is written down where it lives (§11.6.1 D2):
 * a URL is a whitespace-delimited token carrying a `scheme://`, after sentence punctuation at its edges is
 * trimmed. The door answers the URL and its range when `index` falls inside such a token, and nil with an
 * empty range otherwise. */
- (nullable NSURL *)URLAtIndex:(NSUInteger)index effectiveRange:(nullable NSRangePointer)effectiveRange;

/* ---- THE SUPPORTED-TEXT-FORMAT DOORS (2026-10-01, plan §61) ----------------------------------------
 *
 * THE ONE GROUP OF THIS CLASS'S OPEN LEDGER ROWS THAT NEEDS NEITHER A DRAWING LAYER NOR A TYPE THIS SYSTEM
 * LACKS. Apple's "Getting the supported text-file formats" group holds SEVEN members; SIX ship here and the
 * seventh, -prefersRTFDInRange:, stays ABSENT because it answers a question about ATTACHMENTS, which have no
 * class in this tree. The two modern members are CLASS PROPERTIES - Apple's role is "Type Property"
 * (`class var … { get }`) and its header spells them `(cpy)` - and the four legacy members are CLASS METHODS
 * Apple deprecated in 10.5; BOTH SHAPES ARE REPRODUCED EXACTLY (§11.0, the surface rule).
 *
 * THE VALUES ARE THIS LIBRARY'S, as §11.6.1 D2 records for every constant surface: Apple PUBLISHES the
 * legacy doors' defaults ("txt", "rtf", "rtfd", "html" and the four pasteboard types) and the modern doors'
 * INTENT (UTI strings), and this system carries no user-installed filter service, so a FILTERED list is its
 * UNFILTERED twin and each answers the vocabulary recorded in the .m rather than another platform's. All six
 * are CLASS members, so the mutable subclass answers them through inheritance, exactly as Apple's do. */
+ (NSArray *)textFileTypes;			/* DEPRECATED 10.5 */
+ (NSArray *)textUnfilteredFileTypes;		/* DEPRECATED 10.5 */
+ (NSArray *)textPasteboardTypes;		/* DEPRECATED 10.5 */
+ (NSArray *)textUnfilteredPasteboardTypes;	/* DEPRECATED 10.5 */
@property (class, readonly, copy) NSArray *textTypes;
@property (class, readonly, copy) NSArray *textUnfilteredTypes;


@end

/* \u26a0\u26a0 DECLARED IN A CATEGORY BECAUSE THAT IS APPLE'S OWN SHAPE (`NSAttributedString
 * (NSAttributedStringFormatting)` in the corpus): implementing in a category what the CLASS declares draws
 * "category is implementing a method which will also be implemented by its primary class" \u2014 A WARNING
 * ABOUT SHAPE IS A WARNING ABOUT FIDELITY. \u26a0 The `arguments:` door takes a `va_list`, NOT a dictionary
 * (\u00a763.82, from Apple's declaration: the ledger lists selectors and cannot tell). \u26a0 `locale:` is
 * ACCEPTED AND NOT USED, stated rather than hidden \u2014 this engine's conversions are C's and C's are
 * locale-free. */
@interface NSAttributedString (NSAttributedStringFormatting)
- (instancetype)initWithFormat:(NSAttributedString *)format
		       options:(NSAttributedStringFormattingOptions)options
			locale:(nullable NSLocale *)locale, ...;
- (instancetype)initWithFormat:(NSAttributedString *)format
		       options:(NSAttributedStringFormattingOptions)options
			locale:(nullable NSLocale *)locale
		     arguments:(va_list)arguments;
@end
/* ---- THE MARKDOWN FILE DOOR, IN APPLE'S FOUR-ARGUMENT SPELLING (2026-09-30) -------------------------
 *
 * APPLE DECLARES THIS INITIALISER WITH a `baseURL:` argument, and the tree shipped the SHORTER
 * `-initWithContentsOfMarkdownFileAtURL:options:error:` (declared in NSAttributedStringMarkdown.h) without
 * it. The surface rule (§11.0) is method-signature-for-method-signature, so the Apple form is ADDED here
 * and the shorter door KEEPS WORKING: this method differs from it only by threading `baseURL` through to the
 * importer, where the shorter one passes the file's own URL as the base. Both parse the same way; the
 * importer is in NSAttributedStringMarkdown.m.
 *
 * IT LIVES IN THIS HEADER RATHER THAN THE MARKDOWN ONE because the owning ledger row is NSAttributedString's
 * and this file is where that class's surface is declared; the options type it takes is forward-declared. */
@class NSAttributedStringMarkdownParsingOptions;
@interface NSAttributedString (FNMarkdownFileURL)
- (nullable instancetype)initWithContentsOfMarkdownFileAtURL:(NSURL *)url
                                                     options:(nullable NSAttributedStringMarkdownParsingOptions *)options
                                                     baseURL:(nullable NSURL *)baseURL
                                                       error:(NSError * _Nullable * _Nullable)error;
@end

@interface NSMutableAttributedString : NSAttributedString

- (instancetype)initWithString:(NSString *)str;
- (instancetype)initWithString:(NSString *)str attributes:(nullable NSDictionary *)attrs;

- (void)replaceCharactersInRange:(NSRange)range withString:(NSString *)str;
- (void)replaceCharactersInRange:(NSRange)range withAttributedString:(NSAttributedString *)attrString;
- (void)insertAttributedString:(NSAttributedString *)attrString atIndex:(NSUInteger)loc;
- (void)appendAttributedString:(NSAttributedString *)attrString;
/* APPLE'S CURRENT-LOCALE FORMAT DOOR (macOS 12+, ledger row NSMutableAttributedString/-appendLocalizedFormat:).
 * Apple's spelling is variadic, exactly like +stringWithFormat:: "Formats the specified string and arguments
 * with the current locale, then appends the result to the receiver." It is built over this library's own
 * -initWithFormat:locale:arguments:, whose locale door is DOCUMENTED to accept the locale and render the
 * locale-free answer (a locale is honoured for case only here, NSString.h says so) - so no second rendering
 * rule is invented. MUTABLE-ONLY: the immutable base does not answer it. */
- (void)appendLocalizedFormat:(NSString *)format, ...;
- (void)deleteCharactersInRange:(NSRange)range;
- (void)setAttributedString:(NSAttributedString *)attrString;

/* NULLABLE value, WHICH THE DOCUMENTED CONTRACT REQUIRES: the opening comment records that a nil value
 * REMOVES the attribute, so the parameter is nullable - a nonnull spelling here made every caller that
 * passes nil (the documented removal, exercised by the probe) warn under -Wnonnull. */
- (void)addAttribute:(NSAttributedStringKey)name value:(nullable id)value range:(NSRange)range;
- (void)addAttributes:(NSDictionary *)attrs range:(NSRange)range;
- (void)removeAttribute:(NSAttributedStringKey)name range:(NSRange)range;
- (void)setAttributes:(nullable NSDictionary *)attrs range:(NSRange)range;

/* "Nesting is supported": this is a counter, and the coalescing happens at the outermost -endEditing. */
- (void)beginEditing;
- (void)endEditing;
- (void)fixAttributesInRange:(NSRange)range;

@end

NS_ASSUME_NONNULL_END
