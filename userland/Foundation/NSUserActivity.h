/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * NSUserActivity — the activity object behind Handoff, Spotlight indexing and Siri continuity (§62.89), with
 * `NSUserActivityDelegate`. This closes `App Support / Activity Sharing`.
 *
 * **THE ACTIVITY IS REAL; THE OTHER DEVICE IS NOT.** An activity's own state is entirely local — its type, its
 * title, its `userInfo`, the keys it requires, whether it needs saving, and the lifecycle Apple documents
 * (`-becomeCurrent`, `-resignCurrent`, `-invalidate`) — and all of that is implemented and enforced here. What
 * this system does not have is a SECOND DEVICE: there is nowhere to hand an activity to, so the two doors that
 * exist for continuity answer the truth —
 *   * `-getContinuationStreamsWithCompletionHandler:` reports `NSUserActivityConnectionUnavailableError` (an
 *     error code this library already declares in NSError.h), because there is no connection to another device
 *     to open streams with;
 *   * and the delegate's `-userActivityWasContinued:` and `-userActivity:didReceiveInputStream:outputStream:`
 *     arrive from the other device, so they come through the internal seam in `FNUserActivity.h` — the device
 *     §62.85 introduced — rather than being declarations nobody can call.
 *
 * THREE PIECES OF APPLE'S CONTRACT ARE ENFORCED RATHER THAN DESCRIBED, and each is a check in the probe:
 *   * **AN INVALIDATED ACTIVITY CANNOT BECOME CURRENT AGAIN** (Apple, in words: "calling `-becomeCurrent` after
 *     `-invalidate` has no effect");
 *   * **ONLY ONE ACTIVITY IS CURRENT AT A TIME** — Apple's rule, and this class keeps it by resigning the
 *     process's previous current activity when another becomes current;
 *   * **SETTING `needsSave` ASKS THE DELEGATE TO SAVE, AND SAVING CLEARS IT** — that is the documented cycle
 *     (`-userActivityWillSave:` is "where you update the `userInfo` dictionary"), and it is why `needsSave` is
 *     the one piece of state this class changes on its own.
 *
 * A NOTE ON `-init` (the INHERITED one): Apple's `-init` uses the FIRST activity type declared in the
 * application's Info.plist. **NO BUNDLE IN THIS SYSTEM DECLARES ACTIVITY TYPES** — there is no `NSUserActivityTypes`
 * manifest here — so the inherited `-init` cannot name one, and it RAISES with that ground instead of producing an
 * activity of no type. Use `-initWithActivityType:`.
 *
 * AND ONE NAME THAT IS NOT API, SAID OUT LOUD BECAUSE IT IS EASY TO ASSUME: there is NO `-isCurrent` door. Apple
 * manages the current activity through the calls above and documents no public getter, so none is declared here;
 * a caller tracks what it made current.
 *
 * APPLE PUBLISHES `NSUserActivityTypeBrowsingWeb`'s NAME AND NOT ITS VALUE, so the value is this library's
 * standing spelling for such a constant — the name itself (§11.6.1 D2).
 */

#import <Foundation/NSObject.h>

NS_ASSUME_NONNULL_BEGIN

@class NSDate;
@class NSDictionary;
@class NSError;
@class NSInputStream;
@class NSOutputStream;
@class NSSet;
@class NSString;
@class NSURL;
@class NSUserActivity;

/* Apple declares this as a TYPE ALIAS for a string in its Swift interface; in Objective-C it IS `NSString`, and
 * this typedef is how the tree ships such a name (NSURL.h does the same for its ubiquitous-item types). */
typedef NSString *NSUserActivityPersistentIdentifier;

/* THE ONE ACTIVITY TYPE APPLE ITSELF DEFINES: continuing it opens a web page. */
FOUNDATION_EXPORT NSString * const NSUserActivityTypeBrowsingWeb;

@protocol NSUserActivityDelegate <NSObject>
@optional

/* THE ACTIVITY IS ABOUT TO BE SAVED — the moment to update its `userInfo`. Reached here by setting
 * `-needsSave`, because that is the only saver this system has. */
- (void)userActivityWillSave:(NSUserActivity *)userActivity;

/* THE ACTIVITY WAS CONTINUED ON ANOTHER DEVICE: this device is being told it succeeded. Delivered through the
 * seam (see the file's note) because there is no other device here. */
- (void)userActivityWasContinued:(NSUserActivity *)userActivity;

/* Streams from the continuing device are available. Delivered through the seam for the same reason; the streams
 * are supplied by the seam's caller, never manufactured here. */
- (void)userActivity:(NSUserActivity *)userActivity
    didReceiveInputStream:(NSInputStream *)inputStream
	     outputStream:(NSOutputStream *)outputStream;

@end

@interface NSUserActivity : NSObject
{
@private
	id _activityType;	/* retained: it is the activity's identity and cannot change */
	id _title;
	id _userInfo;
	id _requiredUserInfoKeys;
	id _delegate;		/* NOT retained, as Apple's weak delegate is not */
	id _webpageURL;
	id _referrerURL;
	id _expirationDate;
	id _persistentIdentifier;	/* retained: the cross-device identity, a string */
	id _targetContentIdentifier;
	id _keywords;			/* retained: a copied NSSet of NSString keywords */
	BOOL _needsSave;
	BOOL _supportsContinuationStreams;
	BOOL _eligibleForHandoff;
	BOOL _eligibleForSearch;
	BOOL _eligibleForPublicIndexing;
	BOOL _eligibleForPrediction;
	BOOL _invalidated;
	BOOL _current;
}

/* THE ONLY USEFUL INITIALISER. A nil or empty type is a PROGRAMMING ERROR and raises: an activity with no type
 * is not an activity Apple's system could ever continue, and the type is read-only, so there is no second chance
 * to get it right. */
- (instancetype)initWithActivityType:(nullable NSString *)activityType;

/* The activity's identity. Read-only, as Apple declares it. */
- (NSString *)activityType;

/* The user-visible title and the payload the other side would receive. Both are COPIED on the way in, so a
 * caller's later mutation is not the activity's business. */
- (nullable NSString *)title;
- (void)setTitle:(nullable NSString *)aTitle;
- (nullable NSDictionary *)userInfo;
- (void)setUserInfo:(nullable NSDictionary *)aDictionary;

/* MERGE IN MORE PAYLOAD: a key present in both takes the value from `otherDictionary`, which is what Apple's
 * documentation says. There is no remove door — an activity's payload is built up, never pruned, and a key that
 * must disappear is set to nil through `-setUserInfo:`. */
- (void)addUserInfoEntriesFromDictionary:(NSDictionary *)otherDictionary;

/* The keys the activity REQUIRES to be continuable. Apple's rule is that the payload should be small; this set
 * is how an app says which part of it matters, and it is copied. */
- (nullable NSSet *)requiredUserInfoKeys;
- (void)setRequiredUserInfoKeys:(nullable NSSet *)aSet;

/* WHETHER THE STATE NEEDS SAVING. SETTING IT TO YES ASKS THE DELEGATE TO SAVE (see the file's note), and the save
 * clears it — so a caller that sets it on every keystroke gets one delegate message per save rather than one per
 * keystroke. */
- (BOOL)needsSave;
- (void)setNeedsSave:(BOOL)flag;

/* NOT RETAINED: the activity does not keep its delegate alive, as Apple's weak delegate does not. */
- (nullable id <NSUserActivityDelegate>)delegate;
- (void)setDelegate:(nullable id <NSUserActivityDelegate>)anObject;

/* The browsing URL the activity stands for, and the page it was reached from — the two payload pieces Apple
 * treats as first-class rather than as `userInfo` entries. */
- (nullable NSURL *)webpageURL;
- (void)setWebpageURL:(nullable NSURL *)aURL;
- (nullable NSURL *)referrerURL;
- (void)setReferrerURL:(nullable NSURL *)aURL;

/* WHEN THE ACTIVITY STOPS BEING USEFUL. Stored and answered; nothing here expires it, because there is no
 * system doing the expiring. */
- (nullable NSDate *)expirationDate;
- (void)setExpirationDate:(nullable NSDate *)aDate;

/* Whether the continuing device may ask for streams back to this one. See
 * `-getContinuationStreamsWithCompletionHandler:` for what asking actually gets here. */
- (BOOL)supportsContinuationStreams;
- (void)setSupportsContinuationStreams:(BOOL)flag;

/* --- THE REST OF THE VALUE MODEL: identifiers, keywords and the eligibility flags -------------------- */
/* EVERY DOOR BELOW STORES AND ANSWERS A VALUE THE ACTIVITY ITSELF OWNS, exactly as the title and the URLs
 * above do, and every value is COPIED on the way in. What none of them have here is the ENGINE that Apple
 * reads them with: there is no handoff daemon, no Spotlight index and no Siri prediction service in this
 * system, so the flags describe the activity truthfully and stop there rather than pretending to steer a
 * service that does not exist. The engines an engine-door would need are named at the doors that need them. */

/* A STABLE IDENTITY that survives across devices and launches — Apple's `NSUserActivityPersistentIdentifier`,
 * which this header's typealias makes an `NSString`. Copied. */
- (nullable NSUserActivityPersistentIdentifier)persistentIdentifier;
- (void)setPersistentIdentifier:(nullable NSUserActivityPersistentIdentifier)anIdentifier;

/* WHAT ON-SCREEN CONTENT THE ACTIVITY POINTS AT, and which external media item it names. Both are stored
 * strings the activity owns; nothing here resolves either to anything. Copied. */
- (nullable NSString *)targetContentIdentifier;
- (void)setTargetContentIdentifier:(nullable NSString *)aString;

/* ⚠ `-externalMediaContentIdentifier` / `-setExternalMediaContentIdentifier:` AND
 * `-suggestedInvocationPhrase` / `-setSuggestedInvocationPhrase:` WERE HERE AND ARE GONE (§63.57, user decision
 * dec-d2dfbe0c080f14c2). **MEASURED, AND THE MEASUREMENT IS THE WHOLE CASE: macOS 14.5's `NSUserActivity.h` in
 * the corpus is COMPLETE — 9,625 bytes, 17 properties, ending at `NS_HEADER_AUDIT_END` — and holds NEITHER
 * name.** They are Apple's API on iOS, which is not the platform this library declares, so they fail the
 * fidelity bar (§11) here. The failure mode this guards against is worth naming: the header is not TRUNCATED,
 * so "the file looks short" is not what proves the absence — THE ENDING is what proves it.
 *
 * WHAT THE CLASS KEEPS IS WHAT IT ACTUALLY OWNS: the activity type, title, userInfo, the cross-device identity
 * and the on-screen content identifier, expiration, the keyword set, the eligibility flags and the delegate.
 * The two removed pairs stored a string and answered it, and nothing in this system resolves either to a media
 * item or to a spoken phrase — their only content was "a stored string", which the keyword set and the content
 * identifier already demonstrate. */

/* THE KEYWORDS DESCRIBING THE ACTIVITY'S CONTENT, which Apple joins into the searchable text. A copied set;
 * there is no index here to put them in. */
- (nullable NSSet *)keywords;
- (void)setKeywords:(nullable NSSet *)aSet;

/* WHICH SYSTEM BEHAVIORS THE ACTIVITY MAY TAKE PART IN. Stored and answered — the activity owns these flags —
 * but the three engines that would READ them are absent here, and named where they are missed: the handoff
 * daemon (a second device), the CoreSpotlight index, and Siri's prediction/Shortcuts service. */
- (BOOL)eligibleForHandoff;
- (void)setEligibleForHandoff:(BOOL)flag;
- (BOOL)eligibleForSearch;
- (void)setEligibleForSearch:(BOOL)flag;
- (BOOL)eligibleForPublicIndexing;
- (void)setEligibleForPublicIndexing:(BOOL)flag;
- (BOOL)eligibleForPrediction;
- (void)setEligibleForPrediction:(BOOL)flag;

/* THE LIFECYCLE, with Apple's two rules enforced: only one activity is current at a time (becoming current
 * resigns the previous one), and an INVALIDATED activity can never become current again. */
- (void)becomeCurrent;
- (void)resignCurrent;
- (void)invalidate;

/* THE STREAMS DOOR — and here it answers `NSUserActivityConnectionUnavailableError`, because there is no other
 * device to open streams with. The completion handler is called BEFORE THIS METHOD RETURNS (there is nothing to
 * wait for) and may be nil, in which case the door does nothing. */
- (void)getContinuationStreamsWithCompletionHandler:
	(nullable void (^)(NSInputStream *_Nullable inputStream,
			   NSOutputStream *_Nullable outputStream,
			   NSError *_Nullable error))completionHandler;

@end

NS_ASSUME_NONNULL_END
