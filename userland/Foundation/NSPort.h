/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSPort — A COMMUNICATION ENDPOINT, WHICH HERE IS A DESCRIPTOR THE RUN LOOP WATCHES. docs/design/
 * foundation-plan.md W6b and §43.
 *
 * THE CLASS IS ALMOST ALL STRUCK, AND WHAT SURVIVES IS THE PART THE RUN LOOP NEEDS. Apple gives NSPort a
 * message type, a delegate protocol, three concrete subclasses and a connection class, and EVERY ONE of
 * those was deprecated and removed by §11.5:
 *
 *   NSPortMessage          the object a port sent and received
 *   NSPortDelegate         the protocol that received it
 *   NSMachPort, NSMessagePort, NSSocketPortNameServer
 *   NSConnection           distributed objects
 *
 * So `-sendBeforeDate:components:from:reserved:` has no component type to carry, `-setDelegate:` has no
 * protocol to be typed by, and `-addConnection:toRunLoop:forMode:` has no connection. Those names are
 * EXCLUDED BY NAME — the probe's inventory lists each with this reason — and NOT declared at all, which
 * is this tree's rule: a name is not declared until it works.
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
@interface NSPort : NSObject <NSCoding>
{
	BOOL _valid;
	NSRunLoop *_runLoop;		/* the ONE loop this port was last scheduled in (see the header) */
	NSString *_mode;		/* copied */
}

/* "Creates and returns a new NSPort object" — an NSSocketPort here, because it is the only live
 * concrete subclass (see the header). */
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
 * it is struck. A subclass or a consumer overrides this; the base does nothing. */
- (void)portDidBecomeReadable;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSPORT_H */
