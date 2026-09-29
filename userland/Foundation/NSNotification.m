/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSNotification.m — the implementation (W4). MANUAL OWNERSHIP.
 */

#import <Foundation/NSNotification.h>
#import <Foundation/NSString.h>
#import <Foundation/NSDictionary.h>

@implementation NSNotification

/* §C.3 item 4 (M8, 2026-09-29). UNCONDITIONAL, because this family has NO concrete variants here: there is one
 * class, it is the front, and no private subclass's name can leak. The other §C.3 items - a primitive set and the
 * doors over it - have nothing to constrain when there is no second implementation.
 */
- (Class)classForCoder
{
	return [NSNotification class];
}

NSNotificationName const NSAppleEventManagerWillProcessFirstEventNotification = @"NSAppleEventManagerWillProcessFirstEventNotification";
NSNotificationName const NSClassDescriptionNeededForClassNotification = @"NSClassDescriptionNeededForClassNotification";
NSNotificationName const NSExtensionHostDidBecomeActiveNotification = @"NSExtensionHostDidBecomeActiveNotification";
NSNotificationName const NSExtensionHostDidEnterBackgroundNotification = @"NSExtensionHostDidEnterBackgroundNotification";
NSNotificationName const NSExtensionHostWillEnterForegroundNotification = @"NSExtensionHostWillEnterForegroundNotification";
NSNotificationName const NSExtensionHostWillResignActiveNotification = @"NSExtensionHostWillResignActiveNotification";
NSNotificationName const NSMetadataQueryDidFinishGatheringNotification = @"NSMetadataQueryDidFinishGatheringNotification";
NSNotificationName const NSMetadataQueryDidStartGatheringNotification = @"NSMetadataQueryDidStartGatheringNotification";
NSNotificationName const NSMetadataQueryDidUpdateNotification = @"NSMetadataQueryDidUpdateNotification";
NSNotificationName const NSMetadataQueryGatheringProgressNotification = @"NSMetadataQueryGatheringProgressNotification";
NSNotificationName const NSProcessInfoPowerStateDidChangeNotification = @"NSProcessInfoPowerStateDidChangeNotification";
NSNotificationName const NSWillBecomeMultiThreadedNotification = @"NSWillBecomeMultiThreadedNotification";
NSNotificationName const NSDidBecomeSingleThreadedNotification = @"NSDidBecomeSingleThreadedNotification";
NSNotificationName const NSThreadWillExitNotification = @"NSThreadWillExitNotification";
NSNotificationName const NSHTTPCookieManagerAcceptPolicyChangedNotification = @"NSHTTPCookieManagerAcceptPolicyChangedNotification";
NSNotificationName const NSProcessInfoThermalStateDidChangeNotification = @"NSProcessInfoThermalStateDidChangeNotification";
NSNotificationName const NSSystemClockDidChangeNotification = @"NSSystemClockDidChangeNotification";
NSNotificationName const NSSystemTimeZoneDidChangeNotification = @"NSSystemTimeZoneDidChangeNotification";
NSNotificationName const NSUbiquityIdentityDidChangeNotification = @"NSUbiquityIdentityDidChangeNotification";

+ (instancetype)notificationWithName:(NSString *)name object:(nullable id)object
{
	return [[[self alloc] initWithName:name object:object userInfo:nil] autorelease];
}

+ (instancetype)notificationWithName:(NSString *)name
			      object:(nullable id)object
			    userInfo:(nullable NSDictionary *)userInfo
{
	return [[[self alloc] initWithName:name object:object userInfo:userInfo] autorelease];
}

- (instancetype)initWithName:(NSString *)name
		      object:(nullable id)object
		    userInfo:(nullable NSDictionary *)userInfo
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_name = [name copy];
	_object = [object retain];
	_userInfo = [userInfo copy];
	return self;
}

- (NSString *)name
{
	return _name;
}

- (id)object
{
	return _object;
}

- (NSDictionary *)userInfo
{
	return _userInfo;
}

/* IMMUTABLE, SO +1 AND THE SAME OBJECT: the copy of a value that cannot change IS the value, and the
 * caller owns what it is handed (§15.2's rule for the copy family). */
- (id)copy
{
	return [self retain];
}

- (NSString *)description
{
	return [NSString stringWithFormat:@"<NSNotification: %p name=%@ object=%@ userInfo=%@>",
				  self, _name, _object, _userInfo];
}

- (void)dealloc
{
	[_name release];
	[_object release];
	[_userInfo release];
	[super dealloc];
}

@end
