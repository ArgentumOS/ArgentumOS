/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_bundle_payload — the SHARED LIBRARY a fixture bundle names as its NSPrincipalClass.
 *
 * It exists so that -load's POSITIVE path can be asserted rather than assumed: dlopen has to open a real shared
 * object, and the class below has to be findable by objc_getClass afterwards. THE CLASS NAME IS THE ONE THE
 * Fixture's Info.plist NAMES, and Renaming one without the other is exactly the failure this fixture would then
 * report.
 */

#import <Foundation/NSObject.h>
#import <Foundation/NSString.h>

@interface BundleFixturePrincipal : NSObject
- (NSString *)fixtureAnswer;
@end

@implementation BundleFixturePrincipal

- (NSString *)fixtureAnswer
{
	return @"the payload answered";
}

@end
