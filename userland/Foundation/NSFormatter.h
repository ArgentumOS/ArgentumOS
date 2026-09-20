/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSFormatter — the abstract base of the value-to-text family.
 * docs/design/foundation-plan.md §10, the un-refusal program, slice F13.
 *
 * Apple's NSFormatter is the parent of NSDateFormatter and NSNumberFormatter, and it is
 * ABSTRACT: it declares the two doors and implements neither, because what a string IS depends
 * entirely on the subclass. That is the same shape NSPredicate took, and for the same reason —
 * a default answer would be a lie (nil would read as "this value has no text" where the truth is
 * "nothing was asked").
 */

#ifndef FOUNDATION_NSFORMATTER_H
#define FOUNDATION_NSFORMATTER_H

#import <Foundation/NSObject.h>

NS_ASSUME_NONNULL_BEGIN

@interface NSFormatter : NSObject <NSCopying>

/* THE TWO DOORS. Both RAISE in the base class. */
- (nullable NSString *)stringForObjectValue:(nullable id)object;
- (BOOL)getObjectValue:(id _Nullable * _Nullable)object
	     forString:(NSString *)string
      errorDescription:(NSString * _Nullable * _Nullable)error;

/* Partial validation, for a control that edits text in place. The BASE answers NO — which is
 * Apple's default and means "no opinion", so a caller keeps its own behaviour; a subclass that
 * can validate overrides it. */
- (BOOL)isPartialStringValid:(NSString *)partialString
	    newEditingString:(NSString * _Nullable * _Nullable)newString
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
