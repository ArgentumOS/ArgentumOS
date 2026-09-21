/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSSecureUnarchiveFromDataTransformer — the transformer that UNARCHIVES, and only what it was told to.
 * docs/design/foundation-plan.md §12.3 W9.
 *
 * WHY THIS CLASS EXISTS AT ALL IS THE POINT OF IT: a keyed archive NAMES ITS CLASSES AS STRINGS, so
 * unarchiving DATA SOMEBODY ELSE SUPPLIED is the classic object-injection door — the archive asks for
 * whichever class it likes and a reader that honours the request instantiates it. This transformer is the
 * one that says NO: it decodes through `+allowedTopLevelClasses` and answers nil for an archive that does
 * not stay inside the list.
 *
 * FORWARD IS DECODING AND REVERSE IS ENCODING, which reads backwards until the direction is named: the
 * transformer's "transformed value" is the OBJECT, and the value it transforms is the DATA. So
 * `-transformedValue:` unarchives and `-reverseTransformedValue:` archives, and `+allowsReverseTransformation`
 * is YES because both directions are real.
 *
 * THE NAME RESOLVES WITHOUT ANY REGISTRATION, and it does so by design rather than by a side effect:
 * `NSSecureUnarchiveFromDataTransformerName` IS the class name, and `NSValueTransformer`'s lookup already
 * falls back to treating a name that spells a class as that class. A transformer that had to be registered
 * before it could be named would be one a property list could not refer to.
 */

#ifndef FOUNDATION_NSSECUREUNARCHIVEFROMDATATRANSFORMER_H
#define FOUNDATION_NSSECUREUNARCHIVEFROMDATATRANSFORMER_H

#import <Foundation/NSValueTransformer.h>

NS_ASSUME_NONNULL_BEGIN

@interface NSSecureUnarchiveFromDataTransformer : NSValueTransformer

/* THE ALLOWED CLASSES: the classes an archive this transformer reads may contain. A subclass overrides it to
 * widen or narrow the set — narrowing is the reason to subclass, since the default has to be usable.
 *
 * APPLE PUBLISHES THAT THIS EXISTS AND NOT WHAT IS IN IT, so the default list is THIS LIBRARY'S, registered
 * as such: the value and collection types this library ships, which is exactly the set an archive of
 * property-list-shaped data is made of. A subclass that needs more says so.
 *
 * ALSO `NSArray *` RATHER THAN APPLE'S `NSArray<Class> *`, for the reason the unarchiver delegate's header
 * records: the containers here are not lightweight-generic-parameterized. */
+ (NSArray *)allowedTopLevelClasses;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSSECUREUNARCHIVEFROMDATATRANSFORMER_H */
