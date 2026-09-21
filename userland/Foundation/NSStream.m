/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSStream.m — the abstract head's machinery: the status machine, the property bag, and the run-loop seam.
 * The design and the reasoning are in NSStream.h; what is HERE is what every substream inherits.
 *
 * THE KEY STRINGS ARE OURS. Apple documents the NAMES of these constants and not their values (see the
 * header, and the plan's D2 ruling), so each key's value IS its own name — the conventional spelling, and
 * the one a reader of a configuration file would expect to see. The probe asserts them one by one, which is
 * what makes "ours" a contract rather than a habit.
 *
 * THE RUN-LOOP SEAM IS `NSRunLoop`'s OWN FILE-DESCRIPTOR SOURCE, and it fires a ZERO-ARGUMENT selector on a
 * ZEROING-WEAK target — so the base registers `-fnSourceReady` and the CALLER must keep the stream alive,
 * exactly as the loop's own source contract says. That is why `-scheduleInRunLoop:` stores the loop rather
 * than retaining it for the source's sake: the source weak-references the stream, not the other way round.
 */

#import <Foundation/NSStream.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSError.h>
#import <Foundation/NSRunLoop.h>
#import <Foundation/NSString.h>

/* THE FILE AND MEMORY KEYS - the two this library ACTS on. */
NSStreamPropertyKey const NSStreamFileCurrentOffsetKey = @"NSStreamFileCurrentOffsetKey";
NSStreamPropertyKey const NSStreamDataWrittenToMemoryStreamKey = @"NSStreamDataWrittenToMemoryStreamKey";

/* THE NETWORK HALF: DECLARED, CARRIED, NEVER ACTED ON (the header states the decision and its reason). */
NSStreamPropertyKey const NSStreamSocketSecurityLevelKey = @"NSStreamSocketSecurityLevelKey";
NSStreamSocketSecurityLevel const NSStreamSocketSecurityLevelNone = @"NSStreamSocketSecurityLevelNone";
NSStreamSocketSecurityLevel const NSStreamSocketSecurityLevelSSLv2 = @"NSStreamSocketSecurityLevelSSLv2";
NSStreamSocketSecurityLevel const NSStreamSocketSecurityLevelSSLv3 = @"NSStreamSocketSecurityLevelSSLv3";
NSStreamSocketSecurityLevel const NSStreamSocketSecurityLevelTLSv1 = @"NSStreamSocketSecurityLevelTLSv1";
NSStreamSocketSecurityLevel const NSStreamSocketSecurityLevelNegotiatedSSL =
	@"NSStreamSocketSecurityLevelNegotiatedSSL";

NSStreamPropertyKey const NSStreamSOCKSProxyConfigurationKey = @"NSStreamSOCKSProxyConfigurationKey";
NSStreamPropertyKey const NSStreamSOCKSProxyHostKey = @"NSStreamSOCKSProxyHostKey";
NSStreamPropertyKey const NSStreamSOCKSProxyPortKey = @"NSStreamSOCKSProxyPortKey";
NSStreamPropertyKey const NSStreamSOCKSProxyUserKey = @"NSStreamSOCKSProxyUserKey";
NSStreamPropertyKey const NSStreamSOCKSProxyPasswordKey = @"NSStreamSOCKSProxyPasswordKey";
NSStreamPropertyKey const NSStreamSOCKSProxyVersionKey = @"NSStreamSOCKSProxyVersionKey";
NSStreamSOCKSProxyVersion const NSStreamSOCKSProxyVersion4 = @"NSStreamSOCKSProxyVersion4";
NSStreamSOCKSProxyVersion const NSStreamSOCKSProxyVersion5 = @"NSStreamSOCKSProxyVersion5";

NSStreamPropertyKey const NSStreamNetworkServiceType = @"NSStreamNetworkServiceType";
NSStreamNetworkServiceTypeValue const NSStreamNetworkServiceTypeBackground =
	@"NSStreamNetworkServiceTypeBackground";
NSStreamNetworkServiceTypeValue const NSStreamNetworkServiceTypeVideo = @"NSStreamNetworkServiceTypeVideo";
NSStreamNetworkServiceTypeValue const NSStreamNetworkServiceTypeVoice = @"NSStreamNetworkServiceTypeVoice";
NSStreamNetworkServiceTypeValue const NSStreamNetworkServiceTypeCallSignaling =
	@"NSStreamNetworkServiceTypeCallSignaling";

NSErrorDomain const NSStreamSocketSSLErrorDomain = @"NSStreamSocketSSLErrorDomain";
NSErrorDomain const NSStreamSOCKSErrorDomain = @"NSStreamSOCKSErrorDomain";

@interface NSStream ()
- (void)fnRegisterSource;
- (void)fnUnregisterSource;
- (void)fnSourceReady;
@end

@implementation NSStream

- (instancetype)init
{
	self = [super init];
	if (self != nil) {
		_status = NSStreamStatusNotOpen;
		_properties = [[NSMutableDictionary alloc] init];
		_readsForSource = YES;		/* an input stream is the common case; a substream overrides the hook */
	}
	return self;
}

/* ABSTRACT AND INERT, WHICH IS THE HONEST READING OF APPLE'S "abstract class, subclasses open a resource":
 * the base moves nothing, so an instance used directly stays NotOpen rather than claiming to be open. A
 * substream calls -fnStreamSetStatus:error: when its own resource opens or fails. */
- (void)open
{
}

- (void)close
{
	[self fnUnregisterSource];
}

- (nullable id<NSStreamDelegate>)delegate
{
	return _delegate;
}

- (void)setDelegate:(nullable id<NSStreamDelegate>)delegate
{
	_delegate = delegate;		/* ASSIGN, not retain: Apple's contract is that a stream does not own its
					 * delegate. MRC makes that literal - the caller owns it. */
}

- (nullable id)propertyForKey:(NSStreamPropertyKey)key
{
	if (key == nil) {
		return nil;
	}
	return [_properties objectForKey:key];
}

- (BOOL)setProperty:(nullable id)property forKey:(NSStreamPropertyKey)key
{
	if (key == nil) {
		return NO;
	}
	if (property == nil) {
		[_properties removeObjectForKey:key];
		return YES;
	}
	[_properties setObject:property forKey:key];
	return YES;
}

/* SCHEDULING HAPPENS BEFORE -open (Apple), and it is where readiness becomes an EVENT. The descriptor comes
 * from the substream through the private hook; a stream with none (a memory stream) schedules and simply
 * never fires, which is correct rather than a failure. */
- (void)scheduleInRunLoop:(NSRunLoop *)aRunLoop forMode:(NSRunLoopMode)mode
{
	if (aRunLoop == nil || mode == nil) {
		return;
	}
	[aRunLoop retain];
	[_scheduledRunLoop release];
	_scheduledRunLoop = aRunLoop;
	[mode retain];
	[_scheduledMode release];
	_scheduledMode = mode;
	[self fnRegisterSource];
}

- (void)removeFromRunLoop:(NSRunLoop *)aRunLoop forMode:(NSRunLoopMode)mode
{
	[self fnUnregisterSource];
	[_scheduledRunLoop release];
	_scheduledRunLoop = nil;
	[_scheduledMode release];
	_scheduledMode = nil;
}

- (NSStreamStatus)streamStatus
{
	return _status;
}

- (nullable NSError *)streamError
{
	return _streamError;
}

- (void)dealloc
{
	[self fnUnregisterSource];
	[_properties release];
	[_streamError release];
	[_scheduledRunLoop release];
	[_scheduledMode release];
	[super dealloc];
}

/* ---- the substream hooks: the base's answers, which every substream may override ---- */

- (int)fnStreamDescriptor
{
	return -1;			/* the base has no resource to watch */
}

- (BOOL)fnStreamWatchesReadable
{
	return YES;
}

- (void)fnStreamSetStatus:(NSStreamStatus)status error:(nullable NSError *)error
{
	_status = status;
	/* A SUCCESSFUL MOVE CLEARS THE LAST ERROR, and a failure records one: that is what makes
	 * -streamError readable after -streamStatus has already said Error. */
	if (error != nil || status == NSStreamStatusOpen || status == NSStreamStatusClosed) {
		[error retain];
		[_streamError release];
		_streamError = error;
	}
}

/* TO THE DELEGATE, IF IT WANTS ONE: -stream:handleEvent: is @optional, and a delegate that only wants to be
 * told about an error is entitled to implement nothing. */
- (void)fnStreamDispatch:(NSStreamEvent)eventCode
{
	id<NSStreamDelegate> delegate = _delegate;	/* assign: the caller owns it, and it may be gone */

	if (delegate != nil && (eventCode & NSStreamEventNone) == 0 &&
	    [delegate respondsToSelector:@selector(stream:handleEvent:)]) {
		[delegate stream:self handleEvent:eventCode];
	}
}

/* ---- the run-loop source ---- */

- (void)fnRegisterSource
{
	int fd = [self fnStreamDescriptor];

	if (_sourceRegistered || _scheduledRunLoop == nil || fd < 0) {
		return;
	}
	_readsForSource = [self fnStreamWatchesReadable];
	[_scheduledRunLoop addSourceForFileDescriptor:fd
						 mode:_scheduledMode
					     readable:_readsForSource
					       target:self
					     selector:@selector(fnSourceReady)];
	_sourceRegistered = YES;
}

- (void)fnUnregisterSource
{
	if (!_sourceRegistered) {
		return;
	}
	[_scheduledRunLoop removeSourceForTarget:self];
	_sourceRegistered = NO;
}

/* THE LOOP SAYS "READY", THE STREAM SAYS WHAT THAT MEANS. Which event a ready descriptor is worth is the
 * substream's business (input: bytes or end; output: space), so this maps the half it watches and hands the
 * decision-worthy event on; a substream that needs to distinguish end-of-file re-registers with its own
 * selector. */
- (void)fnSourceReady
{
	if ([self fnStreamDescriptor] < 0) {
		return;
	}
	[self fnStreamDispatch:_readsForSource ? NSStreamEventHasBytesAvailable
					      : NSStreamEventHasSpaceAvailable];
}

@end
