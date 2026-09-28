/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSStream — THE STREAM HEAD. docs/design/foundation-plan.md W6's streams half and §45-Y.
 *
 * THIS IS THE ABSTRACT HEAD, AND IT CARRIES THE THREE THINGS EVERY STREAM OBEYS:
 *
 *   * THE STATUS MACHINE (`NSStreamStatus`): a stream is NotOpen, then Opening/Open, then Reading or
 *     Writing while an operation is in flight, and Closed or Error at the end. The observation is
 *     `-streamStatus`, NOT an `-isOpen` boolean, because Apple's enum has the two middle states and a
 *     boolean cannot say them.
 *   * THE PROPERTY BAG (`-propertyForKey:`/`-setProperty:forKey:`), which is where a stream's
 *     configuration lives; the keys are declared below.
 *   * THE RUN-LOOP SEAM (`-scheduleInRunLoop:forMode:`), which is what makes a stream ASYNCHRONOUS: a
 *     concrete stream hands the base a descriptor through the private `-fnStreamDescriptor` hook, and the
 *     base registers it with the run loop's own file-descriptor source, so `NSStreamEventHasBytesAvailable`
 *     arrives as an EVENT instead of a read blocking the thread. This tree needs no new substrate for that:
 *     `NSRunLoop` already has `-addSourceForFileDescriptor:mode:readable:target:selector:` and a select(2)
 *     wait that ends early on a ready descriptor (§45-Y's finding).
 *
 * A SUBSTREAM SUBCLASSES `NSStream` AND OVERRIDES THE FOUR PRIMITIVES: `-fnStreamDescriptor` (the file
 * descriptor, or -1), `-fnStreamOpen`, `-fnStreamClose`, and the bytes/space operations. The base's own
 * `-open`/`-close` DO NOTHING — Apple documents `NSStream` as abstract and its subclasses as the ones that
 * open a resource — so a base instance used directly is inert rather than pretending to be open.
 *
 * AND TWO THINGS ARE OURS, STATED HERE BECAUSE APPLE PUBLISHES NEITHER:
 *
 *   * THE ENUM VALUES AND THE KEY STRINGS. Apple documents the CASE NAMES of `NSStreamEvent`,
 *     `NSStreamStatus`, the property keys and the typealiases, and the case names only — the numbers and the
 *     strings behind them are not published anywhere this project may read (§2's clean-room wall, and the
 *     plan's D2 ruling recorded after F9). The values here are therefore OUR CHOICE: the conventional
 *     `1 << n` masks and the key's own name as its string value, both stated in the probe that pins them.
 *   * THE NETWORK-SERVICE KEYS CARRY NO BEHAVIOUR (user's decision, 2026-09-21). The
 *     `SocketSecurityLevel*`, `SOCKSProxy*` and `NetworkServiceType*` keys are DECLARED so a stream may
 *     carry them and the documented surface is closed, and they are documented as a property bag: a stream
 *     that is handed one records it and answers it, and nothing in this library acts on it. §39 declined the
 *     families they serve (Bonjour, XPC), so there is no service behind them to configure.
 *
 * NOT DECLARED, NAMED: `NSStream`'s socket-bound initialisers and the `NSStreamSocketSSLErrorDomain` /
 * `NSStreamSOCKSErrorDomain` STRINGS are declared below, but the SSLErrorDomain/SOCKSErrorDomain DOMAINS a
 * caller might build an NSError with are ours to spell as the same names (they are `NSString *const`s, and
 * Apple's values are unpublished like the rest).
 */

#ifndef FOUNDATION_NSSTREAM_H
#define FOUNDATION_NSSTREAM_H

#import <Foundation/NSObject.h>
#import <Foundation/NSObjCRuntime.h>	/* NS_ENUM, NS_OPTIONS */
#import <Foundation/NSError.h>		/* NSErrorDomain */
#import <Foundation/NSRunLoop.h>	/* NSRunLoopMode */

@class NSDictionary;
@class NSMutableDictionary;
@class NSError;
@class NSRunLoop;
@class NSString;

NS_ASSUME_NONNULL_BEGIN

/* THE DELEGATE'S EVENT MASK. `NSStreamEventNone` means "no event", which is why it is 0 in a mask that is
 * otherwise bits: a delegate that sees it has been called for a reason other than readiness. */
typedef NS_OPTIONS(NSUInteger, NSStreamEvent) {
	NSStreamEventNone = 0,
	NSStreamEventOpenCompleted = 1 << 0,
	NSStreamEventHasBytesAvailable = 1 << 1,
	NSStreamEventHasSpaceAvailable = 1 << 2,
	NSStreamEventErrorOccurred = 1 << 3,
	NSStreamEventEndEncountered = 1 << 4,
};

/* THE STATUS A STREAM REPORTS. Opening and Closing are states rather than a boolean's edge because a
 * concrete stream performs real work between them, and a caller polling `-streamStatus` must be able to see
 * which side of that work it is on. */
typedef NS_ENUM(NSUInteger, NSStreamStatus) {
	NSStreamStatusNotOpen = 0,
	NSStreamStatusOpening = 1,
	NSStreamStatusOpen = 2,
	NSStreamStatusReading = 3,
	NSStreamStatusWriting = 4,
	NSStreamStatusAtEnd = 5,
	NSStreamStatusClosed = 6,
	NSStreamStatusError = 7,
};

/* THE PROPERTY KEYS, AND THE TYPEALIASES THEY ARE SPELLED IN. */
typedef NSString * NSStreamPropertyKey;
typedef NSString * NSStreamSocketSecurityLevel;
typedef NSString * NSStreamSOCKSProxyVersion;
typedef NSString * NSStreamNetworkServiceTypeValue;
typedef NSDictionary * NSStreamSOCKSProxyConfiguration;

/* THE FILE AND MEMORY KEYS - the two that HAVE behaviour in this library. */
extern NSStreamPropertyKey const NSStreamFileCurrentOffsetKey;
extern NSStreamPropertyKey const NSStreamDataWrittenToMemoryStreamKey;

/* THE NETWORK HALF: DECLARED, CARRIED, AND NOT ACTED ON (the decision above). */
extern NSStreamPropertyKey const NSStreamSocketSecurityLevelKey;
extern NSStreamSocketSecurityLevel const NSStreamSocketSecurityLevelNone;
extern NSStreamSocketSecurityLevel const NSStreamSocketSecurityLevelSSLv2;
extern NSStreamSocketSecurityLevel const NSStreamSocketSecurityLevelSSLv3;
extern NSStreamSocketSecurityLevel const NSStreamSocketSecurityLevelTLSv1;
extern NSStreamSocketSecurityLevel const NSStreamSocketSecurityLevelNegotiatedSSL;

extern NSStreamPropertyKey const NSStreamSOCKSProxyConfigurationKey;
extern NSStreamPropertyKey const NSStreamSOCKSProxyHostKey;
extern NSStreamPropertyKey const NSStreamSOCKSProxyPortKey;
extern NSStreamPropertyKey const NSStreamSOCKSProxyUserKey;
extern NSStreamPropertyKey const NSStreamSOCKSProxyPasswordKey;
extern NSStreamPropertyKey const NSStreamSOCKSProxyVersionKey;
extern NSStreamSOCKSProxyVersion const NSStreamSOCKSProxyVersion4;
extern NSStreamSOCKSProxyVersion const NSStreamSOCKSProxyVersion5;

extern NSStreamPropertyKey const NSStreamNetworkServiceType;
extern NSStreamNetworkServiceTypeValue const NSStreamNetworkServiceTypeVoIP;
extern NSStreamNetworkServiceTypeValue const NSStreamNetworkServiceTypeBackground;
extern NSStreamNetworkServiceTypeValue const NSStreamNetworkServiceTypeVideo;
extern NSStreamNetworkServiceTypeValue const NSStreamNetworkServiceTypeVoice;
extern NSStreamNetworkServiceTypeValue const NSStreamNetworkServiceTypeCallSignaling;

extern NSErrorDomain const NSStreamSocketSSLErrorDomain;
extern NSErrorDomain const NSStreamSOCKSErrorDomain;

@class NSStream;

/* ONE OPTIONAL METHOD, and it is optional for a reason: a delegate that only wants to be told an error has no
 * business implementing readiness. */
@protocol NSStreamDelegate <NSObject>
@optional
- (void)stream:(NSStream *)aStream handleEvent:(NSStreamEvent)eventCode;
@end

@interface NSStream : NSObject
{
	NSStreamStatus _status;
	NSError *_streamError;
	NSMutableDictionary *_properties;
	id<NSStreamDelegate> _delegate;	/* NOT retained: this library is MRC and a delegate is assign */
	NSRunLoop *_scheduledRunLoop;
	NSRunLoopMode _scheduledMode;
	BOOL _sourceRegistered;
	BOOL _readsForSource;		/* which half of the descriptor the source watches */
}

/* ABSTRACT (Apple's own sentence is that NSStream's subclasses open a resource): the base does NOTHING and
 * the status does not move, so an instance used directly is inert rather than falsely open. */
- (void)open;
- (void)close;

/* NOT RETAINED, matching Apple: a stream does not own its delegate (and a delegate that owns the stream
 * would otherwise never see -dealloc). */
- (nullable id<NSStreamDelegate>)delegate;
- (void)setDelegate:(nullable id<NSStreamDelegate>)delegate;

/* THE PROPERTY BAG. `-setProperty:forKey:` answers NO when the stream does not take the key; with no
 * behaviour behind any key in this library, the base records what it is given and answers YES, and the two
 * file/memory keys are the ones a concrete stream CONSULTS. */
- (nullable id)propertyForKey:(NSStreamPropertyKey)key;
- (BOOL)setProperty:(nullable id)property forKey:(NSStreamPropertyKey)key;

/* THE SEAM. Apple: scheduling happens BEFORE -open, and both calls must name the same loop and mode. The
 * base registers its descriptor as a run-loop source when the stream HAS one, which is what turns
 * readiness into a delegate call. */
- (void)scheduleInRunLoop:(NSRunLoop *)aRunLoop forMode:(NSRunLoopMode)mode;
- (void)removeFromRunLoop:(NSRunLoop *)aRunLoop forMode:(NSRunLoopMode)mode;

- (NSStreamStatus)streamStatus;
- (nullable NSError *)streamError;

@end

/* THE SUBSTREAM HOOKS. Private in the sense that Apple declares no such thing and no caller should use them:
 * they are how a substream tells the base WHAT to watch and WHAT to say.
 *
 *   -fnStreamDescriptor        the descriptor to watch, or -1 when the stream has none (a memory stream).
 *   -fnStreamWatchesReadable   which half of that descriptor is this stream's business.
 *   -fnStreamSetStatus:error:  the SUBSTREAM moves the status, because only it knows whether its resource
 *                              opened; the base's own -open/-close stay inert (see above).
 *   -fnStreamEventForReadiness what a READY descriptor means to this stream. The base answers the event for
 *                              the half it watches (bytes for an input, space for an output); an INPUT stream
 *                              overrides it because a descriptor that is ready and EMPTY is the END of the
 *                              stream rather than bytes, and only the substream can tell those apart.
 *   -fnStreamDispatch:         hand an event to the delegate, if it wants one. */
@interface NSStream (FNSubstream)
- (int)fnStreamDescriptor;
- (BOOL)fnStreamWatchesReadable;
- (NSStreamEvent)fnStreamEventForReadiness;
- (void)fnStreamSetStatus:(NSStreamStatus)status error:(nullable NSError *)error;
- (void)fnStreamDispatch:(NSStreamEvent)eventCode;
@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSSTREAM_H */
