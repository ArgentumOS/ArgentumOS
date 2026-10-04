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

/* THE NAMES THIS LIBRARY REGISTERS. The safe unarchiver arrived with W9, and §62.96 added Apple's other five:
 * `NSIsNilTransformerName`, `NSIsNotNilTransformerName`, `NSNegateBooleanTransformerName`,
 * `NSKeyedUnarchiveFromDataTransformerName` and `NSUnarchiveFromDataTransformerName`. Apple declares all six
 * beside the transformer class, which is why these constants live in this header rather than in each class's
 * own — a caller that wants a transformer by name should not have to import a class to spell a name that is
 * used in an Interface Builder-style property list.
 *
 * TWO SENTENCES THAT USED TO BE HERE WERE WRONG, AND ARE CORRECTED RATHER THAN DELETED (§62.96):
 *
 *   1. `NSKeyedUnarchiveFromDataTransformerName` was called "struck on the surface ledger". IT IS NOT — the
 *      ledger lists it OPEN, so the sentence was a claim about a file rather than a reading of it, and the
 *      name it dismissed is the one this unit owed.
 *   2. The paragraph ended "this library ships no deprecated API". That stopped being true the moment §62.93
 *      shipped `NSJSONReadingAllowFragments`, and the ground under it had already been RETIRED on 2026-09-26
 *      (foundation-plan.md row D7, §62.24: deprecated API is IN SCOPE, because running programs written
 *      against the old names is what this library is for). `NSUnarchiveFromDataTransformerName` and the
 *      keyed one are both deprecated on Apple's side and both ship here.
 *
 * THE REGISTRATION HAPPENS AT LOAD, and `+valueTransformerNames` is where that shows: the five new names appear
 * there, which is why they are REGISTERED rather than merely named after their classes — this registry resolves
 * an unknown name by looking for a class of that name, which would make a transformer findable and leave the
 * documented LIST empty. */
extern NSValueTransformerName const NSSecureUnarchiveFromDataTransformerName;
extern NSValueTransformerName const NSIsNilTransformerName;
extern NSValueTransformerName const NSIsNotNilTransformerName;
extern NSValueTransformerName const NSNegateBooleanTransformerName;
extern NSValueTransformerName const NSKeyedUnarchiveFromDataTransformerName;
extern NSValueTransformerName const NSUnarchiveFromDataTransformerName;

NS_ASSUME_NONNULL_END
#endif /* FOUNDATION_NSVALUETRANSFORMER_H */
