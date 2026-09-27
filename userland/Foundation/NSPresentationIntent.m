/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSPresentationIntent.m — what a piece of an attributed string is (§62.65). MANUAL OWNERSHIP.
 *
 * THE CLASS IS A RECORD WITH A PARENT LINK, and the parent link is what makes it more than a record: the two
 * answers a caller cannot get from the fields alone — the INDENTATION of a nested list and the equivalence of
 * two intents that differ only in identity — are both read off that chain.
 *
 * THE FACTORIES ARE THE ONLY DOORS, so every field is set exactly once, by the factory that knows what its kind
 * means. That is why the store's fields are plain ivars with no setters: an intent that could be half-built (a
 * header with no level, a table with no column count) is a state Apple's API cannot express, and this file does
 * not add one.
 */

#import <Foundation/NSPresentationIntent.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSString.h>

/* THE PRIVATE DESIGNATED INITIALISER: kind, identity and parent, with every kind-specific field zeroed until the
 * factory that knows its meaning fills it in. */
@interface NSPresentationIntent (FNPrivate)
- (instancetype)fnInitWithKind:(NSPresentationIntentKind)kind
		      identity:(NSInteger)identity
			parent:(nullable NSPresentationIntent *)parent;
@end

/* IS THIS KIND A LIST? The indentation rule counts nested LISTS, and only these two kinds are lists — a list
 * ITEM belongs to its list and carries that list's level rather than adding one. */
static BOOL fn_kind_is_list(NSPresentationIntentKind kind)
{
	return kind == NSPresentationIntentKindOrderedList || kind == NSPresentationIntentKindUnorderedList;
}

@implementation NSPresentationIntent

- (instancetype)fnInitWithKind:(NSPresentationIntentKind)kind
		      identity:(NSInteger)identity
			parent:(nullable NSPresentationIntent *)parent
{
	self = [super init];
	if (self != nil) {
		_intentKind = kind;
		_identity = identity;
		_parentIntent = [parent retain];
	}
	return self;
}

- (void)dealloc
{
	[_parentIntent release];
	[_columnAlignments release];
	[_languageHint release];
	[super dealloc];		/* NSObject's -dealloc is what frees the instance */
}

/* ---- THE TWELVE FACTORIES, one per kind --------------------------------------------------------------- */

+ (instancetype)paragraphIntentWithIdentity:(NSInteger)identity
			  nestedInsideIntent:(nullable NSPresentationIntent *)parent
{
	return [[[self alloc] fnInitWithKind:NSPresentationIntentKindParagraph
				    identity:identity
				      parent:parent] autorelease];
}

+ (instancetype)headerIntentWithIdentity:(NSInteger)identity
				   level:(NSInteger)level
		      nestedInsideIntent:(nullable NSPresentationIntent *)parent
{
	NSPresentationIntent *intent = [[self alloc] fnInitWithKind:NSPresentationIntentKindHeader
							  identity:identity
							    parent:parent];

	intent->_headerLevel = level;
	return [intent autorelease];
}

+ (instancetype)orderedListIntentWithIdentity:(NSInteger)identity
			   nestedInsideIntent:(nullable NSPresentationIntent *)parent
{
	return [[[self alloc] fnInitWithKind:NSPresentationIntentKindOrderedList
				    identity:identity
				      parent:parent] autorelease];
}

+ (instancetype)unorderedListIntentWithIdentity:(NSInteger)identity
			     nestedInsideIntent:(nullable NSPresentationIntent *)parent
{
	return [[[self alloc] fnInitWithKind:NSPresentationIntentKindUnorderedList
				    identity:identity
				      parent:parent] autorelease];
}

+ (instancetype)listItemIntentWithIdentity:(NSInteger)identity
				   ordinal:(NSInteger)ordinal
			nestedInsideIntent:(nullable NSPresentationIntent *)parent
{
	NSPresentationIntent *intent = [[self alloc] fnInitWithKind:NSPresentationIntentKindListItem
							  identity:identity
							    parent:parent];

	intent->_ordinal = ordinal;
	return [intent autorelease];
}

+ (instancetype)codeBlockIntentWithIdentity:(NSInteger)identity
			       languageHint:(nullable NSString *)languageHint
			 nestedInsideIntent:(nullable NSPresentationIntent *)parent
{
	NSPresentationIntent *intent = [[self alloc] fnInitWithKind:NSPresentationIntentKindCodeBlock
							  identity:identity
							    parent:parent];

	/* A SNAPSHOT, THROUGH -initWithString: RATHER THAN -copy, for the reason the locking family and the
	 * attributed-string store record: this library's -copy is not a value copy. */
	intent->_languageHint = languageHint != nil ? [[NSString alloc] initWithString:languageHint] : nil;
	return [intent autorelease];
}

+ (instancetype)blockQuoteIntentWithIdentity:(NSInteger)identity
			  nestedInsideIntent:(nullable NSPresentationIntent *)parent
{
	return [[[self alloc] fnInitWithKind:NSPresentationIntentKindBlockQuote
				    identity:identity
				      parent:parent] autorelease];
}

+ (instancetype)thematicBreakIntentWithIdentity:(NSInteger)identity
			    nestedInsideIntent:(nullable NSPresentationIntent *)parent
{
	return [[[self alloc] fnInitWithKind:NSPresentationIntentKindThematicBreak
				    identity:identity
				      parent:parent] autorelease];
}

+ (instancetype)tableIntentWithIdentity:(NSInteger)identity
			    columnCount:(NSInteger)columnCount
			     alignments:(nullable NSArray *)alignments
		      nestedInsideIntent:(nullable NSPresentationIntent *)parent
{
	NSPresentationIntent *intent = [[self alloc] fnInitWithKind:NSPresentationIntentKindTable
							  identity:identity
							    parent:parent];

	intent->_columnCount = columnCount;
	/* THE ALIGNMENTS ARE NUMBERS, one per column — Apple's parameter says "for each NSNumber in the array, set
	 * the value to a value from the enumerated type" — and the store is a COPY, so a caller's mutable array
	 * cannot change under the intent. */
	intent->_columnAlignments = alignments != nil ? [[NSArray alloc] initWithArray:alignments] : nil;
	return [intent autorelease];
}

+ (instancetype)tableHeaderRowIntentWithIdentity:(NSInteger)identity
			      nestedInsideIntent:(nullable NSPresentationIntent *)parent
{
	return [[[self alloc] fnInitWithKind:NSPresentationIntentKindTableHeaderRow
				    identity:identity
				      parent:parent] autorelease];
}

+ (instancetype)tableRowIntentWithIdentity:(NSInteger)identity
				       row:(NSInteger)row
			nestedInsideIntent:(nullable NSPresentationIntent *)parent
{
	NSPresentationIntent *intent = [[self alloc] fnInitWithKind:NSPresentationIntentKindTableRow
							  identity:identity
							    parent:parent];

	intent->_row = row;
	return [intent autorelease];
}

+ (instancetype)tableCellIntentWithIdentity:(NSInteger)identity
				    column:(NSInteger)column
			  nestedInsideIntent:(nullable NSPresentationIntent *)parent
{
	NSPresentationIntent *intent = [[self alloc] fnInitWithKind:NSPresentationIntentKindTableCell
							  identity:identity
							    parent:parent];

	intent->_column = column;
	return [intent autorelease];
}

/* ---- THE FIELDS -------------------------------------------------------------------------------------- */

- (NSInteger)identity { return _identity; }
- (NSPresentationIntentKind)intentKind { return _intentKind; }
- (nullable NSPresentationIntent *)parentIntent { return _parentIntent; }
- (NSInteger)headerLevel { return _headerLevel; }
- (NSInteger)ordinal { return _ordinal; }
- (NSInteger)row { return _row; }
- (NSInteger)column { return _column; }
- (NSInteger)columnCount { return _columnCount; }
- (nullable NSArray *)columnAlignments { return _columnAlignments; }
- (nullable NSString *)languageHint { return _languageHint; }

/* THE DEPTH OF LIST NESTING, FROM APPLE'S OWN SENTENCE. Three clauses decide it, and the third is the one that
 * rules out the obvious implementation: "the initial list has an indentation level of 0", "each time you nest a
 * new list, the indentation level for new list increases by 1", and "ALL ELEMENTS WITHIN THE SAME LIST HAVE THE
 * SAME INDENTATION LEVEL". A count of list ANCESTORS would give the initial list 0 but its own items 1 — the
 * items would not share their list's level — so the rule implemented here is the count of lists in the chain
 * (the receiver's own kind included) MINUS the outermost one. */
- (NSInteger)indentationLevel
{
	NSPresentationIntent *intent = self;
	NSInteger lists = 0;

	while (intent != nil) {
		if (fn_kind_is_list([intent intentKind])) {
			lists++;
		}
		intent = [intent parentIntent];
	}
	return lists > 0 ? lists - 1 : 0;
}

/* ATTRIBUTES, NOT IDENTITY. Every field is compared and the IDENTITY deliberately is not — Apple says so in as
 * many words, and a caller comparing a re-parsed document against the original is asking exactly this question.
 * THE PARENT IS NOT COMPARED EITHER: it is not one of "the intent's attributes" but the chain the intent hangs
 * in, and two intents that differ only in where they sit are the case this method exists for. */
- (BOOL)isEquivalentToPresentationIntent:(NSPresentationIntent *)other
{
	NSArray *mine;
	NSArray *theirs;
	NSString *a;
	NSString *b;

	if (other == nil) {
		return NO;
	}
	if (other == self) {
		return YES;
	}
	if ([other intentKind] != _intentKind ||
	    [other headerLevel] != _headerLevel ||
	    [other ordinal] != _ordinal ||
	    [other row] != _row ||
	    [other column] != _column ||
	    [other columnCount] != _columnCount) {
		return NO;
	}
	/* A NIL columnAlignments AND AN EMPTY ONE ARE THE SAME ATTRIBUTE: Apple's absence rule makes a non-table's
	 * alignments nil, and a table built with none holds nil as well, so the comparison treats them alike. */
	mine = _columnAlignments != nil ? _columnAlignments : [NSArray array];
	theirs = [other columnAlignments] != nil ? [other columnAlignments] : [NSArray array];
	if (![mine isEqualToArray:theirs]) {
		return NO;
	}
	a = _languageHint;
	b = [other languageHint];
	return a == nil ? b == nil : [a isEqualToString:b];
}

@end
