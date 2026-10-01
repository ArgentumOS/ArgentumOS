/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUserActivity.m — the activity object in a system with no second device (§62.89). MANUAL OWNERSHIP.
 *
 * THREE RULES LIVE HERE AND NOWHERE ELSE, which is why they are worth naming at the top of the file:
 *   * the type is the identity, so it is copied once and never changed;
 *   * the process's CURRENT activity is a single slot, so becoming current resigns whoever held it;
 *   * and an INVALIDATED activity is out of the race for good — Apple's words are that becoming current
 *     afterwards "has no effect", and `_invalidated` is that sentence.
 *
 * THE ONE SAVE THIS SYSTEM HAS IS THE DELEGATE'S. Apple's `-userActivityWillSave:` is sent before the system
 * saves an activity; here there is no system doing the saving, so SETTING `needsSave` is what asks the delegate
 * — and the flag clears once it has been asked, which is the documented cycle and the reason a caller may set
 * it as often as it likes.
 */

#import <Foundation/NSUserActivity.h>
#import <Foundation/FNUserActivity.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSDate.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSError.h>
#import <Foundation/NSException.h>
#import <Foundation/NSInputStream.h>
#import <Foundation/NSOutputStream.h>
#import <Foundation/NSSet.h>
#import <Foundation/NSString.h>
#import <Foundation/NSURL.h>

NSString * const NSUserActivityTypeBrowsingWeb = @"NSUserActivityTypeBrowsingWeb";

/* THE PROCESS'S SINGLE CURRENT ACTIVITY. One slot, not a registry: Apple's rule is that only one activity is
 * current at a time, and a slot is the smallest thing that can enforce it. NOT RETAINED — whoever made it
 * current owns it, exactly as the system would. */
static id fn_current_activity = nil;

/* THE PRIVATE DOOR THE SEAM USES. Apple's public surface has no "is this activity retired?" question, and the
 * seam needs one: an invalidated activity is no longer ELIGIBLE for continuation, so a continuation arriving for
 * it is not a fact about this device. A METHOD rather than the seam reading the ivar — reaching in from outside a
 * class is what §62.79 recorded as a mistake. */
@interface NSUserActivity (FNPrivate)
- (BOOL)fnIsInvalidated;
@end

@implementation NSUserActivity

- (instancetype)init
{
	/* APPLE'S INHERITED INITIALISER USES THE FIRST TYPE DECLARED IN THE APPLICATION'S Info.plist. NO BUNDLE IN
	 * THIS SYSTEM DECLARES ACTIVITY TYPES, so this cannot name one — and an activity with no type is not an
	 * activity Apple's system could continue. Raising with the ground beats answering with something unusable. */
	[NSException raise:NSInvalidArgumentException
		    format:@"-init needs an activity type from the application's Info.plist, and no bundle in this "
			   @"system declares one — use -initWithActivityType:"];
	return nil;
}

- (instancetype)initWithActivityType:(NSString *)activityType
{
	if (activityType == nil || [activityType length] == 0) {
		[NSException raise:NSInvalidArgumentException
			    format:@"an activity needs a type — it is the activity's identity and cannot change later"];
	}
	self = [super init];
	if (self != nil) {
		_activityType = [activityType copy];
	}
	return self;
}

- (void)dealloc
{
	[(id)_activityType release];
	[(id)_title release];
	[(id)_userInfo release];
	[(id)_requiredUserInfoKeys release];
	[(id)_webpageURL release];
	[(id)_referrerURL release];
	[(id)_expirationDate release];
	[(id)_persistentIdentifier release];
	[(id)_targetContentIdentifier release];
	[(id)_externalMediaContentIdentifier release];
	[(id)_suggestedInvocationPhrase release];
	[(id)_keywords release];
	[super dealloc];
}

- (NSString *)activityType { return _activityType; }

/* --- the payload ---------------------------------------------------------------------------------- */

- (nullable NSString *)title { return _title; }

- (void)setTitle:(nullable NSString *)aTitle
{
	id kept = [aTitle copy];

	[(id)_title release];
	_title = kept;
}

- (nullable NSDictionary *)userInfo { return _userInfo; }

- (void)setUserInfo:(nullable NSDictionary *)aDictionary
{
	id kept = [aDictionary copy];

	[(id)_userInfo release];
	_userInfo = kept;
}

- (void)addUserInfoEntriesFromDictionary:(NSDictionary *)otherDictionary
{
	NSArray *keys;
	NSUInteger i, n;
	id merged;

	if (otherDictionary == nil) {
		return;
	}
	/* A MERGE, NOT A REPLACEMENT: Apple's rule is that a key present in both takes the value from the
	 * incoming dictionary, so the existing entries are the base and the new ones are written over them. */
	merged = _userInfo != nil ? [[NSMutableDictionary alloc] initWithDictionary:(NSDictionary *)_userInfo]
				  : [[NSMutableDictionary alloc] init];
	keys = [otherDictionary allKeys];
	n = [keys count];
	for (i = 0; i < n; i++) {
		[merged setObject:[otherDictionary objectForKey:[keys objectAtIndex:i]]
			   forKey:[keys objectAtIndex:i]];
	}
	[(id)_userInfo release];
	_userInfo = merged;
}

- (nullable NSSet *)requiredUserInfoKeys { return _requiredUserInfoKeys; }

- (void)setRequiredUserInfoKeys:(nullable NSSet *)aSet
{
	id kept = [aSet copy];

	[(id)_requiredUserInfoKeys release];
	_requiredUserInfoKeys = kept;
}

- (nullable NSURL *)webpageURL { return _webpageURL; }

- (void)setWebpageURL:(nullable NSURL *)aURL
{
	id kept = [aURL copy];

	[(id)_webpageURL release];
	_webpageURL = kept;
}

- (nullable NSURL *)referrerURL { return _referrerURL; }

- (void)setReferrerURL:(nullable NSURL *)aURL
{
	id kept = [aURL copy];

	[(id)_referrerURL release];
	_referrerURL = kept;
}

- (nullable NSDate *)expirationDate { return _expirationDate; }

- (void)setExpirationDate:(nullable NSDate *)aDate
{
	id kept = [aDate copy];

	[(id)_expirationDate release];
	_expirationDate = kept;
}

- (BOOL)supportsContinuationStreams { return _supportsContinuationStreams; }

- (void)setSupportsContinuationStreams:(BOOL)flag { _supportsContinuationStreams = flag; }

/* --- the rest of the value model: identifiers, keywords and the eligibility flags ------------------- */
/* EACH STRING AND THE KEYWORD SET IS COPIED IN, exactly as the title and the URLs above are: a caller's
 * later mutation is not the activity's business. THE FLAGS ARE PLAIN BOOLEANS the activity owns; the engines
 * that would read them (the handoff daemon, the CoreSpotlight index, Siri) do not exist in this system, so a
 * flag is state the activity answers and nothing else acts on — the same footing `supportsContinuationStreams`
 * already stands on. */

- (nullable NSUserActivityPersistentIdentifier)persistentIdentifier { return _persistentIdentifier; }

- (void)setPersistentIdentifier:(nullable NSUserActivityPersistentIdentifier)anIdentifier
{
	id kept = [anIdentifier copy];

	[(id)_persistentIdentifier release];
	_persistentIdentifier = kept;
}

- (nullable NSString *)targetContentIdentifier { return _targetContentIdentifier; }

- (void)setTargetContentIdentifier:(nullable NSString *)aString
{
	id kept = [aString copy];

	[(id)_targetContentIdentifier release];
	_targetContentIdentifier = kept;
}

- (nullable NSString *)externalMediaContentIdentifier { return _externalMediaContentIdentifier; }

- (void)setExternalMediaContentIdentifier:(nullable NSString *)anIdentifier
{
	id kept = [anIdentifier copy];

	[(id)_externalMediaContentIdentifier release];
	_externalMediaContentIdentifier = kept;
}

- (nullable NSString *)suggestedInvocationPhrase { return _suggestedInvocationPhrase; }

- (void)setSuggestedInvocationPhrase:(nullable NSString *)aPhrase
{
	id kept = [aPhrase copy];

	[(id)_suggestedInvocationPhrase release];
	_suggestedInvocationPhrase = kept;
}

- (nullable NSSet *)keywords { return _keywords; }

- (void)setKeywords:(nullable NSSet *)aSet
{
	id kept = [aSet copy];

	[(id)_keywords release];
	_keywords = kept;
}

- (BOOL)eligibleForHandoff { return _eligibleForHandoff; }
- (void)setEligibleForHandoff:(BOOL)flag { _eligibleForHandoff = flag; }
- (BOOL)eligibleForSearch { return _eligibleForSearch; }
- (void)setEligibleForSearch:(BOOL)flag { _eligibleForSearch = flag; }
- (BOOL)eligibleForPublicIndexing { return _eligibleForPublicIndexing; }
- (void)setEligibleForPublicIndexing:(BOOL)flag { _eligibleForPublicIndexing = flag; }
- (BOOL)eligibleForPrediction { return _eligibleForPrediction; }
- (void)setEligibleForPrediction:(BOOL)flag { _eligibleForPrediction = flag; }

/* --- the delegate and the one save ----------------------------------------------------------------- */

- (nullable id <NSUserActivityDelegate>)delegate { return _delegate; }

- (void)setDelegate:(nullable id <NSUserActivityDelegate>)anObject { _delegate = anObject; }

- (BOOL)needsSave { return _needsSave; }

- (void)setNeedsSave:(BOOL)flag
{
	if (!flag) {
		_needsSave = NO;
		return;
	}
	/* ASKING TO SAVE IS ASKING THE DELEGATE: Apple's `-userActivityWillSave:` is "where you update the
	 * `userInfo` dictionary", so it is sent BEFORE the flag clears. A delegate that updates the payload
	 * during this call has its updates included, which is the point of the order. */
	_needsSave = YES;
	if ([(id)_delegate respondsToSelector:@selector(userActivityWillSave:)]) {
		[(id <NSUserActivityDelegate>)_delegate userActivityWillSave:self];
	}
	_needsSave = NO;
}

/* --- the lifecycle ---------------------------------------------------------------------------------- */

- (void)becomeCurrent
{
	if (_invalidated) {
		/* APPLE, IN WORDS: "calling `-becomeCurrent` after `-invalidate` has no effect". The sentence is the
		 * implementation. */
		return;
	}
	if (fn_current_activity != nil && fn_current_activity != self &&
	    [fn_current_activity respondsToSelector:@selector(resignCurrent)]) {
		/* ONLY ONE IS CURRENT: whoever held the slot resigns, so the rule is kept rather than described. */
		[fn_current_activity resignCurrent];
	}
	_current = YES;
	fn_current_activity = self;	/* NOT retained: the caller owns the activity it made current */
}

- (void)resignCurrent
{
	_current = NO;
	if (fn_current_activity == self) {
		fn_current_activity = nil;
	}
}

- (void)invalidate
{
	_invalidated = YES;
	if (fn_current_activity == self) {
		fn_current_activity = nil;
	}
	_current = NO;
}

- (BOOL)fnIsInvalidated { return _invalidated; }

- (void)getContinuationStreamsWithCompletionHandler:
	(nullable void (^)(NSInputStream *_Nullable inputStream,
			   NSOutputStream *_Nullable outputStream,
			   NSError *_Nullable error))completionHandler
{
	NSError *error;

	if (completionHandler == nil) {
		return;
	}
	/* THERE IS NO OTHER DEVICE TO OPEN STREAMS WITH, and the error code for that was already in this library
	 * before this unit existed. The handler runs BEFORE this returns: there is nothing to wait for. */
	error = [NSError errorWithDomain:NSCocoaErrorDomain
				   code:NSUserActivityConnectionUnavailableError
			       userInfo:[NSDictionary dictionaryWithObject:
					@"there is no connection to another device"
								      forKey:NSLocalizedDescriptionKey]];
	completionHandler(nil, nil, error);
}

@end

/* --- the seam: what arrives from another device ----------------------------------------------------- */

void FNUserActivityDeliverContinuation(NSUserActivity *activity)
{
	id delegate;

	if ([activity fnIsInvalidated]) {
		/* AN INVALIDATED ACTIVITY IS NOT ELIGIBLE FOR CONTINUATION (Apple's own words for what invalidation
		 * means), so a continuation for it is not a fact about this device and is DROPPED. This is also the
		 * one part of the lifecycle rule that is observable through the public surface — the probe says so. */
		return;
	}
	delegate = [activity delegate];
	if ([delegate respondsToSelector:@selector(userActivityWasContinued:)]) {
		[(id <NSUserActivityDelegate>)delegate userActivityWasContinued:activity];
	}
}

void FNUserActivityDeliverStreams(NSUserActivity *activity, NSInputStream *inputStream,
				  NSOutputStream *outputStream)
{
	id delegate = [activity delegate];

	if ([delegate respondsToSelector:@selector(userActivity:didReceiveInputStream:outputStream:)]) {
		[(id <NSUserActivityDelegate>)delegate userActivity:activity
					      didReceiveInputStream:inputStream
							     outputStream:outputStream];
	}
}
