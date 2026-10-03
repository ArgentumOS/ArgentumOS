/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSFormatter — the abstract base of the value-to-text family.
 * docs/design/foundation-plan.md §10, the un-refusal program, slice F13.
 *
 * Apple's NSFormatter is the parent of NSDateFormatter and NSNumberFormatter, and it is
 * ABSTRACT: it declares the doors and implements all but one of them by refusing, because what a
 * string IS depends entirely on the subclass. **A PARENT'S JOB HERE IS TO REFUSE WHAT IT CANNOT KNOW** —
 * a default answer would be a lie (nil would read as "this value has no text" where the truth is
 * "nothing was asked"). ⚠ THIS SENTENCE USED TO COMPARE THE SHAPE TO `NSPredicate`'s, which left the
 * surface with its 10.4 family in §63.161; the rule it was making is about ABSTRACT PARENTS, and it
 * stands without the example.
 *
 * COMPLETED AGAINST APPLE'S PAGE (2026-09-20, W11). Three members this header was missing when W11
 * read Apple's documentation against it, plus the `NSCoding` conformance Apple declares:
 * `-attributedStringForObjectValue:withDefaultAttributes:`, `-editingStringForObjectValue:`,
 * `-isPartialStringValid:proposedSelectedRange:originalString:originalSelectedRange:errorDescription:`,
 * and `<NSCoding>`. Each one's base behaviour below is Apple's OWN documented default, quoted,
 * except where noted — inventing a default is the thing this plan forbids (§11.5).
 */

#ifndef FOUNDATION_NSFORMATTER_H
#define FOUNDATION_NSFORMATTER_H

#import <Foundation/NSObject.h>
#import <Foundation/NSCoding.h>

/* The two doors that take OBJECT-shaped arguments Apple does not name concretely. `NSAttributedString`
 * is forward-declared rather than imported: the base's documented answer is nil, so no instance of it
 * is ever minted here, and W10 owns the class. */
@class NSDictionary;
@class NSAttributedString;

NS_ASSUME_NONNULL_BEGIN

@interface NSFormatter : NSObject <NSCopying, NSCoding>

/* THE DOORS. `-stringForObjectValue:` and `-getObjectValue:forString:errorDescription:` RAISE in the
 * base class, and Apple says so in as many words for both ("The default implementation of this
 * method raises an exception."). */
- (nullable NSString *)stringForObjectValue:(nullable id)object;
- (BOOL)getObjectValue:(id _Nullable * _Nullable)object
	     forString:(NSString *)string
      errorDescription:(NSString * _Nullable * _Nullable)error;

/* Apple: "The default implementation returns nil to indicate that the formatter object does not
 * provide an attributed string." So the base answers nil, and the return type is the reason this
 * method exists at all — a formatter that CAN render attributes overrides it (W10 owns the class). */
- (nullable NSAttributedString *)attributedStringForObjectValue:(nullable id)object
					   withDefaultAttributes:(nullable NSDictionary *)attributes;

/* Apple: "The default implementation of this method invokes -stringForObjectValue:." That is one
 * line here, and it is not a convenience: it is why a subclass that overrides only
 * `-stringForObjectValue:` still gets a working editing string — and why the base raises here too,
 * since the method it invokes is the one that raises. */
- (nullable NSString *)editingStringForObjectValue:(nullable id)object;

/* Partial validation, for a control that edits text in place. The BASE answers NO — which is
 * Apple's default for THIS form and means "no opinion", so a caller keeps its own behaviour; a
 * subclass that can validate overrides it. */
- (BOOL)isPartialStringValid:(NSString *)partialString
	    newEditingString:(NSString * _Nullable * _Nullable)newString
	    errorDescription:(NSString * _Nullable * _Nullable)error;

/* The same question asked with the EDIT IN THE MIDDLE OF THE STRING instead of at its end — the
 * form a field needs when a caret is anywhere (Apple: "This method should be implemented in
 * subclasses that want to validate user changes to a string in a field, where the user changes are
 * not necessarily at the end of the string…").
 *
 * THE BASE's BEHAVIOUR IS OURS, AND IT HAS TO BE SAID: `partialStringPtr` is in/out, and this
 * method's Apple page publishes NO default implementation — it describes what a SUBCLASS should do
 * and stops. Where Apple publishes no behaviour any choice conforms (§11.6 gate 1), so the choice
 * here is the one that keeps the family coherent: DELEGATE to the three-argument form above. A
 * subclass that implemented only the simpler door is then still honoured, and a subclass that
 * overrides neither keeps the base's "no opinion". */
- (BOOL)isPartialStringValid:(NSString * _Nullable * _Nullable)partialStringPtr
	 proposedSelectedRange:(nullable NSRangePointer)proposedSelRangePtr
		originalString:(NSString *)originalString
	  originalSelectedRange:(NSRange)originalSelectedRange
	       errorDescription:(NSString * _Nullable * _Nullable)error;

@end

/* The house styles a formatter may render in (2026-09-20): where in a sentence the
 * text will appear, and how long-winded to be. Names from Apple's documentation
 * index; values are ours (§11.6.1 D2, see NSFileManager.h). */
typedef enum {
	NSFormattingContextUnknown = 0,
	NSFormattingContextDynamic = 1,
	NSFormattingContextStandalone = 2,
	NSFormattingContextListItem = 3,
	NSFormattingContextBeginningOfSentence = 4,
	NSFormattingContextMiddleOfSentence = 5
} NSFormattingContext;

typedef enum {
	NSFormattingUnitStyleShort = 0,
	NSFormattingUnitStyleMedium = 1,
	NSFormattingUnitStyleLong = 2
} NSFormattingUnitStyle;

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSFORMATTER_H */
