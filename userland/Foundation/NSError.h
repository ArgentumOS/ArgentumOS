/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSError — a value: a domain, a code, and a userInfo dictionary.
 * docs/design/foundation-plan.md, F4.
 *
 * A VALUE, NOT AN OBJECT WITH BEHAVIOUR: Cocoa passes NSError by pointer-to-
 * pointer in its error-out arguments, and nothing about it is mutable. Ours is
 * the same, which is why it is <NSCopying> and why -copy returns self.
 *
 * THE NAME IS FREE. The runtime declares no NSError (checked: zero matches in
 * libobjc2), so unlike Object and NSAutoreleasePool there is no collision to
 * avoid here.
 *
 * -localizedDescription reads NSLocalizedDescriptionKey out of userInfo, and
 * falls back to a rendering of the domain and code when the key is absent — the
 * same contract Cocoa documents, without the localisation machinery underneath.
 */

#ifndef FOUNDATION_NSERROR_H
#define FOUNDATION_NSERROR_H

#import <Foundation/NSArray.h>
#import <Foundation/NSObject.h>

@class NSString;
@class NSDictionary;

/* NULLABILITY (F6, slice 3): NONNULL by default. An error is a VALUE, so most of
 * it is total — but three things are genuinely optional, and each one is a fact
 * from the writer (NSError.m), not a guess:
 *   - the two constructs are nullable: -initWithDomain:... is one of the measured
 *     `return nil;` sites, and +errorWithDomain:... is
 *     `return [[self alloc] initWithDomain:...]`, so it PROPAGATES that;
 *   - -userInfo is nullable because the property is `[userInfo copy]` of a
 *     nullable argument, and -isEqualToError: itself branches on `_userInfo == nil`;
 *   - -localizedFailureReason is nullable because it is `[_userInfo objectForKey:]`,
 *     whose result is nullable by definition. -localizedDescription is NOT: the
 *     method falls back to a rendered string, so it always answers an object.
 * The userInfo PARAMETERS are nullable too: Cocoa allows nil, and NSException.m
 * passes nil itself when it builds an exception from a format. */
NS_ASSUME_NONNULL_BEGIN

/* THE TYPE OF A userInfo KEY (§62.103): a name for `NSString *` that a caller can spell, which is what makes
 * `NSErrorUserInfoKey key = ...` compile the way Cocoa-shaped code expects. */
typedef NSString *NSErrorUserInfoKey;

/* Cocoa's domain type is just a string. */
typedef NSString *NSErrorDomain;

/* Cocoa's userInfo keys, and they are Cocoa's SPELLINGS on purpose: an error a
 * caller built against Cocoa's documentation has to be readable by our code and
 * vice versa. */
extern NSString *const NSLocalizedDescriptionKey;
extern NSString *const NSLocalizedFailureReasonKey;
extern NSString *const NSLocalizedRecoverySuggestionErrorKey;
extern NSString *const NSUnderlyingErrorKey;

@class NSError;

/* THE PROVIDER BLOCK'S CONTRACT, in Apple's shape: it is asked for a userInfo value the dictionary does NOT
 * carry, and it may answer nil. It is the reason NSError can describe errors whose texts live in a
 * framework rather than in the error object. */
typedef id _Nullable (^NSErrorUserInfoValueProvider)(NSError *error, NSErrorUserInfoKey key);

@interface NSError : NSObject <NSCopying>
{
	NSString *_domain;
	NSInteger _code;
	NSDictionary *_userInfo;
}

+ (nullable instancetype)errorWithDomain:(NSErrorDomain)domain
			   code:(NSInteger)code
		       userInfo:(nullable NSDictionary *)userInfo;

- (nullable id)initWithDomain:(NSErrorDomain)domain
		code:(NSInteger)code
	    userInfo:(nullable NSDictionary *)userInfo;

- (NSErrorDomain)domain;
- (NSInteger)code;
- (nullable NSDictionary *)userInfo;

- (NSString *)localizedDescription;
- (nullable NSString *)localizedFailureReason;

/* THE FIVE READERS THAT COME OUT OF userInfo, WITH APPLE'S KEYS — and every one of them asks a DOMAIN'S
 * PROVIDER when the dictionary does not carry the value (see the two class doors below), because that is
 * what the provider is FOR rather than inert state. */
@property (nullable, readonly, copy) NSString *helpAnchor;
@property (nullable, readonly, copy) NSArray *localizedRecoveryOptions;
@property (nullable, readonly, copy) NSString *localizedRecoverySuggestion;
@property (nullable, readonly) id recoveryAttempter;
/* NOT OPTIONAL: Apple's contract is an EMPTY array when there is nothing, which is why this one is not
 * nullable - it is built from NSMultipleUnderlyingErrorsKey, else the one-element NSUnderlyingErrorKey. */
@property (readonly, copy) NSArray *underlyingErrors;

/* A PROVIDER IS KEYED BY DOMAIN, and a nil provider removes the one that was there. A nil DOMAIN is this
 * library's WILDCARD — "the provider for every domain" — and it is a stated reading: Apple's documentation
 * says a provider is asked when the dictionary has no value, and does not spell the nil-domain case out. */
+ (void)setUserInfoValueProviderForDomain:(nullable NSErrorDomain)domain
				 provider:(nullable NSErrorUserInfoValueProvider)provider;
+ (nullable NSErrorUserInfoValueProvider)userInfoValueProviderForDomain:(nullable NSErrorDomain)domain;



/* ---- THE ERROR DOMAINS, USER-INFO KEYS AND CODES (the coverage slice) ------------------------------
 *
 * THE NAMES ARE APPLE'S AND THE VALUES ARE OURS (§11.6.1 D2), WITH ONE PROPERTY THAT DOES MATTER: an error
 * code is a number a caller can compare, so each family here is numbered sequentially and its Minimum/Maximum
 * pair ENCLOSES EXACTLY ITS OWN CODES. A program that compares against the symbols is right; a program that
 * hardcodes Apple's numbers is not portable, and this header says so rather than implying a fidelity the
 * library does not claim. */
extern NSErrorDomain const NSCocoaErrorDomain;
extern NSErrorDomain const NSDebugDescriptionErrorKey;
extern NSErrorDomain const NSFilePathErrorKey;
extern NSErrorDomain const NSHelpAnchorErrorKey;
extern NSErrorDomain const NSLocalizedFailureErrorKey;
extern NSErrorDomain const NSLocalizedFailureReasonErrorKey;
extern NSErrorDomain const NSLocalizedRecoveryOptionsErrorKey;
extern NSErrorDomain const NSMachErrorDomain;
extern NSErrorDomain const NSMultipleUnderlyingErrorsKey;
extern NSErrorDomain const NSOSStatusErrorDomain;
extern NSErrorDomain const NSPOSIXErrorDomain;
extern NSErrorDomain const NSRecoveryAttempterErrorKey;
extern NSErrorDomain const NSStringEncodingErrorKey;

extern NSInteger const NSBundleErrorMaximum;
extern NSInteger const NSBundleErrorMinimum;
extern NSInteger const NSBundleOnDemandResourceExceededMaximumSizeError;
extern NSInteger const NSBundleOnDemandResourceInvalidTagError;
extern NSInteger const NSBundleOnDemandResourceOutOfSpaceError;
extern NSInteger const NSCloudSharingConflictError;
extern NSInteger const NSCloudSharingErrorMaximum;
extern NSInteger const NSCloudSharingErrorMinimum;
extern NSInteger const NSCloudSharingNetworkFailureError;
extern NSInteger const NSCloudSharingNoPermissionError;
extern NSInteger const NSCloudSharingOtherError;
extern NSInteger const NSCloudSharingQuotaExceededError;
extern NSInteger const NSCloudSharingTooManyParticipantsError;
extern NSInteger const NSCoderErrorMaximum;
extern NSInteger const NSCoderErrorMinimum;
extern NSInteger const NSCoderInvalidValueError;
extern NSInteger const NSCoderReadCorruptError;
extern NSInteger const NSCoderValueNotFoundError;
extern NSInteger const NSExecutableArchitectureMismatchError;
extern NSInteger const NSExecutableErrorMaximum;
extern NSInteger const NSExecutableErrorMinimum;
extern NSInteger const NSExecutableLinkError;
extern NSInteger const NSExecutableLoadError;
extern NSInteger const NSExecutableNotLoadableError;
extern NSInteger const NSExecutableRuntimeMismatchError;
extern NSInteger const NSFeatureUnsupportedError;
extern NSInteger const NSFileErrorMaximum;
extern NSInteger const NSFileErrorMinimum;
extern NSInteger const NSFileLockingError;
extern NSInteger const NSFileManagerUnmountBusyError;
extern NSInteger const NSFileManagerUnmountUnknownError;
extern NSInteger const NSFileNoSuchFileError;
extern NSInteger const NSFileReadCorruptFileError;
extern NSInteger const NSFileReadInapplicableStringEncodingError;
extern NSInteger const NSFileReadInvalidFileNameError;
extern NSInteger const NSFileReadNoPermissionError;
extern NSInteger const NSFileReadNoSuchFileError;
extern NSInteger const NSFileReadTooLargeError;
extern NSInteger const NSFileReadUnknownError;
extern NSInteger const NSFileReadUnknownStringEncodingError;
extern NSInteger const NSFileReadUnsupportedSchemeError;
extern NSInteger const NSFileWriteFileExistsError;
extern NSInteger const NSFileWriteInapplicableStringEncodingError;
extern NSInteger const NSFileWriteInvalidFileNameError;
extern NSInteger const NSFileWriteNoPermissionError;
extern NSInteger const NSFileWriteOutOfSpaceError;
extern NSInteger const NSFileWriteUnknownError;
extern NSInteger const NSFileWriteUnsupportedSchemeError;
extern NSInteger const NSFileWriteVolumeReadOnlyError;
extern NSInteger const NSFormattingError;
extern NSInteger const NSFormattingErrorMaximum;
extern NSInteger const NSFormattingErrorMinimum;
extern NSInteger const NSPropertyListErrorMaximum;
extern NSInteger const NSPropertyListErrorMinimum;
extern NSInteger const NSPropertyListReadCorruptError;
extern NSInteger const NSPropertyListReadStreamError;
extern NSInteger const NSPropertyListReadUnknownVersionError;
extern NSInteger const NSPropertyListWriteInvalidError;
extern NSInteger const NSPropertyListWriteStreamError;
extern NSInteger const NSUbiquitousFileErrorMaximum;
extern NSInteger const NSUbiquitousFileErrorMinimum;
extern NSInteger const NSUbiquitousFileNotUploadedDueToQuotaError;
extern NSInteger const NSUbiquitousFileUbiquityServerNotAvailable;
extern NSInteger const NSUbiquitousFileUnavailableError;
extern NSInteger const NSUserActivityConnectionUnavailableError;
extern NSInteger const NSUserActivityErrorMaximum;
extern NSInteger const NSUserActivityErrorMinimum;
extern NSInteger const NSUserActivityHandoffFailedError;
extern NSInteger const NSUserActivityHandoffUserInfoTooLargeError;
extern NSInteger const NSUserActivityRemoteApplicationTimedOutError;
extern NSInteger const NSUserCancelledError;
extern NSInteger const NSValidationErrorMaximum;
extern NSInteger const NSValidationErrorMinimum;

NS_ASSUME_NONNULL_END

@end

#endif /* FOUNDATION_NSERROR_H */
