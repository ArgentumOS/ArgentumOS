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
#include <stdio.h>	/* §63.232: snprintf/memset for the service string and the hints */
#include <string.h>	/* §63.232: snprintf/memset for the service string and the hints */
#include <sys/socket.h>	/* §63.232: the pair makers are socket code */
#include <netdb.h>	/* §63.232: the pair makers are socket code */
#include <unistd.h>	/* §63.232: the pair makers are socket code */
#import <Foundation/NSOutputStream.h>	/* §63.232: ditto */
#import <Foundation/NSInputStream.h>	/* §63.232: the pair is made of concrete streams */
#import <Foundation/NSHost.h>	/* §63.232: the host form reads the host's name */
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
NSStreamNetworkServiceTypeValue const NSStreamNetworkServiceTypeVoIP = @"NSStreamNetworkServiceTypeVoIP";
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
	/* AND OPENING IS WHEN A SOURCE BECOMES REGISTRABLE, because Apple requires scheduling BEFORE -open and a
	 * descriptor-backed stream has no descriptor until it opens. So the base tries again here rather than
	 * making every substream remember to. */
	if (status == NSStreamStatusOpen) {
		[self fnRegisterSource];
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

/* THE LOOP SAYS "READY", THE SUBSTREAM SAYS WHAT THAT MEANS - which is not a formality: a descriptor that
 * is ready and EMPTY means END OF STREAM, and only the substream can tell that from bytes arriving. */
- (NSStreamEvent)fnStreamEventForReadiness
{
	return _readsForSource ? NSStreamEventHasBytesAvailable : NSStreamEventHasSpaceAvailable;
}

- (void)fnSourceReady
{
	NSStreamEvent event;

	if ([self fnStreamDescriptor] < 0) {
		return;
	}
	event = [self fnStreamEventForReadiness];
	if (event != NSStreamEventNone) {
		[self fnStreamDispatch:event];
	}
}


+ (void)getStreamsToHostWithName:(NSString *)hostname
			    port:(NSInteger)port
		     inputStream:(NSInputStream **)inputStream
		    outputStream:(NSOutputStream **)outputStream
{
	struct addrinfo hints;
	struct addrinfo *answer = NULL;
	char service[16];
	int fd;

	if (inputStream != NULL) {
		*inputStream = nil;
	}
	if (outputStream != NULL) {
		*outputStream = nil;
	}
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	snprintf(service, sizeof(service), "%d", (int)port);
	if (getaddrinfo([hostname UTF8String], service, &hints, &answer) != 0 || answer == NULL) {
		return;
	}
	fd = socket(answer->ai_family, answer->ai_socktype, answer->ai_protocol);
	if (fd >= 0 && connect(fd, answer->ai_addr, answer->ai_addrlen) != 0) {
		(void)close(fd);
		fd = -1;
	}
	freeaddrinfo(answer);
	if (fd < 0) {
		return;
	}
	/* ONE SOCKET, TWO STREAMS, TWO DESCRIPTORS: each stream closes what it owns, so the second gets a dup. */
	if (inputStream != NULL) {
		*inputStream = [[[NSInputStream alloc] fnInitWithSocketDescriptor:fd] autorelease];
		if (outputStream == NULL) {
			return;
		}
	}
	if (outputStream != NULL) {
		*outputStream = [[[NSOutputStream alloc]
					fnInitWithSocketDescriptor:(inputStream != NULL ? dup(fd) : fd)] autorelease];
	}
}

+ (void)getStreamsToHost:(NSHost *)host
		    port:(NSInteger)port
	     inputStream:(NSInputStream **)inputStream
	    outputStream:(NSOutputStream **)outputStream
{
	/* THE HOST FORM IS THE NAME FORM: NSHost knows its own name, and a second resolution path would be a second
	 * place for the two to disagree. */
	[self getStreamsToHostWithName:[host name] port:port inputStream:inputStream outputStream:outputStream];
}

+ (void)getBoundStreamsWithBufferSize:(NSUInteger)bufferSize
			  inputStream:(NSInputStream **)inputStream
			 outputStream:(NSOutputStream **)outputStream
{
	/* A CONNECTED UNIX PAIR, which is what "bound" means for this door: two ends of one socket, already joined.
	 * `bufferSize` is accepted and not applied — these streams do their own buffering, and the reading is here
	 * rather than implied. */
	int fds[2];

	(void)bufferSize;
	if (inputStream != NULL) {
		*inputStream = nil;
	}
	if (outputStream != NULL) {
		*outputStream = nil;
	}
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
		return;
	}
	if (inputStream != NULL) {
		*inputStream = [[[NSInputStream alloc] fnInitWithSocketDescriptor:fds[0]] autorelease];
	} else {
		(void)close(fds[0]);
	}
	if (outputStream != NULL) {
		*outputStream = [[[NSOutputStream alloc] fnInitWithSocketDescriptor:fds[1]] autorelease];
	} else {
		(void)close(fds[1]);
	}
}
@end
