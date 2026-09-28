/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * NSUserNotification + NSUserNotificationAction — THE NOTIFICATION AS A VALUE (plan §62.68), and the third family
 * this thread has closed. APPLE-DEPRECATED, REPLACED BY THE UserNotifications FRAMEWORK, AND THEREFORE IN SCOPE
 * (§62.24): a surface built so that an older application compiles is defeated by excluding exactly the class
 * such an application calls.
 *
 * WHAT THIS PAIR IS: a VALUE OBJECT. Every door below is a stored property with Apple's own ownership — the
 * strings and dates are COPIED, so a caller's mutable string cannot be changed behind the notification's back —
 * plus `-copy`, because Apple's page lists NSCopying among the conformances and a value that cannot be copied is
 * not a value. Nothing here is a decision about where a notification GOES; that is
 * NSUserNotificationCenter's business, and the two are split the way Apple's pages split them.
 *
 * TWO ANNOTATIONS ARE FLAGGED RATHER THAN GUESSED, because Apple's published shape and Apple's own object
 * disagree in one place:
 *
 *   * `-actionButtonTitle` and `-otherButtonTitle` are NON-OPTIONAL in Apple's published shape while a freshly
 *     created notification has neither set. OURS MATCHES THE ANNOTATION, because that annotation is what a
 *     ported application compiles against (a caller may pass the result straight into another nonnull door), and
 *     the fresh-state nil is Apple's behaviour too. The two facts are recorded here rather than reconciled;
 *   * `-contentImage` is an `NSImage *` on Apple's system, and THIS SYSTEM HAS NO NSImage — the drawing layer
 *     is the toolkit's, and Foundation has never owned an image class here. It is declared `id` (§11.6
 *     deviation, and the only workable one: inventing an NSImage in Foundation to satisfy one property would
 *     invent an image type for a system that draws elsewhere). The ownership is still `copy`, so a caller's
 *     mutable image object is copied like everything else.
 */

#import <Foundation/NSObject.h>
#import <Foundation/NSObjCRuntime.h>

NS_ASSUME_NONNULL_BEGIN

@class NSString;
@class NSArray;
@class NSAttributedString;
@class NSDate;
@class NSDateComponents;
@class NSDictionary;
@class NSTimeZone;

/* THE SOUND A NOTIFICATION ASKS FOR BY NAME when it wants the default one, rather than naming a file. */
extern NSString *const NSUserNotificationDefaultSoundName;

/* HOW A NOTIFICATION WAS ACTIVATED — the answer the centre puts on the notification before it tells its
 * delegate, and the reason the delegate's activation door exists. Apple publishes the names and not the values,
 * so the values are ours (§11.6.1 D2) in Apple's own declaration order, with None at zero. */
typedef NS_ENUM(NSInteger, NSUserNotificationActivationType) {
	NSUserNotificationActivationTypeNone = 0,
	NSUserNotificationActivationTypeContentsClicked = 1,
	NSUserNotificationActivationTypeActionButtonClicked = 2,
	NSUserNotificationActivationTypeReplied = 3,
	NSUserNotificationActivationTypeAdditionalActionClicked = 4
};

/* AN EXTRA BUTTON, which is what a notification with more than one action carries. Both of its properties are
 * READ-ONLY: it is built once, through the factory, and then handed around. */
@interface NSUserNotificationAction : NSObject <NSCopying>
{
@private
	NSString *_identifier;
	NSString *_title;
}
+ (instancetype)actionWithIdentifier:(nullable NSString *)identifier title:(nullable NSString *)title;
- (nullable NSString *)identifier;
- (nullable NSString *)title;
@end

@interface NSUserNotification : NSObject <NSCopying>

/* DISPLAY INFORMATION. */
@property (nullable, copy) NSString *title;
@property (nullable, copy) NSString *subtitle;
@property (nullable, copy) NSString *informativeText;
@property (nullable, copy) id contentImage;						/* an NSImage* on Apple's system; see the note above */
@property (nullable, copy) NSString *identifier;
@property (nullable, readonly, copy) NSAttributedString *response;
@property (nullable, copy) NSString *responsePlaceholder;

/* THE BUTTONS. */
@property BOOL hasActionButton;
@property (copy) NSString *actionButtonTitle;					/* see the note above: annotated, not always set */
@property (copy) NSString *otherButtonTitle;					/* ditto */
@property BOOL hasReplyButton;

/* DELIVERY TIMING: when it should go, when it did, and whether it comes back. */
@property (nullable, copy) NSDate *deliveryDate;
@property (nullable, readonly, copy) NSDate *actualDeliveryDate;
@property (nullable, copy) NSDateComponents *deliveryRepeatInterval;
@property (nullable, copy) NSTimeZone *deliveryTimeZone;

/* DELIVERY INFORMATION. `presented` and `remote` are the CENTRE's answers, not the caller's — a notification
 * sets neither, and each has its own getter name because that is what Apple's are. */
@property (readonly, getter=isPresented) BOOL presented;
@property (readonly, getter=isRemote) BOOL remote;
@property (nullable, copy) NSString *soundName;

/* THE ACTIVATION, which the centre fills in when something activates the notification. */
@property (readonly) NSUserNotificationActivationType activationType;
@property (nullable, readonly, copy) NSUserNotificationAction *additionalActivationAction;
@property (nullable, copy) NSArray *additionalActions;

/* WHAT THE CALLER PUTS IN IT. */
@property (nullable, copy) NSDictionary *userInfo;

@end

NS_ASSUME_NONNULL_END
