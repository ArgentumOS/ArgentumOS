/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSPersonNameComponents — a name, taken apart. docs/design/foundation-plan.md §12.3 W11.
 *
 * A BAG OF SEVEN MAYBE-SET STRINGS, and the "maybe" is the whole design. Every component is
 * optional because names are not a fixed shape: a mononym has no family name, a culture may put the
 * family name first, and a piece the parser could not identify is absent rather than empty. So nil
 * means NOT PRESENT and never "present and blank" — the same distinction NSDateComponents draws with
 * its NSDateComponentUndefined sentinel, except that for a string the natural sentinel IS nil.
 *
 * THE SEVENTH COMPONENT IS NOT A STRING. `phoneticRepresentation` is another
 * NSPersonNameComponents — the same name spelled the way it is SAID — which is why this class can
 * nest. It is an owned object, so it is copied on assignment and released on dealloc, and copying a
 * components object copies that one too (no shared mutable state between two "copies", which is the
 * contract §15.2 measured family-wide).
 *
 * NO @property ANYWHERE IN THIS LIBRARY (the plan, §7): accessors are methods, so
 * `[components setGivenName:@"Ada"]` and `[components givenName]`. The thread of each property's
 * meaning is Apple's own wording for it, kept because a name component is exactly the kind of thing
 * where the one-line gloss IS the specification.
 *
 * WHAT IS NOT HERE, named: `NSPersonNameComponentsFormatter` — the class that renders these
 * components — is a DIFFERENT class in the ledger, and this one lands first because the formatter's
 * input type has to exist before the formatter has anything to format.
 */

#ifndef FOUNDATION_NSPERSONNAMECOMPONENTS_H
#define FOUNDATION_NSPERSONNAMECOMPONENTS_H

#import <Foundation/NSObject.h>
#import <Foundation/NSCoding.h>

NS_ASSUME_NONNULL_BEGIN

@interface NSPersonNameComponents : NSObject <NSCopying, NSSecureCoding>
{
	NSString *_namePrefix;
	NSString *_givenName;
	NSString *_middleName;
	NSString *_familyName;
	NSString *_nameSuffix;
	NSString *_nickname;
	NSPersonNameComponents *_phoneticRepresentation;
}

/* The portion of a name's full form of address that precedes the name itself (for example, “Dr.”). */
- (nullable NSString *)namePrefix;
- (void)setNamePrefix:(nullable NSString *)value;

/* Name bestowed upon an individual to differentiate them from other members of a group that share a
 * family name (for example, “Ada” in “Ada Lovelace”). */
- (nullable NSString *)givenName;
- (void)setGivenName:(nullable NSString *)value;

/* Secondary name bestowed upon an individual to differentiate them from others that have the same
 * given name. */
- (nullable NSString *)middleName;
- (void)setMiddleName:(nullable NSString *)value;

/* Name bestowed upon an individual to denote membership in a group or family. */
- (nullable NSString *)familyName;
- (void)setFamilyName:(nullable NSString *)value;

/* The portion of a name's full form of address that follows the name itself (for example, “Jr.”). */
- (nullable NSString *)nameSuffix;
- (void)setNameSuffix:(nullable NSString *)value;

/* Name substituted for the purposes of familiarity (for example, “Bill” for “William”). */
- (nullable NSString *)nickname;
- (void)setNickname:(nullable NSString *)value;

/* The phonetic representation of these name components — the same name spelled as it is spoken.
 * A components object is its own type here, so the relation is recursive and is OWNED. */
- (nullable NSPersonNameComponents *)phoneticRepresentation;
- (void)setPhoneticRepresentation:(nullable NSPersonNameComponents *)value;

/* `-copy` (not `-copyWithZone:`): §11.6.1 D1, the copying model this library ships. */
- (id)copy;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSPERSONNAMECOMPONENTS_H */
