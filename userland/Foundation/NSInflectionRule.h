/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * NSInflectionRule — WHAT WOULD DECIDE AN INFLECTION, AS A VALUE (§62.79), and the last-but-one row of
 * `Fundamentals / Automatic grammar agreement`.
 *
 * THE RULE IS A VALUE AND THE ENGINE IS NOT HERE, which is the same boundary NSMorphology.h states one level
 * down: nothing in this system performs grammar agreement, so `+canInflectLanguage:` and
 * `+canInflectPreferredLocalization` BOTH ANSWER NO - and they answer it as a fact about this system rather than
 * as an unimplemented door. What is real is the rule OBJECT: an automatic rule a caller can hold, copy, archive
 * and pass around, and an explicit rule that CARRIES the morphology it would inflect with.
 *
 * `+automaticRule` ANSWERS A VALUE rather than refusing, on the same reading §62.78 used for `+userMorphology`:
 * Apple's is "an inflection rule that performs automatic grammar agreement with default transformations", and a
 * system that can inflect no language still has such a rule as a thing to name - so the object exists and the
 * capable doors above say NO about what it can do.
 */

#import <Foundation/NSObject.h>
#import <Foundation/NSCoding.h>

NS_ASSUME_NONNULL_BEGIN

@class NSString;
@class NSMorphology;

@interface NSInflectionRule : NSObject <NSCopying, NSSecureCoding>

/* THE AUTOMATIC RULE: a value that names "let the system decide", which in this system means a rule nothing
 * applies. A FRESH instance every call, because the rule is mutable-by-subclass and a shared one would be a
 * shared answer. */
+ (NSInflectionRule *)automaticRule;

/* NO, AND THE GROUND IS THE ABSENCE OF AN AGREEMENT MODEL rather than an unimplemented door: no language's
 * grammar is agreed here. */
+ (BOOL)canInflectLanguage:(NSString *)language;
+ (BOOL)canInflectPreferredLocalization;

@end

@interface NSInflectionRuleExplicit : NSInflectionRule
{
@private
	NSMorphology *_morphology;
}

/* THE RULE THAT SAYS WHAT TO INFLECT WITH, carrying the morphology as a value - the one rule in this system that
 * is not merely a name. */
- (instancetype)initWithMorphology:(NSMorphology *)morphology;
- (NSMorphology *)morphology;

@end

NS_ASSUME_NONNULL_END
