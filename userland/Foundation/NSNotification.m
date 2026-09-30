/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSNotification.m — the implementation (W4). MANUAL OWNERSHIP.
 */

#import <Foundation/NSNotification.h>
#import <Foundation/NSCoder.h>	/* the NSCoding doors call the coder's methods, not just its type */
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

/* ===================================================================================================
 * THE NSCoding DOORS (§63.14). THE KEYS ARE DEFINED BESIDE THEIR ONLY WRITER AND READER, which is why they
 * are here rather than at the top with the imports: unlike the collections' keys they are shared with nothing
 * (see the header), so their scope IS this pair of methods.
 * =================================================================================================== */
static NSString *const kNameKey = @"NS.name";
static NSString *const kObjectKey = @"NS.object";
static NSString *const kUserInfoKey = @"NS.userInfo";

- (void)encodeWithCoder:(NSCoder *)coder
{
	/* ALL THREE PARTS, INCLUDING THE TWO THAT MAY BE NIL: `-encodeObject:` writes a nil as the archive's
	 * "nothing was here" and an absent value is exactly what a nil object or userInfo means, so the pair of
	 * nils is information rather than an omission. */
	[coder encodeObject:_name forKey:kNameKey];
	[coder encodeObject:_object forKey:kObjectKey];
	[coder encodeObject:_userInfo forKey:kUserInfoKey];
}

- (nullable instancetype)initWithCoder:(NSCoder *)coder
{
	NSString *name = [coder decodeObjectForKey:kNameKey];
	id object = [coder decodeObjectForKey:kObjectKey];
	NSDictionary *userInfo = [coder decodeObjectForKey:kUserInfoKey];

	/* A MISSING NAME IS A CORRUPT ARCHIVE AND RAISES RATHER THAN ANSWERING A NAMELESS NOTIFICATION: the name
	 * is what the class is FOR (`-name` is nonnull in the header), and a nil there would travel as a value no
	 * observer could compare against. The other two are legitimately optional and pass through as nil. */
	if (name == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSNotification: the archive carries no name"];
	}
	/* THROUGH THE DESIGNATED DOOR, so the class's own ownership rules apply on the way back rather than being
	 * trusted from the archive: the name and the userInfo are COPIED here, whatever the coder handed over. */
	return [self initWithName:name object:object userInfo:userInfo];
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
