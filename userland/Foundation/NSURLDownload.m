/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSURLDownload.m — a download as an object (§62.82), re-based on `NSURLProtocol` (§63.157). MANUAL OWNERSHIP.
 *
 * THE TRANSPORT IS AN `NSURLProtocol` AND THIS CLASS IS ITS CLIENT — the same re-base `NSURLConnection` took in
 * §63.153, and for the same measured reason: this class used to drive an `NSURLSessionDownloadTask` through a
 * session it owned, so a 10.2 keeper was built on a class the 10.2 surface cut removes (§63.140's shape).
 *
 * DECISION 1: THE SEAM HANDS OVER CHUNKS, SO THE BODY IS ACCUMULATED AND WRITTEN AT THE ENDING. The session's
 * door handed over a FILE, and every destination rule below is written in terms of a file that already exists —
 * so the re-base writes the accumulated body to one temporary file at the ending and then runs those rules
 * UNCHANGED. Nothing about the destination protocol moved.
 *
 * DECISION 2: THE ENDING OWNS THE LIFETIME, AND THE CYCLE IS BROKEN THERE. The protocol retains this object as its
 * client (`-client` is a strong property), so releasing the protocol is what gives up that retain — which means
 * the teardown can be the release that frees `self` while `self` is the receiver. Every ending therefore takes a
 * GUARD retain first and gives up exactly one reference at the end. There is no `-start` retain to balance: unlike
 * a connection, a download does not promise to keep itself alive, and the protocol's own client retain is what
 * keeps it alive for as long as the transfer runs.
 *
 * DECISION 3: `-cancel` STOPS THE TRANSPORT AND REPORTS NOTHING, WHICH IS APPLE'S CONTRACT FOR THIS CLASS. It also
 * produces NO RESUME DATA, and that is a fact about the seam rather than an omission: resume data is a session's
 * to produce (§63.157), so `-resumeData` stays nil and `-initWithResumeData:delegate:path:` answers nil.
 */
#import <Foundation/NSURLDownload.h>
#import <Foundation/NSURLProtocol.h>
#import <Foundation/NSCachedURLResponse.h>
#import <Foundation/NSURLRequest.h>
#import <Foundation/NSURLResponse.h>
#import <Foundation/NSURLError.h>	/* NSURLErrorDomain and NSURLErrorUnsupportedURL */
#import <Foundation/NSData.h>		/* NSMutableData is declared here too */
#import <Foundation/NSError.h>
#import <Foundation/NSFileManager.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSString.h>
#import <Foundation/NSURL.h>
#import <Foundation/NSURLAuthenticationChallenge.h>
#import <Foundation/NSURLCredential.h>
#import <Foundation/NSURLProtectionSpace.h>

#include <unistd.h>

/* THE TEMPORARY FILES GET A NAME NOTHING ELSE WILL PICK: two downloads in one process can be running at the
 * same time, and a fixed name would have them writing into each other's file. */
static unsigned long fn_download_serial = 0;

/* THE CLIENT CONFORMANCE IS DECLARED HERE RATHER THAN IN THE HEADER: it is HOW THIS CLASS DRIVES A TRANSPORT
 * rather than part of the class's own API — a caller never sees these doors. */
@interface NSURLDownload () <NSURLProtocolClient>
- (instancetype)fnStart;
- (void)fnBeginTransferWithRequest:(NSURLRequest *)request;
- (void)fnTearDown;
- (void)fnFinishWithError:(nullable NSError *)error;
- (NSString *)fnMakeTempPath;
- (NSError *)fnUnsupportedURLError;
- (NSError *)fnBodyWriteError;
@end

@implementation NSURLDownload

+ (BOOL)canResumeDownloadDecodedWithEncodingMIMEType:(NSString *)MIMEType
{
	(void)MIMEType;
	/* NO: this class resumes BYTES. A decoded resume means the partial is a decoded stream, which is a different
	 * promise and one nothing here makes. */
	return NO;
}

/* BEGIN, AND THE TRANSPORT IS THE ENGINE (see the header): the protocol class is consulted through
 * `+fnProtocolClassForRequest:` — the door §52 shipped for exactly this step — and the transfer runs with this
 * object as its client. */
- (instancetype)fnStart
{
	_started = YES;
	if ([(id)_delegate respondsToSelector:@selector(downloadDidBegin:)]) {
		[(id)_delegate downloadDidBegin:self];
	}
	[self fnBeginTransferWithRequest:_request];
	return self;
}

/* THE ONE PLACE A TRANSFER BEGINS, and both callers are here rather than duplicated: `fnStart` and the redirect
 * door, which re-issues with the request the delegate answered. */
- (void)fnBeginTransferWithRequest:(NSURLRequest *)request
{
	Class protocolClass = [NSURLProtocol fnProtocolClassForRequest:request];
	NSURLProtocol *previous = _protocol;
	NSURLProtocol *protocol;
	NSURLRequest *canonical;

	if (protocolClass == Nil) {
		[self fnFinishWithError:[self fnUnsupportedURLError]];
		return;
	}
	canonical = [protocolClass canonicalRequestForRequest:request];
	protocol = [[protocolClass alloc] initWithRequest:canonical cachedResponse:nil client:self];
	if (protocol == nil) {
		[self fnFinishWithError:[self fnUnsupportedURLError]];
		return;
	}
	/* THE NEW ONE IS IN PLACE BEFORE THE OLD ONE IS GIVEN UP, and the old one is AUTORELEASED rather than
	 * released: on a REDIRECT it is the protocol whose own frame is on the stack — the bridge reports the 3xx
	 * from inside `curl_easy_perform` and goes on to report its ending — and the bridge's transfer runs inside
	 * an autorelease pool of its own that drains at the very end. That is exactly the lifetime it needs. */
	_protocol = protocol;
	[previous autorelease];
	[protocol startLoading];
}

/* THE FALLBACK DESTINATION, WHICH IS OURS AND IS STATED: a download whose delegate never chose keeps the file in
 * the temporary directory under the suggested name, so the bytes survive and the caller is told where they are.
 *
 * AND A NAME THAT IS TAKEN IS SIDESTEPPED RATHER THAN OVERWRITTEN, which this class learned by MEASURING it: a
 * `file:` source suggests its OWN name, so the very first fallback destination this class ever chose was the file
 * being downloaded - and a plain move onto it failed with `EEXIST`, failing a download that had nothing wrong
 * with it. The fallback is OUR choice, so it is ours to make room for: a free sibling (`name-1.txt`, `name-2.txt`)
 * is picked instead. The delegate's own answer keeps Apple's semantics untouched - THERE, `-allowOverwrite:` is
 * the caller's decision and is honoured exactly as given. */
- (NSString *)fnFallbackDestinationFor:(nullable NSString *)suggested
{
	NSString *name = suggested != nil && [suggested length] > 0 ? suggested : @"download";
	NSString *directory = NSTemporaryDirectory();
	NSString *candidate = [directory stringByAppendingPathComponent:name];
	NSFileManager *manager = [NSFileManager defaultManager];

	if (![manager fileExistsAtPath:candidate]) {
		return candidate;
	}
	{
		NSString *stem = [name stringByDeletingPathExtension];
		NSString *extension = [name pathExtension];
		int n;

		for (n = 1; n <= 9999; n++) {
			NSString *trial = [stem stringByAppendingFormat:@"-%d", n];

			if ([extension length] > 0) {
				trial = [trial stringByAppendingPathExtension:extension];
			}
			candidate = [directory stringByAppendingPathComponent:trial];
			if (![manager fileExistsAtPath:candidate]) {
				return candidate;
			}
		}
	}
	/* The only way here is a directory already holding 9999 siblings of one suggested name. Replacing one of them
	 * is better than losing the download, and saying so is better than leaving a silent corner. */
	_allowOverwrite = YES;
	return [directory stringByAppendingPathComponent:name];
}

/* THE ONE PLACE A FILE IS PUT WHERE IT BELONGS. `overwrite` decides what happens to an existing file, and a move
 * that cannot happen is reported through the failure door rather than silently leaving the download nowhere. */
- (BOOL)fnMoveFrom:(NSString *)source to:(NSString *)destination overwrite:(BOOL)overwrite
{
	NSFileManager *manager = [NSFileManager defaultManager];
	NSError *error = nil;

	if (overwrite) {
		(void)[manager removeItemAtPath:destination error:NULL];
	}
	if (![manager moveItemAtPath:source toPath:destination error:&error]) {
		if ([(id)_delegate respondsToSelector:@selector(download:didFailWithError:)]) {
			[(id)_delegate download:self didFailWithError:error];
		}
		return NO;
	}
	if ([(id)_delegate respondsToSelector:@selector(download:didCreateDestination:)]) {
		[(id)_delegate download:self didCreateDestination:destination];
	}
	return YES;
}

/* THE ENDING, IN ONE PLACE: the destination is resolved, the file moved, the destination reported, and then either
 * the finish or the failure - never both. */
- (void)fnFinishAtLocation:(nullable NSURL *)location
		  response:(nullable NSURLResponse *)response
		 withError:(nullable NSError *)error
{
	if (_cancelled) {
		return;		/* a cancelled download is not finished, and it reports nothing at all */
	}
	if (error != nil) {
		if (_destination != nil && _deletesFileUponFailure) {
			(void)[[NSFileManager defaultManager] removeItemAtPath:_destination error:NULL];
		}
		if ([(id)_delegate respondsToSelector:@selector(download:didFailWithError:)]) {
			[(id)_delegate download:self didFailWithError:error];
		}
		return;
	}
	if (_destination == nil) {
		NSString *suggested = [[response suggestedFilename] copy];

		if ([(id)_delegate respondsToSelector:
			@selector(download:decideDestinationWithSuggestedFilename:)]) {
			[(id)_delegate download:self
			    decideDestinationWithSuggestedFilename:suggested != nil ? suggested : @"download"];
		}
		if (_destination == nil) {
			_destination = [[self fnFallbackDestinationFor:suggested] retain];
		}
		[suggested release];
	}
	{
		NSString *source = location != nil ? [location path] : nil;

		if (source != nil && ![_destination isEqualToString:source]) {
			(void)[self fnMoveFrom:source to:_destination overwrite:_allowOverwrite];
		} else if ([(id)_delegate respondsToSelector:@selector(download:didCreateDestination:)]) {
			[(id)_delegate download:self didCreateDestination:_destination];
		}
	}
	_finished = YES;
	if ([(id)_delegate respondsToSelector:@selector(downloadDidFinish:)]) {
		[(id)_delegate downloadDidFinish:self];
	}
}

/* SET AFTER THE BYTES ARRIVED MEANS MOVE THEM NOW - which is what makes Apple's ASYNCHRONOUS answer expressible: a
 * delegate may call this from inside the decision door (the ordinary case) or later, and this method is where
 * both land. */
- (void)setDestination:(NSString *)path allowOverwrite:(BOOL)allowOverwrite
{
	NSString *kept = [path copy];

	[_destination release];
	_destination = kept;
	_allowOverwrite = allowOverwrite;
	/* A destination chosen AFTER the finish has nothing left to move: the fallback already put the file
	 * somewhere, and this class does not keep the location past its call (see the file's note). A later answer
	 * therefore replaces the RECORD rather than the file, which the header states. */
}

- (instancetype)initWithRequest:(NSURLRequest *)request
		       delegate:(nullable id <NSURLDownloadDelegate>)delegate
{
	self = [super init];
	if (self != nil) {
		_request = [request copy];
		_delegate = delegate;	/* NOT retained, as Apple's is */
		/* THE DEFAULT IS YES: a failed download removes the file it was writing, which is Apple's documented
		 * default for this flag. */
		_deletesFileUponFailure = YES;
		[self fnStart];
	}
	return self;
}

/* ⚠⚠ RESUME IS REFUSED AS A FACT RATHER THAN AS AN ABSENCE (§63.157). Resume data is PRODUCED and CONSUMED by a
 * session, and this class no longer owns one — the transport seam has no resume to ask for and none to hand
 * back. THE DECLARATION STAYS because it is Apple's surface (`-initWithResumeData:delegate:path:` is a `shipped`
 * row in the ledger, and `--check` fails if the header stops declaring it), so this answers NIL: a caller that
 * passes resume data is TOLD THE TRUTH rather than handed a download that would silently start from the
 * beginning. */
- (nullable instancetype)initWithResumeData:(NSData *)resumeData
				   delegate:(nullable id <NSURLDownloadDelegate>)delegate
				       path:(NSString *)path
{
	(void)resumeData;
	(void)delegate;
	(void)path;
	[self release];
	return nil;
}

- (void)dealloc
{
	/* NO TEARDOWN OF A RUNNING TRANSFER HERE: a running download is retained BY ITS PROTOCOL (which holds this
	 * object as its client), so this can only run after the ending, when the protocol is already released and
	 * nil. The temporary file needs no attention either: the ending MOVED it to the destination, so what is left
	 * at `_tempPath` is a path that no longer names anything. */
	[_request release];
	[_destination release];
	[_resumeData release];
	[_protocol release];
	[_body release];
	[_response release];
	[_tempPath release];
	[super dealloc];
}

- (NSURLRequest *)request { return _request; }
- (nullable NSData *)resumeData { return _resumeData; }
- (BOOL)deletesFileUponFailure { return _deletesFileUponFailure; }
- (void)setDeletesFileUponFailure:(BOOL)deletes { _deletesFileUponFailure = deletes; }

- (void)cancel
{
	if (_finished) {
		return;
	}
	_cancelled = YES;
	_finished = YES;
	/* NOTHING IS REPORTED — Apple's contract for this class — AND NO RESUME DATA IS PRODUCED, because the
	 * transport cannot produce any (§63.157). `-resumeData` therefore stays nil, which is the truth. */
	[self fnTearDown];
}

/* --- THE ENDING ----------------------------------------------------------------------------------- */

- (void)fnFinishWithError:(nullable NSError *)error
{
	NSURL *location = nil;

	if (_finished) {
		return;	/* exactly one ending, whatever the transport reports and in what order */
	}
	_finished = YES;

	if (_cancelled) {
		/* A CANCELLED DOWNLOAD REPORTS NOTHING. It can get here when the transport reports after `-cancel`;
		 * `-cancel` itself has already torn the transfer down. */
		return;
	}

	/* THE GUARD: the teardown below gives up the protocol's retain of this object, so it can be the release
	 * that frees `self` while `self` is the receiver. */
	[self retain];

	if (error == nil) {
		/* THE BODY BECOMES A FILE, WHICH IS THIS CLASS'S CONTRACT. A body that cannot be written is a
		 * FAILURE rather than a finish with no file anywhere. */
		if (_body == nil) {
			_body = [[NSMutableData alloc] init];
		}
		_tempPath = [[self fnMakeTempPath] retain];
		if (![_body writeToFile:_tempPath atomically:YES]) {
			error = [self fnBodyWriteError];
		} else {
			location = [NSURL fileURLWithPath:_tempPath];
		}
		/* THE BYTES ARE ON DISK NOW, so the copy in memory goes: a download is the one thing here that can
		 * hold a very large object. */
		[_body release];
		_body = nil;
	}

	[self fnFinishAtLocation:location response:_response withError:error];
	[self fnTearDown];
	[self release];	/* the guard */
}

- (void)fnTearDown
{
	NSURLProtocol *protocol = _protocol;

	/* CLEARED FIRST, so a callback that arrives while the transport is being stopped finds a download that is
	 * finished and a protocol of nil — and messaging nil is the no-op this class wants there. */
	_protocol = nil;
	[protocol stopLoading];
	[protocol release];	/* gives up the protocol's own retain of this object as its client */
}

- (NSString *)fnMakeTempPath
{
	return [NSTemporaryDirectory() stringByAppendingPathComponent:
		[NSString stringWithFormat:@"NSURLDownload-%d-%lu.tmp", (int)getpid(),
		   ++fn_download_serial]];
}

- (NSError *)fnUnsupportedURLError
{
	return [NSError errorWithDomain:NSURLErrorDomain
				   code:NSURLErrorUnsupportedURL
			       userInfo:@{NSLocalizedDescriptionKey:
					@"no registered protocol claims this download's URL"}];
}

- (NSError *)fnBodyWriteError
{
	return [NSError errorWithDomain:@"NSURLDownload"
				   code:2
			       userInfo:@{NSLocalizedDescriptionKey:
					@"the downloaded body could not be written to the temporary directory"}];
}

/* --- THE CLIENT DOORS ------------------------------------------------------------------------------
 *
 * EVERY ONE OF THESE IS A TRANSLATION INTO THIS CLASS'S DELEGATE PROTOCOL. FIVE OF THEM COULD NOT BE DRAWN AT
 * ALL UNDER THE SESSION ENGINE (§63.157) — the response, the per-chunk progress, the redirect and the two
 * authentication doors — and that is the other half of why this re-base was worth doing. */

- (void)URLProtocol:(NSURLProtocol *)protocol
    didReceiveResponse:(NSURLResponse *)response
     cacheStoragePolicy:(NSURLCacheStoragePolicy)policy
{
	(void)protocol;
	(void)policy;	/* an ADVICE for a store, and this class has none to advise */

	[response retain];
	[_response release];
	_response = response;
	if ([(id)_delegate respondsToSelector:@selector(download:didReceiveResponse:)]) {
		[(id)_delegate download:self didReceiveResponse:_response];
	}
}

- (void)URLProtocol:(NSURLProtocol *)protocol didLoadData:(NSData *)data
{
	(void)protocol;

	if (_body == nil) {
		_body = [[NSMutableData alloc] init];
	}
	[_body appendData:data];
	_received += [data length];
	/* THE PER-CHUNK PROGRESS DOOR, WHICH THE SESSION'S COMPLETION-HANDLER COULD NOT FEED: the seam reports every
	 * chunk, so the length reported is the length that just arrived. */
	if ([(id)_delegate respondsToSelector:@selector(download:didReceiveDataOfLength:)]) {
		[(id)_delegate download:self didReceiveDataOfLength:[data length]];
	}
}

/* SOMEWHERE ELSE. The seam REPORTS the 3xx and stops, so following is this class's act: the delegate is asked
 * (Apple's door and Apple's return value) and its answer is what the next transfer runs. */
- (void)URLProtocol:(NSURLProtocol *)protocol
    wasRedirectedToRequest:(NSURLRequest *)request
	 redirectResponse:(NSURLResponse *)redirectResponse
{
	NSURLRequest *next = request;

	(void)protocol;

	if ([(id)_delegate respondsToSelector:
			@selector(download:willSendRequest:redirectResponse:)]) {
		next = [(id <NSURLDownloadDelegate>)_delegate download:self
						       willSendRequest:request
						    redirectResponse:redirectResponse];
	}
	if (next == nil) {
		/* ⚠ DO NOT FOLLOW, AND FOR A DOWNLOAD THAT IS A FAILURE RATHER THAN A RESPONSE: a connection hands
		 * the 3xx back as the answer because a caller asked for a RESPONSE, while a download's whole contract
		 * is the BODY — there is none behind a 3xx this class was told not to follow, so finishing would put an
		 * empty file where a caller expects bytes. The error is this class's own, and it says which choice was
		 * made. */
		[self fnFinishWithError:
			[NSError errorWithDomain:@"NSURLDownload"
					    code:1
					userInfo:@{NSLocalizedDescriptionKey:
						@"the download's delegate refused the redirect, so no body "
						@"was fetched"}]];
		return;
	}
	[self fnBeginTransferWithRequest:next];
}

- (void)URLProtocol:(NSURLProtocol *)protocol cachedResponseIsValid:(NSCachedURLResponse *)cachedResponse
{
	/* NOTHING TO DO, AND IT IS NOT A REFUSAL: this class never hands a cached answer to a protocol — it passes
	 * `nil` for `cachedResponse:` in every transfer it starts — so this notification cannot arise from anything
	 * here. It stays implemented because the CLIENT protocol requires it of every client. */
	(void)protocol;
	(void)cachedResponse;
}

/* THE AUTHENTICATION DOORS, WITH APPLE'S ORDER FOR *THIS* PROTOCOL: it declares no modern door, so the GATE is
 * asked first and the challenge door second — and the answer goes back through the challenge's SENDER, which IS
 * the continuation the transport is blocked on. Calling that continuation here as well would answer one challenge
 * twice. */
- (void)URLProtocol:(NSURLProtocol *)protocol
    didReceiveAuthenticationChallenge:(NSURLAuthenticationChallenge *)challenge
		  completionHandler:(void (^)(NSURLSessionAuthChallengeDisposition,
					      NSURLCredential * _Nullable))completionHandler
{
	id delegate = _delegate;

	(void)protocol;

	if ([delegate respondsToSelector:
			@selector(download:canAuthenticateAgainstProtectionSpace:)]) {
		if (![(id <NSURLDownloadDelegate>)delegate download:self
				canAuthenticateAgainstProtectionSpace:[challenge protectionSpace]]) {
			completionHandler(NSURLSessionAuthChallengePerformDefaultHandling, nil);
			return;
		}
	}
	if ([delegate respondsToSelector:@selector(download:didReceiveAuthenticationChallenge:)]) {
		[(id <NSURLDownloadDelegate>)delegate download:self
				didReceiveAuthenticationChallenge:challenge];
		return;	/* answered through the sender */
	}
	completionHandler(NSURLSessionAuthChallengePerformDefaultHandling, nil);
}

- (void)URLProtocolDidFinishLoading:(NSURLProtocol *)protocol
{
	(void)protocol;
	[self fnFinishWithError:nil];
}

- (void)URLProtocol:(NSURLProtocol *)protocol didFailWithError:(NSError *)error
{
	(void)protocol;
	[self fnFinishWithError:error];
}

@end
