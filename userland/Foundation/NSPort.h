/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSPort — A COMMUNICATION ENDPOINT, WHICH HERE IS A DESCRIPTOR THE RUN LOOP WATCHES. docs/design/
 * foundation-plan.md W6b and §43.
 *
 * THE FAMILY WAS STRUCK BY §11.5 AND §62.24 PUT IT BACK. This note used to read "THE CLASS IS ALMOST ALL
 * STRUCK", listing a message type, a delegate protocol, three concrete subclasses and a connection class as
 * EXCLUDED BY NAME — because they were deprecated and "a name is not declared until it works". THE POLICY
 * RETIRED THE DEPRECATION GROUND AS A STRIKE, so those names are a PORTING TARGET again and each lands with its
 * implementation:
 *
 *   NSPortMessage          LANDED (§62.53) — the object a port sends and receives
 *   NSPortDelegate         LANDED (§62.53) — the protocol that receives it, and the port's delegate door
 *   NSMachPort             LANDED (§62.53) — what `+port` answers, on this system's socket pair
 *   NSMessagePort          not yet — the naming half of the family
 *   the name servers       not yet — they publish a port for another PROCESS to find, which needs the
 *                          accept-and-connect path NSConnection owns
 *   NSConnection           not yet — distributed objects
 *
 * So `-sendBeforeDate:components:from:reserved:` HAS a component type to carry, `-setDelegate:` HAS a protocol
 * to be typed by, and the two names left are on the work list rather than in the tree: NOTHING IS DECLARED HERE
 * THAT IS NOT DEFINED SOMEWHERE.
 *
 * WHAT IS LEFT IS REAL, AND IT IS WHY THIS CLASS IS NOT STRUCK WITH ITS FAMILY: **a port is something you
 * SCHEDULE IN A RUN LOOP** — a descriptor with an object around it — which is exactly the source kind
 * that NSFileHandle's background read and NSStream's `-scheduleInRunLoop:forMode:` both need. NSSocketPort
 * is the only live concrete subclass left, so `+port` answers one. (Apple's `+port` answers an NSMachPort;
 * that is a consequence of the strike rather than a preference, and it is stated rather than hidden.)
 *
 * THE READINESS OF A SCHEDULED PORT HAS NOWHERE APPLE-SHAPED TO GO, so it goes to
 * `-portDidBecomeReadable`, WHICH IS OURS — the stand-in for the struck `-handlePortMessage:`. The base
 * implementation does nothing, deliberately: a port nobody listens to is still a well-formed run-loop
 * source (the loop wakes, asks the port, and the port says nothing), which is better than a source that
 * has to be removed to stay quiet.
 *
 * ONE (RUN LOOP, MODE) PAIR, AND THAT IS A DOCUMENTED LIMITATION RATHER THAN APPLE'S CONTRACT. Apple's
 * `-scheduleInRunLoop:forMode:` may be called once per mode; this port records the SINGLE pair it was
 * last scheduled in and `-removeFromRunLoop:forMode:` removes that pair only, so scheduling in a second
 * mode REPLACES the first. It is written down here because a silent replacement is exactly the kind of
 * thing a caller should be able to read about.
 *
 * THE DESCRIPTOR IS NOT THIS CLASS'S TO CLOSE either, in the one case that matters: a port built around a
 * socket the CALLER made (`-initWithProtocolFamily:socketType:protocol:socket:`) does not close it. That
 * is the same rule NSFileHandle states for `-initWithFileDescriptor:`, and for the same reason.
 */

#ifndef FOUNDATION_NSPORT_H
#define FOUNDATION_NSPORT_H

#import <Foundation/NSObject.h>
#import <Foundation/NSCoding.h>
#import <Foundation/NSRunLoop.h>

@class NSDate;
@class NSMutableArray;

NS_ASSUME_NONNULL_BEGIN

/* Apple's spelling for a platform-native socket handle — an `int` on this system, and the TYPE IS THE
 * CONTRACT (a port hands descriptors around), so it is its own name rather than a bare int. */
typedef int NSSocketNativeHandle;

/* Posted by `-invalidate`, with the port as the notification's object and no userInfo. */
extern NSString *const NSPortDidBecomeInvalidNotification;

/*
 * AN ABSTRACT SUPERCLASS: it holds the port's VALIDITY and its SCHEDULING, and a subclass holds whatever
 * it is a port for. `-scheduleInRunLoop:forMode:` and `-removeFromRunLoop:forMode:` are the pair a
 * subclass overrides — Apple documents them as "should be implemented by a subclass to set up monitoring"
 * — and the base class still records the pair, because `-invalidate` has to be able to undo it.
 */
@class NSPortMessage;	/* the protocol below names it before its own header is read */

/* THE PROTOCOL A RECEIVED MESSAGE GOES TO (§62.53). Apple's home for it is this header, and its single door is
 * `-handlePortMessage:` — which is why the send door below needed `NSPortMessage` to exist before it could have a
 * component type. It was EXCLUDED BY NAME when §11.5 struck this family and it is back because §62.24's policy
 * retired the deprecation ground as a strike. */
@protocol NSPortDelegate <NSObject>
@optional
- (void)handlePortMessage:(NSPortMessage *)message;
@end

@interface NSPort : NSObject <NSCoding>
{
	BOOL _valid;
	NSRunLoop *_runLoop;		/* the ONE loop this port was last scheduled in (see the header) */
	NSString *_mode;		/* copied */
	id <NSPortDelegate> _delegate;	/* NOT retained: a delegate that retained its port would be a cycle */
}

/* "Creates and returns a new NSPort object" — AN NSMACHPORT, which is Apple's own answer and now this
 * library's too: the port it makes is a socket pair with a peer this process can hand out (`-peerPort`). */
+ (NSPort *)port;

/* Marks the receiver invalid, unregisters it, and posts NSPortDidBecomeInvalidNotification ONCE: a
 * second `-invalidate` is a no-op, which is what makes it safe to call from a teardown path twice. */
- (void)invalidate;

- (BOOL)isValid;

/* Both are overridden by a subclass that has something to watch; the base records the pair. */
- (void)scheduleInRunLoop:(NSRunLoop *)runLoop forMode:(NSRunLoopMode)mode;
- (void)removeFromRunLoop:(NSRunLoop *)runLoop forMode:(NSRunLoopMode)mode;

/* "The number of bytes of space reserved by the receiver for sending data." Zero here, and it is a real
 * answer rather than a stub: the message API that would have needed reserved space is struck. */
- (NSUInteger)reservedSpaceLength;

/* OURS — the readiness of a scheduled port, delivered here because the delegate that would have carried
 * it WAS struck. A subclass or a consumer overrides this; the base does nothing.
 *
 * §62.53 PUT THE DELEGATE BACK, and this door stays: a port that has no delegate still has a readiness to
 * report, and the wake it produces is what the run loop already relies on. */
- (void)portDidBecomeReadable;

/* ===================================================================================================
 * THE DELEGATE AND THE SEND DOOR (§62.53)
 *
 * Both were EXCLUDED BY NAME when §11.5 struck this family, and both are back because §62.24's policy retired
 * the deprecation ground as a strike. The send door has a component type to carry again — `NSPortMessage` —
 * which is exactly the reason the note above gave for its absence.
 *
 * THE BASE CLASS HAS NO TRANSPORT, so `-sendBeforeDate:…` here ANSWERS NO rather than pretending: a port that
 * cannot carry a message says so at the door a caller asks, and every concrete port in this library answers
 * with its own. */
- (void)setDelegate:(nullable id <NSPortDelegate>)anObject;
- (nullable id <NSPortDelegate>)delegate;

- (BOOL)sendBeforeDate:(NSDate *)date
	    components:(nullable NSMutableArray *)components
		  from:(nullable NSPort *)receivePort
	      reserved:(NSUInteger)headerSpaceReserved;

/* THE GENERAL FORM, AND IT IS THE ONE A TRANSPORT IMPLEMENTS: Apple's `-sendBeforeDate:msgid:components:from:
 * reserved:` carries the message id that the narrow form above cannot, and a subclass overrides THIS one — the
 * narrow form is defined in terms of it, with a msgid of zero, which is the relationship Apple documents. */
- (BOOL)sendBeforeDate:(NSDate *)date
		 msgid:(NSUInteger)msgid
	    components:(nullable NSMutableArray *)components
		  from:(nullable NSPort *)receivePort
	      reserved:(NSUInteger)headerSpaceReserved;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSPORT_H */
