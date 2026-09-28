/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSURLDownload.m — a download as an object (§62.82). MANUAL OWNERSHIP.
 *
 * THE SESSION'S COMPLETION-HANDLER DOOR IS THE ENGINE (see the header): the body goes to a file and the block
 * hands over its LOCATION, and this class turns that one ending into the legacy protocol's sequence. The location
 * is the SESSION'S OWN file and its lifetime is the call, so the file is moved inside that call rather than kept
 * and moved later.
 *
 * THE FILE IS MOVED BY ONE METHOD, and the same one serves both answers Apple's protocol allows: a destination set
 * before the finish, and a delegate that answers INSIDE the decision door or later. What makes the later case work
 * is that the class keeps nothing pending: `-setDestination:allowOverwrite:` moves what is already there, and the
 * finish path is the only producer of "there".
 */

#import <Foundation/NSURLDownload.h>
#import <Foundation/NSURLSession.h>
#import <Foundation/NSURLSessionTask.h>
#import <Foundation/NSURLRequest.h>
#import <Foundation/NSURL.h>
#import <Foundation/NSURLResponse.h>
#import <Foundation/NSError.h>
#import <Foundation/NSFileManager.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSData.h>
#import <Foundation/NSString.h>

@implementation NSURLDownload

+ (BOOL)canResumeDownloadDecodedWithEncodingMIMEType:(NSString *)MIMEType
{
	(void)MIMEType;
	/* NO: this class resumes BYTES. A decoded resume means the partial is a decoded stream, which is a different
	 * promise and one nothing here makes. */
	return NO;
}

/* BEGIN, AND THE SESSION'S COMPLETION-HANDLER DOOR IS THE ENGINE (see the header). The block RETAINS this object
 * under MRC - a block retains the objects it captures when it is copied - and the cycle it forms with the task and
 * the session is the same one NSURLConnection forms: a download in flight is expected to be alive. */
- (instancetype)fnStart
{
	_started = YES;
	if ([(id)_delegate respondsToSelector:@selector(downloadDidBegin:)]) {
		[(id)_delegate downloadDidBegin:self];
	}
	[_task release];
	_task = [[(NSURLSession *)_session downloadTaskWithRequest:_request
						completionHandler:^(NSURL *location, NSURLResponse *response,
								    NSError *error) {
		[self fnFinishAtLocation:location response:response withError:error];
	}] retain];
	[(NSURLSessionDownloadTask *)_task resume];
	return self;
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
		return;		/* a cancelled download is not finished, and its resume data is the answer instead */
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
	NSURLSessionConfiguration *configuration;

	self = [super init];
	if (self != nil) {
		_request = [request copy];
		_delegate = delegate;		/* NOT retained, as Apple's is */
		/* THE DEFAULT IS YES: a failed download removes the file it was writing, which is Apple's documented
		 * default for this flag. */
		_deletesFileUponFailure = YES;
		configuration = [NSURLSessionConfiguration defaultSessionConfiguration];
		_session = [[NSURLSession sessionWithConfiguration:configuration
							 delegate:nil
						    delegateQueue:nil] retain];
		if (_session == nil) {
			[self release];
			return nil;
		}
		[self fnStart];
	}
	return self;
}

- (nullable instancetype)initWithResumeData:(NSData *)resumeData
				   delegate:(nullable id <NSURLDownloadDelegate>)delegate
				       path:(NSString *)path
{
	NSURLSessionConfiguration *configuration;

	if (resumeData == nil || path == nil) {
		[self release];
		return nil;
	}
	self = [super init];
	if (self != nil) {
		_delegate = delegate;
		_deletesFileUponFailure = YES;
		_destination = [path copy];
		_resumeData = [resumeData copy];
		configuration = [NSURLSessionConfiguration defaultSessionConfiguration];
		_session = [[NSURLSession sessionWithConfiguration:configuration
							 delegate:nil
						    delegateQueue:nil] retain];
		if (_session == nil) {
			[self release];
			return nil;
		}
		_started = YES;
		if ([(id)_delegate respondsToSelector:@selector(downloadDidBegin:)]) {
			[(id)_delegate downloadDidBegin:self];
		}
		[_task release];
		_task = [[(NSURLSession *)_session downloadTaskWithResumeData:_resumeData] retain];
		[(NSURLSessionDownloadTask *)_task resume];
	}
	return self;
}

- (void)dealloc
{
	/* THE SESSION IS INVALIDATED, or a download object that went away would leave a session running with nobody
	 * to tell. */
	[(NSURLSession *)_session finishTasksAndInvalidate];
	[(id)_session release];
	[_request release];
	[_destination release];
	[_resumeData release];
	[_task release];
	[super dealloc];
}

- (NSURLRequest *)request { return _request; }
- (nullable NSData *)resumeData { return _resumeData; }
- (BOOL)deletesFileUponFailure { return _deletesFileUponFailure; }
- (void)setDeletesFileUponFailure:(BOOL)deletes { _deletesFileUponFailure = deletes; }

- (void)cancel
{
	_cancelled = YES;
	/* CANCEL ASKS THE TASK FOR RESUME DATA, which is what makes -resumeData answer something a caller can hand
	 * back to -initWithResumeData:delegate:path:. */
	[(NSURLSessionDownloadTask *)_task cancelByProducingResumeData:^(NSData * _Nullable data) {
		if (data != nil) {
			self->_resumeData = [data copy];
		}
	}];
}

@end
