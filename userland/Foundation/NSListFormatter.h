/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSListFormatter — a list, joined the way the locale joins one.
 * docs/design/foundation-plan.md §12.3 W11.
 *
 * THE WHOLE CLASS IS A BINDING, AND THAT IS THE POINT. "a, b and c" versus "a, b, and c" versus
 * "a、b、c" is not a rule anyone should write by hand: the conjunction and the separators are CLDR
 * list-pattern DATA, and ICU already ships it. So this class opens an ICU list formatter with the
 * locale's list pattern and hands the items to it. §11.4 item 3 names this family for exactly this
 * reason — the highest fidelity per unit of work in the ledger, because the data already exists.
 * WHAT IS OURS is the OBJECT (the settings, the ownership, the error contract); the grammar is ICU's.
 *
 * THE ITEM FORMATTER IS COMPOSITION, NOT A SECOND FORMATTER. `itemFormatter`, when set, is asked to
 * render EACH item — so a list of dates becomes a list of date strings and is then joined. With none
 * set, a string item is used as it is and anything else is asked for its `-description`, which is
 * Apple's shape: the class formats the LIST, and the itemFormatter is how a caller says what the
 * items are.
 *
 * NULLABILITY ON THE ICU-BACKED FAMILY (stated rather than inherited silently): a formatter that
 * could not be opened, or whose ICU call failed, answers nil. `NSDateFormatter` declares its whole
 * ICU-backed surface this way, and this class follows that precedent rather than inventing a
 * different one for the same failure.
 *
 * NO @property ANYWHERE IN THIS LIBRARY (the plan, §7): accessors are methods.
 */

#ifndef FOUNDATION_NSLISTFORMATTER_H
#define FOUNDATION_NSLISTFORMATTER_H

#import <Foundation/NSFormatter.h>

@class NSArray;
@class NSLocale;
@class NSString;

NS_ASSUME_NONNULL_BEGIN

@interface NSListFormatter : NSFormatter
{
	NSLocale *_locale;		/* nil means "the current one", read at use */
	NSFormatter *_itemFormatter;	/* owned: -setItemFormatter: copies */
	void *_formatter;		/* UListFormatter *, rebuilt by every setting */
}

/* Creates a formatted string for an array of items. Each item is rendered by `itemFormatter` when
 * one is set; without it, an NSString is used as it is and anything else is asked for its
 * `-description`. */
- (nullable NSString *)stringFromItems:(NSArray *)items;

/* NSFormatter's door, narrowed: the object value this class accepts IS an array. An object of any
 * other class answers nil, which is the documented convention for "not my kind of value". */
- (nullable NSString *)stringForObjectValue:(nullable id)object;

/* The same list, joined, in the CURRENT locale and with no item formatter. Apple documents it as
 * "the list format specific to the current locale", so there is no second way to say "default".
 * Apple's declaration is `NSArray<NSString *> *`; this tree's NSArray carries no type parameters, so
 * the element type is stated in prose here rather than as a generic argument. */
+ (nullable NSString *)localizedStringByJoiningStrings:(NSArray *)strings;

/* An object that formats each item in the list. Copied on assignment (Apple declares it `copy`): a
 * caller that sets a formatter and then mutates it must not thereby rewrite this list's rendering. */
- (nullable NSFormatter *)itemFormatter;
- (void)setItemFormatter:(nullable NSFormatter *)value;

/* The locale to use. This one is RESETTABLE: a nil locale is not "no locale", it means "the current
 * locale" — so the getter answers `+[NSLocale currentLocale]` when nothing was set, and a setter
 * given nil returns the object to that state. */
- (NSLocale *)locale;
- (void)setLocale:(nullable NSLocale *)value;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSLISTFORMATTER_H */
