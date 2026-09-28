/*
 * NSURLError.h
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * WHAT WENT WRONG, as names instead of numbers: the URL loading system's error codes, the two dictionaries of
 * reasons, and the userInfo keys that carry them.
 *
 * WHERE THESE VALUES COME FROM, WHICH A READER HAS TO BE ABLE TO TELL (docs/design/foundation-plan.md §55 and
 * §56), because the honest answer is not "from the documentation":
 *
 *   * THE CODES ARE TRANSCRIBED FROM THE PUBLISHED ERROR-CONDITION BLOCKS Apple's SDK carries - `-1`, the
 *     `-999`/`-1000…-1022` run, `-1100…-1104`, `-1200…-1206`, `-2000`, `-3000…-3007`, and the three
 *     `-995…-997` background-session ones. **APPLE'S PUBLISHED DOCUMENTATION PAGES DO NOT STATE THEM:** §55
 *     measured that every code's page carries a name, an abstract and no number - in the ObjC view and in the
 *     Swift view alike, with and without `?language=`. This tree had been using three of them as LITERALS with
 *     the names in comments (`-999`, `-1002`, `-3000`), and §54's failure path wanted a fourth. They are
 *     transcribed here rather than left as literals, and EVERY VALUE IS REFUTABLE the day one is shown wrong;
 *   * THE USERINFO KEYS' STRING VALUES ARE OURS (§11.6.1 D2), and legitimately so: a program compares against
 *     the CONSTANT, so the constant is the contract and the spelling is this library's. `NSURLErrorDomain` is
 *     the exception, because its value crosses systems - it is `NSURLErrorDomain`, which this tree was
 *     already spelling at three call sites;
 *   * THE TWO REASON ENUMERATIONS ARE OURS (D2) for the same reason: both travel as values INSIDE a userInfo
 *     dictionary and are read back through their own key, so no server and no other system ever sees one.
 *
 * AND THE FAMILY HAS A SHAPE worth stating where the numbers are: `-1` for "no idea", `-999` for "the caller
 * stopped it", the `-1000` run for the exchange itself, `-1100` for the file system, `-1200` for TLS, `-2000`
 * for "this needed the network and there is none", and `-3000` for the body's file. A caller that has to tell
 * those cases apart has one place to look.
 */

#ifndef _FNX_FOUNDATION_NSURLERROR_H
#define _FNX_FOUNDATION_NSURLERROR_H

#import <Foundation/NSObject.h>
#import <Foundation/NSObjCRuntime.h>
#import <Foundation/NSString.h>

NS_ASSUME_NONNULL_BEGIN

/* THE DOMAIN EVERY ONE OF THESE IS REPORTED IN. A caller compares an NSError's domain against this, so its
 * VALUE is part of the contract rather than a spelling choice. */
extern NSString * const NSURLErrorDomain;

/* THE CODES, as an ANONYMOUS enum - which is how this family was always declared: there is no type a caller
 * passes around, only the constants, and Apple's own documentation lists them as cases under NSError rather
 * than under a type of their own. */
enum {
	/* NOTHING KNOWS. */
	NSURLErrorUnknown = -1,
	/* THE CALLER STOPPED IT - the one code this library already reported, from -cancel. */
	NSURLErrorCancelled = -999,

	/* THE EXCHANGE ITSELF: what was wrong with the request, the connection or the answer. */
	NSURLErrorBadURL = -1000,
	NSURLErrorTimedOut = -1001,
	NSURLErrorUnsupportedURL = -1002,
	NSURLErrorCannotFindHost = -1003,
	NSURLErrorCannotConnectToHost = -1004,
	NSURLErrorNetworkConnectionLost = -1005,
	NSURLErrorDNSLookupFailed = -1006,
	NSURLErrorHTTPTooManyRedirects = -1007,
	NSURLErrorResourceUnavailable = -1008,
	NSURLErrorNotConnectedToInternet = -1009,
	NSURLErrorRedirectToNonExistentLocation = -1010,
	NSURLErrorBadServerResponse = -1011,
	NSURLErrorUserCancelledAuthentication = -1012,
	NSURLErrorUserAuthenticationRequired = -1013,
	NSURLErrorZeroByteResource = -1014,
	NSURLErrorCannotDecodeRawData = -1015,
	NSURLErrorCannotDecodeContentData = -1016,
	NSURLErrorCannotParseResponse = -1017,
	NSURLErrorInternationalRoamingOff = -1018,
	NSURLErrorCallIsActive = -1019,
	NSURLErrorDataNotAllowed = -1020,
	NSURLErrorRequestBodyStreamExhausted = -1021,
	NSURLErrorAppTransportSecurityRequiresSecureConnection = -1022,

	/* THE FILE SYSTEM, for the file: scheme and for bodies written to disk. */
	NSURLErrorFileDoesNotExist = -1100,
	NSURLErrorFileIsDirectory = -1101,
	NSURLErrorNoPermissionsToReadFile = -1102,
	NSURLErrorDataLengthExceedsMaximum = -1103,
	NSURLErrorFileOutsideSafeArea = -1104,

	/* TLS: the handshake, and then each thing that can be wrong with a certificate. */
	NSURLErrorSecureConnectionFailed = -1200,
	NSURLErrorServerCertificateHasBadDate = -1201,
	NSURLErrorServerCertificateUntrusted = -1202,
	NSURLErrorServerCertificateHasUnknownRoot = -1203,
	NSURLErrorServerCertificateNotYetValid = -1204,
	NSURLErrorClientCertificateRejected = -1205,
	NSURLErrorClientCertificateRequired = -1206,

	/* THIS NEEDED THE NETWORK, AND THERE IS NONE. */
	NSURLErrorCannotLoadFromNetwork = -2000,

	/* THE BODY'S FILE, which a download task writes and a caller then moves. */
	NSURLErrorCannotCreateFile = -3000,
	NSURLErrorCannotOpenFile = -3001,
	NSURLErrorCannotCloseFile = -3002,
	NSURLErrorCannotWriteToFile = -3003,
	NSURLErrorCannotRemoveFile = -3004,
	NSURLErrorCannotMoveFile = -3005,
	NSURLErrorDownloadDecodingFailedMidStream = -3006,
	NSURLErrorDownloadDecodingFailedToComplete = -3007,

	/* A BACKGROUND SESSION COULD NOT BE RUN AT ALL. */
	NSURLErrorBackgroundSessionRequiresSharedContainer = -995,
	NSURLErrorBackgroundSessionInUseByAnotherProcess = -996,
	NSURLErrorBackgroundSessionWasDisconnected = -997
};

/* WHERE A CALLER LOOKS FOR THE URL, the failing URL, and the peer trust of the answer that failed. THE
 * VALUES ARE OURS (D2) - a program compares against the constant, not against the string. */
extern NSString * const NSURLErrorKey;
extern NSString * const NSURLErrorFailingURLErrorKey;
/* THE DEPRECATED STRING SPELLINGS (§62.103): the failing URL as a STRING rather than as an NSURL, kept for the
 * programs that were written against them. Each value is its own name, which is what both keys are for. */
extern NSString * const NSURLErrorFailingURLStringErrorKey;
extern NSString * const NSErrorFailingURLStringKey;
extern NSString * const NSURLErrorFailingURLPeerTrustErrorKey;

/* WHY THE NETWORK COULD NOT BE USED, carried under -NSURLErrorNetworkUnavailableReasonKey. ORDERED BY WHAT A
 * CALLER ASKS FIRST: no network at all, then each stricter reason on the connection it does have. */
typedef NS_ENUM(NSInteger, NSURLErrorNetworkUnavailableReason) {
	NSURLErrorNetworkUnavailableReasonCellular = 0,
	NSURLErrorNetworkUnavailableReasonExpensive = 1,
	NSURLErrorNetworkUnavailableReasonConstrained = 2,
	NSURLErrorNetworkUnavailableReasonUltraConstrained = 3
};

extern NSString * const NSURLErrorNetworkUnavailableReasonKey;

/* WHY A BACKGROUND TASK WAS CANCELLED, carried under -NSURLErrorBackgroundTaskCancelledReasonKey: the user
 * quit the app, the system would not let the update run, or the system needed the resources back. */
typedef NS_ENUM(NSInteger, NSURLErrorBackgroundTaskCancelledReason) {
	NSURLErrorCancelledReasonUserForceQuitApplication = 0,
	NSURLErrorCancelledReasonBackgroundUpdatesDisabled = 1,
	NSURLErrorCancelledReasonInsufficientSystemResources = 2
};

extern NSString * const NSURLErrorBackgroundTaskCancelledReasonKey;

NS_ASSUME_NONNULL_END

#endif /* _FNX_FOUNDATION_NSURLERROR_H */
