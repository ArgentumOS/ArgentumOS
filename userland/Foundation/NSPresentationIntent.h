/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * NSPresentationIntent — WHAT A PIECE OF AN ATTRIBUTED STRING IS (plan §62.65). The last row of the
 * `Fundamentals / Strings with Metadata` family, and the OBJECT the W10 vocabulary has been pointing at since
 * the attributes shipped: `NSPresentationIntentAttributeName` names this class's value, `NSPresentationIntentKind`
 * is its kind, and `NSPresentationIntentTableColumnAlignment` aligns its table's columns — all three declared in
 * NSAttributedString.h, which this header imports for them.
 *
 * ONE CLASS, TWELVE FACTORIES, AND THAT IS APPLE'S SHAPE RATHER THAN OURS: there is no `-init` and no settable
 * property, because an intent's fields are decided by WHAT KIND OF THING IT IS — a header has a level, a list
 * item has an ordinal, a table row has a row number, a table has columns — so Apple gives one factory per kind
 * and no way to build a half-formed one. Each factory takes an `identity` (the document's own identifier for
 * this intent) and a `parent` intent to nest inside, which is why `-parentIntent` is a real door and why the
 * INDENTATION of a nested list can be computed rather than stored.
 *
 * TWO RULES APPLE STATES, EACH READ OFF THE PAGE THAT OWNS IT:
 *
 *   * `-indentationLevel`: "The initial list has an indentation level of 0. Each time you nest a new list, the
 *     indentation level for new list increases by 1. ALL ELEMENTS WITHIN THE SAME LIST HAVE THE SAME INDENTATION
 *     LEVEL." That last clause is what makes this a count of NESTED LISTS rather than of ancestors, and it is
 *     computed from the parent chain here rather than carried in a field.
 *   * `-isEquivalentToPresentationIntent:`: "Two intents are equivalent if their ATTRIBUTES match. This method
 *     doesn't consider the [identity] property ... when determining their equivalence" — so two intents of the
 *     same kind carrying the same fields ARE equivalent even when they are different objects with different
 *     identities, which is exactly what a caller comparing a re-parsed document needs.
 *
 * AND ONE ABSENCE RULE IS OURS, STATED WHERE IT BITES: a field the receiver's KIND does not carry answers 0
 * (Apple publishes an absence rule for exactly one of them — `-columnAlignments` is nil "if the intent is not a
 * table" — and for the numbers it publishes none). §11.6.1 D2.
 */

#import <Foundation/NSObject.h>
#import <Foundation/NSAttributedString.h>

NS_ASSUME_NONNULL_BEGIN

@class NSArray, NSString;

@interface NSPresentationIntent : NSObject
{
@protected
	NSPresentationIntentKind _intentKind;
	NSInteger _identity;
	NSPresentationIntent *_parentIntent;	/* retained: a child outlives its parent in a caller's hands */
	NSInteger _headerLevel;
	NSInteger _ordinal;
	NSInteger _row;
	NSInteger _column;
	NSInteger _columnCount;
	NSArray *_columnAlignments;		/* retained; NSNumber, each an NSPresentationIntentTableColumnAlignment */
	NSString *_languageHint;		/* copied */
}

/* THE TWELVE FACTORIES, one per kind. `nestedInsideIntent:` is the parent and may be nil for a document's
 * outermost intent; Apple's parameter names are kept, including the long `nestedInsideIntent:` spelling that
 * distinguishes this modern door from the older `-presentationIntentWithKind:identity:parent:` shape. */
+ (instancetype)paragraphIntentWithIdentity:(NSInteger)identity
			  nestedInsideIntent:(nullable NSPresentationIntent *)parent;
+ (instancetype)headerIntentWithIdentity:(NSInteger)identity
				   level:(NSInteger)level
		      nestedInsideIntent:(nullable NSPresentationIntent *)parent;
+ (instancetype)orderedListIntentWithIdentity:(NSInteger)identity
			   nestedInsideIntent:(nullable NSPresentationIntent *)parent;
+ (instancetype)unorderedListIntentWithIdentity:(NSInteger)identity
			     nestedInsideIntent:(nullable NSPresentationIntent *)parent;
+ (instancetype)listItemIntentWithIdentity:(NSInteger)identity
				   ordinal:(NSInteger)ordinal
			nestedInsideIntent:(nullable NSPresentationIntent *)parent;
+ (instancetype)codeBlockIntentWithIdentity:(NSInteger)identity
			       languageHint:(nullable NSString *)languageHint
			 nestedInsideIntent:(nullable NSPresentationIntent *)parent;
+ (instancetype)blockQuoteIntentWithIdentity:(NSInteger)identity
			  nestedInsideIntent:(nullable NSPresentationIntent *)parent;
+ (instancetype)thematicBreakIntentWithIdentity:(NSInteger)identity
			    nestedInsideIntent:(nullable NSPresentationIntent *)parent;
+ (instancetype)tableIntentWithIdentity:(NSInteger)identity
			    columnCount:(NSInteger)columnCount
			     alignments:(nullable NSArray *)alignments
		      nestedInsideIntent:(nullable NSPresentationIntent *)parent;
+ (instancetype)tableHeaderRowIntentWithIdentity:(NSInteger)identity
			      nestedInsideIntent:(nullable NSPresentationIntent *)parent;
+ (instancetype)tableRowIntentWithIdentity:(NSInteger)identity
				       row:(NSInteger)row
			nestedInsideIntent:(nullable NSPresentationIntent *)parent;
+ (instancetype)tableCellIntentWithIdentity:(NSInteger)identity
				    column:(NSInteger)column
			  nestedInsideIntent:(nullable NSPresentationIntent *)parent;

/* THE IDENTITY AND THE SHAPE. `identity` is the document's own identifier for this intent and is precisely what
 * `-isEquivalentToPresentationIntent:` IGNORES; `parentIntent` is nil at the top of a document. */
@property (readonly) NSInteger identity;
@property (readonly) NSPresentationIntentKind intentKind;
@property (readonly, nullable, strong) NSPresentationIntent *parentIntent;

/* THE FIELDS EACH KIND CARRIES, all answered by every intent (0 where the kind has none — ours, above): a
 * header's level, a list item's ordinal, a table row's number, a table cell's column, a table's column count and
 * the alignments of those columns ("the value of this property is nil if the intent is not a table"), and a code
 * block's language hint. */
@property (readonly) NSInteger headerLevel;
@property (readonly) NSInteger ordinal;
@property (readonly) NSInteger row;
@property (readonly) NSInteger column;
@property (readonly) NSInteger columnCount;
@property (readonly, nullable) NSArray *columnAlignments;
@property (readonly, copy, nullable) NSString *languageHint;

/* THE DEPTH OF LIST NESTING, computed from the parent chain: "the initial list has an indentation level of 0.
 * Each time you nest a new list, the indentation level for new list increases by 1. All elements within the same
 * list have the same indentation level." */
@property (readonly) NSInteger indentationLevel;

/* ATTRIBUTES, NOT IDENTITY: "two intents are equivalent if their attributes match. This method doesn't consider
 * the [identity] property of the intents when determining their equivalence." */
- (BOOL)isEquivalentToPresentationIntent:(NSPresentationIntent *)other;

@end

NS_ASSUME_NONNULL_END
