/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUserNotification.m — the value pair (§62.68). MANUAL OWNERSHIP.
 *
 * EVERY PROPERTY IS STORED AND MOST ARE COPIED, which is Apple's own ownership and the whole content of this
 * file: the accessors are synthesised, so the only things written out by hand are the two an object cannot
 * synthesise — `-copy` (the NSCopying conformance Apple's page lists) and the four setters the DELIVERY owns,
 * which live in the internal seam rather than on the public surface.
 *
 * A COPY IS A VALUE COPY AND NOT A SHALLOW ONE, in the sense that matters here: the properties are `copy`
 * properties, so the strings, dates, dictionaries and arrays in the copy are the callers' values SNAPSHOTTED,
 * and changing the original afterwards cannot reach the copy. (Nothing in this library's NSCopying is a value
 * copy by reflex — a `-copy` that shared its mutable state would be the surprising kind.)
 */

#import <Foundation/NSUserNotification.h>
#import <Foundation/FNUserNotification.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSAttributedString.h>
#import <Foundation/NSDate.h>
#import <Foundation/NSDateComponents.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSString.h>
#import <Foundation/NSTimeZone.h>

/* THE NAME A NOTIFICATION USES TO ASK FOR THE DEFAULT SOUND. Apple's page publishes the constant and not its
 * text; this is the spelling Apple's own documentation uses in prose, and it is what a caller compares against
 * rather than a path — nothing in this system resolves it to a file, and a notification that names it is asking
 * for whatever the caller's presentation layer does by default. */
NSString *const NSUserNotificationDefaultSoundName = @"NSUserNotificationDefaultSoundName";

/* THE +1 INITIALISER the factory and -copy share, so that the two family conventions stay apart: a factory
 * answers an AUTORELEASED object and -copy answers an OWNED one. Writing the factory's body twice — once
 * autoreleasing and once not — is how a `-copy` that hands back a borrowed object gets written, and THAT is the
 * defect this file was built with: the notification's own setter stored the result as if it owned it, the
 * autorelease pool drained, and the next `release` of the stored action ran on freed memory. */
@interface NSUserNotificationAction ()
- (instancetype)fnInitWithIdentifier:(nullable NSString *)identifier title:(nullable NSString *)title;
@end

@implementation NSUserNotificationAction

+ (instancetype)actionWithIdentifier:(nullable NSString *)identifier title:(nullable NSString *)title
{
	return [[[self alloc] fnInitWithIdentifier:identifier title:title] autorelease];
}

- (instancetype)fnInitWithIdentifier:(nullable NSString *)identifier title:(nullable NSString *)title
{
	self = [super init];
	if (self != nil) {
		_identifier = [identifier copy];
		_title = [title copy];
	}
	return self;
}

- (nullable NSString *)identifier { return _identifier; }
- (nullable NSString *)title { return _title; }

- (void)dealloc
{
	[_identifier release];
	[_title release];
	[super dealloc];
}

- (id)copy
{
	/* +1, WHICH IS WHAT -copy OWES (plan §15.2's owned family) — not the factory, which answers +0. */
	return [[NSUserNotificationAction alloc] fnInitWithIdentifier:_identifier title:_title];
}

@end

@implementation NSUserNotification

/* THE SIX THE DELIVERY OWNS ARE SYNTHESISED EXPLICITLY, because each has a hand-written getter (its own name,
 * from Apple's `getter=` for two of them): when a getter is written out, the compiler stops synthesising the
 * storage, and these six must still HAVE storage for the seam to write. */
/* EVERY STORED PROPERTY IS SYNTHESISED EXPLICITLY, including the sixteen whose accessors the compiler would
 * write anyway: the acceptance gate counts `@synthesize` (and written-out accessors) as the IMPLEMENTATION of a
 * declared selector, so an implicit synthesis is a declaration the library appears not to define. */
@synthesize title = _title;
@synthesize subtitle = _subtitle;
@synthesize informativeText = _informativeText;
@synthesize contentImage = _contentImage;
@synthesize identifier = _identifier;
@synthesize responsePlaceholder = _responsePlaceholder;
@synthesize hasActionButton = _hasActionButton;
@synthesize actionButtonTitle = _actionButtonTitle;
@synthesize otherButtonTitle = _otherButtonTitle;
@synthesize hasReplyButton = _hasReplyButton;
@synthesize deliveryDate = _deliveryDate;
@synthesize deliveryRepeatInterval = _deliveryRepeatInterval;
@synthesize deliveryTimeZone = _deliveryTimeZone;
@synthesize soundName = _soundName;
@synthesize additionalActions = _additionalActions;
@synthesize userInfo = _userInfo;
@synthesize presented = _presented;
@synthesize remote = _remote;
@synthesize activationType = _activationType;
@synthesize response = _response;
@synthesize actualDeliveryDate = _actualDeliveryDate;
@synthesize additionalActivationAction = _additionalActivationAction;

/* THE ACCESSORS ARE WRITTEN OUT RATHER THAN LEFT TO THE COMPILER, which is this tree's convention (see
 * NSDateComponents) and, concretely, what the acceptance gate reads: a written accessor is the IMPLEMENTATION of
 * the selector a `@property` declares, so an implicitly synthesised one looks like a declaration the library
 * never defines. The object setters share ONE shape — snapshot through the class's own -copy, release what was
 * there, keep the snapshot — because that IS Apple's `copy` ownership. */
static void fn_set_copy(id *slot, id value)
{
	id kept = [value copy];

	[*slot release];
	*slot = kept;
}

- (nullable NSString *)title { return _title; }
- (void)setTitle:(nullable NSString *)value { fn_set_copy((id *)&_title, value); }
- (nullable NSString *)subtitle { return _subtitle; }
- (void)setSubtitle:(nullable NSString *)value { fn_set_copy((id *)&_subtitle, value); }
- (nullable NSString *)informativeText { return _informativeText; }
- (void)setInformativeText:(nullable NSString *)value { fn_set_copy((id *)&_informativeText, value); }
- (nullable id)contentImage { return _contentImage; }
- (void)setContentImage:(nullable id)value { fn_set_copy((id *)&_contentImage, value); }
- (nullable NSString *)identifier { return _identifier; }
- (void)setIdentifier:(nullable NSString *)value { fn_set_copy((id *)&_identifier, value); }
- (nullable NSString *)responsePlaceholder { return _responsePlaceholder; }
- (void)setResponsePlaceholder:(nullable NSString *)value { fn_set_copy((id *)&_responsePlaceholder, value); }
- (BOOL)hasActionButton { return _hasActionButton; }
- (void)setHasActionButton:(BOOL)value { _hasActionButton = value; }
- (NSString *)actionButtonTitle { return _actionButtonTitle; }
- (void)setActionButtonTitle:(NSString *)value { fn_set_copy((id *)&_actionButtonTitle, value); }
- (NSString *)otherButtonTitle { return _otherButtonTitle; }
- (void)setOtherButtonTitle:(NSString *)value { fn_set_copy((id *)&_otherButtonTitle, value); }
- (BOOL)hasReplyButton { return _hasReplyButton; }
- (void)setHasReplyButton:(BOOL)value { _hasReplyButton = value; }
- (nullable NSDate *)deliveryDate { return _deliveryDate; }
- (void)setDeliveryDate:(nullable NSDate *)value { fn_set_copy((id *)&_deliveryDate, value); }
- (nullable NSDateComponents *)deliveryRepeatInterval { return _deliveryRepeatInterval; }
- (void)setDeliveryRepeatInterval:(nullable NSDateComponents *)value
{
	fn_set_copy((id *)&_deliveryRepeatInterval, value);
}
- (nullable NSTimeZone *)deliveryTimeZone { return _deliveryTimeZone; }
- (void)setDeliveryTimeZone:(nullable NSTimeZone *)value { fn_set_copy((id *)&_deliveryTimeZone, value); }
- (nullable NSString *)soundName { return _soundName; }
- (void)setSoundName:(nullable NSString *)value { fn_set_copy((id *)&_soundName, value); }
- (nullable NSArray *)additionalActions { return _additionalActions; }
- (void)setAdditionalActions:(nullable NSArray *)value { fn_set_copy((id *)&_additionalActions, value); }
- (nullable NSDictionary *)userInfo { return _userInfo; }
- (void)setUserInfo:(nullable NSDictionary *)value { fn_set_copy((id *)&_userInfo, value); }

/* A NOTIFICATION STARTS EMPTY, WHICH IS WHAT THE READ-ONLY PROPERTIES THEN REPORT: not presented, not remote,
 * not activated, with no delivery date and no response. The five are the delivery's record, and none of them is
 * a caller's to set. */
- (BOOL)isPresented { return _presented; }
- (BOOL)isRemote { return _remote; }
- (NSUserNotificationActivationType)activationType { return _activationType; }
- (nullable NSAttributedString *)response { return _response; }
- (nullable NSDate *)actualDeliveryDate { return _actualDeliveryDate; }
- (nullable NSUserNotificationAction *)additionalActivationAction { return _additionalActivationAction; }

- (void)dealloc
{
	[_title release];
	[_subtitle release];
	[_informativeText release];
	[_contentImage release];
	[_identifier release];
	[_response release];
	[_responsePlaceholder release];
	[_actionButtonTitle release];
	[_otherButtonTitle release];
	[_deliveryDate release];
	[_actualDeliveryDate release];
	[_deliveryRepeatInterval release];
	[_deliveryTimeZone release];
	[_soundName release];
	[_additionalActivationAction release];
	[_additionalActions release];
	[_userInfo release];
	[super dealloc];		/* NSObject's -dealloc is what frees the instance */
}

/* THE VALUE COPY: every field, through its own setter, so the copy properties snapshot what they are given. The
 * delivery's record is copied too — a copy of a delivered notification is a notification that was delivered. */
- (id)copy
{
	NSUserNotification *copy = [[NSUserNotification alloc] init];

	[copy setTitle:_title];
	[copy setSubtitle:_subtitle];
	[copy setInformativeText:_informativeText];
	[copy setContentImage:_contentImage];
	[copy setIdentifier:_identifier];
	[copy setResponsePlaceholder:_responsePlaceholder];
	[copy setHasActionButton:_hasActionButton];
	[copy setActionButtonTitle:_actionButtonTitle];
	[copy setOtherButtonTitle:_otherButtonTitle];
	[copy setHasReplyButton:_hasReplyButton];
	[copy setDeliveryDate:_deliveryDate];
	[copy setDeliveryRepeatInterval:_deliveryRepeatInterval];
	[copy setDeliveryTimeZone:_deliveryTimeZone];
	[copy setSoundName:_soundName];
	[copy setAdditionalActions:_additionalActions];
	[copy setUserInfo:_userInfo];
	[copy fnSetPresented:_presented];
	[copy fnSetActualDeliveryDate:_actualDeliveryDate];
	[copy fnSetActivationType:_activationType];
	[copy fnSetResponse:_response];
	[copy fnSetAdditionalActivationAction:_additionalActivationAction];
	return copy;
}

@end

/* THE DELIVERY'S OWN SETTERS — the seam's implementation, defined here because the storage is here and because a
 * category in another file could not reach these ivars. */
@implementation NSUserNotification (FNDelivery)

- (void)fnSetPresented:(BOOL)presented { _presented = presented; }

- (void)fnSetActualDeliveryDate:(nullable NSDate *)date
{
	NSDate *kept = [date copy];

	[_actualDeliveryDate release];
	_actualDeliveryDate = kept;
}

- (void)fnSetActivationType:(NSUserNotificationActivationType)type { _activationType = type; }

- (void)fnSetResponse:(nullable NSAttributedString *)response
{
	NSAttributedString *kept = [response copy];

	[_response release];
	_response = kept;
}

- (void)fnSetAdditionalActivationAction:(nullable NSUserNotificationAction *)action
{
	NSUserNotificationAction *kept = [action copy];

	[_additionalActivationAction release];
	_additionalActivationAction = kept;
}

@end
