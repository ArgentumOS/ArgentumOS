/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSValueTransformer.h — a value changed from one representation to another (W2h, §14).
 *
 * THE CLASS IS ABSTRACT AND THE REGISTRY IS THE POINT: an INSTANCE is registered under a name, not
 * the class, which is what lets one general transformer be registered several times with different
 * parameters. Looking a name up also falls back to the class of that name — so a transformer whose
 * name matches its own class needs no registration at all, which is how Cocoa's bindings find one.
 *
 * WHAT IS NOT, named: the built-in subclasses (`NSSecureUnarchiveFromDataTransformer` and the
 * archived-data pair), which belong to a coding stack this library does not have yet.
 */
#ifndef FOUNDATION_NSVALUETRANSFORMER_H
#define FOUNDATION_NSVALUETRANSFORMER_H

#import <Foundation/NSObject.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSString.h>

NS_ASSUME_NONNULL_BEGIN

typedef NSString *NSValueTransformerName;

@interface NSValueTransformer : NSObject

/* NAME -> INSTANCE, and nil UNREGISTERS: Apple's contract, and the reason the registry holds
 * instances rather than classes. */
+ (void)setValueTransformer:(nullable NSValueTransformer *)transformer forName:(NSValueTransformerName)name;
+ (nullable NSValueTransformer *)valueTransformerForName:(NSValueTransformerName)name;
+ (NSArray *)valueTransformerNames;

/* THE TWO QUESTIONS A SUBCLASS ANSWERS ABOUT ITSELF. `allowsReverseTransformation` defaults to NO,
 * which is Apple's documented default; `transformedValueClass` has no documented default, so this
 * library answers NSObject rather than raising — a guess about an unobservable case is worse than
 * a stated one, and a subclass that cares overrides it. */
+ (Class)transformedValueClass;
+ (BOOL)allowsReverseTransformation;

/* THE ABSTRACT BASE RAISES, both ways: Apple's documentation says a subclass must override these,
 * and an abstract method that quietly answers nil would be a transformation that "worked". */
- (nullable id)transformedValue:(nullable id)value;
- (nullable id)reverseTransformedValue:(nullable id)value;

@end

/* THE NAMES THIS LIBRARY REGISTERS. Only one so far, and it arrived with W9: the secure unarchiver, whose
 * NAME is what `-valueTransformerForName:` is asked for. Apple declares these beside the transformer class,
 * which is why this constant lives in this header rather than in NSSecureUnarchiveFromDataTransformer.h —
 * a caller that wants the transformer by name should not have to import the class to spell the name that
 * is used in an Interface Builder-style property list.
 *
 * (`NSKeyedUnarchiveFromDataTransformerName`, the deprecated spelling of the same thing, is NOT declared:
 * it is struck on the surface ledger, and this library ships no deprecated API.) */
extern NSValueTransformerName const NSSecureUnarchiveFromDataTransformerName;

NS_ASSUME_NONNULL_END
#endif /* FOUNDATION_NSVALUETRANSFORMER_H */
