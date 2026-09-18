/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * fnpredicate.h — the PRIVATE half of the predicate family: the comparison leaf and the operator
 * table the grammar produces. docs/design/foundation-plan.md, F11b.
 *
 * It is a header of its own for the same reason fninvoke.h and fnmethodsignature.h are: the
 * OBJECT MODEL (nspredicate.m) and the GRAMMAR (npredicateformat.m) are two files, and they must
 * agree about one thing — the shape of a comparison.
 *
 * THE OPERATORS LIVE IN ONE ENUM, so the PARSER that produces one and the RENDERER that prints it
 * back cannot drift apart. That matters because -predicateFormat is what the round trip is
 * checked against: parsing a rendered predicate must give the same answer as parsing the
 * original.
 *
 * THE COMPARISON LEAF IS PRIVATE because its Cocoa spelling is NSComparisonPredicate, whose whole
 * API is expressed in NSExpression — and NSExpression is refused (§5). This is the node the
 * GRAMMAR produces, not an NSExpression-shaped API.
 */

#ifndef FOUNDATION_FNPREDICATE_H
#define FOUNDATION_FNPREDICATE_H

#import <foundation/NSPredicate.h>

NS_ASSUME_NONNULL_BEGIN

typedef enum {
	FNCompareEqual = 0,
	FNCompareNotEqual,
	FNCompareLess,
	FNCompareLessOrEqual,
	FNCompareGreater,
	FNCompareGreaterOrEqual,
	FNCompareContains,
	FNCompareBeginsWith,
	FNCompareEndsWith,
	FNCompareLike
} FNCompareOperator;

/*
 * THE GRAMMAR'S ENTRY POINT, as a C FUNCTION rather than a category method on NSPredicate. The
 * two methods that expose it belong to the PRIMARY implementation (a category implementing what
 * the class's own header declares is a warning, and rightly so), so the parser is reached
 * through this instead.
 *
 * It RAISES on a bad format — naming the construct, including the ones the grammar refuses — so
 * it never answers nil.
 */
NSPredicate *FNPredicateParse(NSString *format);

/*
 * One comparison. Each side is EITHER a key path (resolved through KVC, F9) OR a literal — a
 * string, a number, or nothing at all (`NULL`/`NIL`, for which BOTH the path and the literal are
 * nil). `SELF` is a path that names the object itself, and the leaf knows that spelling.
 */
@interface FNPredicateComparison : NSPredicate
{
	NSString *_leftPath;
	id _leftLiteral;
	NSString *_rightPath;
	id _rightLiteral;
	FNCompareOperator _op;
	BOOL _caseInsensitive;
}
- (instancetype)initWithLeftPath:(nullable NSString *)leftPath
		     leftLiteral:(nullable id)leftLiteral
			operator:(FNCompareOperator)op
		       rightPath:(nullable NSString *)rightPath
		    rightLiteral:(nullable id)rightLiteral
		 caseInsensitive:(BOOL)caseInsensitive;
@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_FNPREDICATE_H */
