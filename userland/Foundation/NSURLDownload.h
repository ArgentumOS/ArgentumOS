/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * NSURLDownload — A DOWNLOAD AS AN OBJECT, WITH A DELEGATE THAT DECIDES WHERE IT LANDS (§62.82), and the last
 * rows of `Networking / Legacy / URL Download`.
 *
 * THE ENGINE IS REAL AND IT IS THE SESSION'S: this class drives an `NSURLSessionDownloadTask` through an
 * `NSURLSession` it owns, using the session's COMPLETION-HANDLER door — the one §62.76 proved round-trips a real
 * body byte for byte — and translates that one ending into the legacy protocol's promises: BEGIN, the destination
 * decision, the destination created, FINISH and FAILURE.
 *
 * NOTHING HAS TO BE STARTED FIRST: this library's transport (`FNCURLURLProtocol`, which serves `file`, `http` and
 * `https`) is registered AT LOAD, so a download runs out of the box. That is worth one sentence because it used to
 * be the opposite — the registry shipped empty and a forgotten `+registerClass:` surfaced here as
 * `NSURLErrorUnsupportedURL`, which reads like a verdict about the URL. §62.83 changed it; a caller may still
 * register a transport of its own, which outranks this one.
 *
 * WHERE THE DESTINATION COMES FROM IS APPLE'S ASYNCHRONOUS PROTOCOL, HONOURED RATHER THAN SIMPLIFIED:
 * `-download:decideDestinationWithSuggestedFilename:` is sent, and the delegate ANSWERS BY CALLING
 * `-setDestination:allowOverwrite:` — possibly later, which this class supports by moving the file when the answer
 * arrives. Two fallbacks are stated because they are ours: a destination set BEFORE the finish wins outright, and
 * a download whose delegate never chooses keeps the file in the temporary directory under the suggested name,
 * reported through `-download:didCreateDestination:` so a caller still learns where it is. WHEN THAT NAME IS
 * TAKEN the fallback picks a free sibling (`name-1.txt`, `name-2.txt`) instead of replacing it - a `file:` source
 * suggests its OWN name, so the first fallback destination this class ever chose was the file being downloaded,
 * and overwriting a caller's file to satisfy a fallback of ours is not a trade this class makes.
 *
 * AND THE DOORS THIS ENGINE DOES NOT DRIVE ARE NAMED RATHER THAN LEFT TO BE DISCOVERED: the four authentication
 * doors, `-download:didReceiveResponse:` (the session's download protocol has no response door),
 * `-download:shouldDecodeSourceDataOfMIMEType:` (nothing here decodes) and
 * `-download:willSendRequest:redirectResponse:`; and the two the completion-handler door cannot feed —
 * `-download:didReceiveDataOfLength:` and `-download:willResumeWithResponse:fromByte:` — because that door reports
 * one ending and no per-chunk progress. They are declared because Apple's protocol declares them, and a delegate
 * that implements them is compiled and never called, which a reader is told here.
 *
 * RESUME IS BYTE-LEVEL ONLY: `-cancel` asks the task for resume data, `-resumeData` answers it, and
 * `-initWithResumeData:delegate:path:` consumes it. `+canResumeDownloadDecodedWithEncodingMIMEType:` answers NO,
 * because a DECODED resume is a different promise and nothing here decodes.
 *
 * THE DELEGATE IS CALLED ON THE LOADING SYSTEM'S THREAD, the deviation NSURLConnection.h documents: the run loop
 * has no door to hand work to another thread, so Apple's "the thread you started it on" cannot be honoured and a
 * caller that shares state with its delegate must synchronise.
 */

#import <Foundation/NSObject.h>

NS_ASSUME_NONNULL_BEGIN

@class NSData;
@class NSError;
@class NSString;
@class NSURLAuthenticationChallenge;
@class NSURLProtectionSpace;
@class NSURLRequest;
@class NSURLResponse;
@class NSURLDownload;

@protocol NSURLDownloadDelegate <NSObject>
@optional
- (BOOL)download:(NSURLDownload *)download
    canAuthenticateAgainstProtectionSpace:(NSURLProtectionSpace *)protectionSpace;
- (void)download:(NSURLDownload *)download
    decideDestinationWithSuggestedFilename:(NSString *)filename;
- (void)download:(NSURLDownload *)download
    didCancelAuthenticationChallenge:(NSURLAuthenticationChallenge *)challenge;
- (void)download:(NSURLDownload *)download didCreateDestination:(NSString *)path;
- (void)download:(NSURLDownload *)download didFailWithError:(NSError *)error;
- (void)download:(NSURLDownload *)download
    didReceiveAuthenticationChallenge:(NSURLAuthenticationChallenge *)challenge;
- (void)download:(NSURLDownload *)download didReceiveResponse:(NSURLResponse *)response;
- (void)download:(NSURLDownload *)download didReceiveDataOfLength:(NSUInteger)length;
- (BOOL)download:(NSURLDownload *)download
    shouldDecodeSourceDataOfMIMEType:(NSString *)encodingType;
- (void)download:(NSURLDownload *)download
    willResumeWithResponse:(NSURLResponse *)response
		  fromByte:(long long)startingByte;
- (nullable NSURLRequest *)download:(NSURLDownload *)download
		      willSendRequest:(NSURLRequest *)request
		   redirectResponse:(nullable NSURLResponse *)redirectResponse;
- (void)downloadDidBegin:(NSURLDownload *)download;
- (void)downloadDidFinish:(NSURLDownload *)download;
- (BOOL)downloadShouldUseCredentialStorage:(NSURLDownload *)download;
@end

@interface NSURLDownload : NSObject
{
@private
	NSURLRequest *_request;			/* retained */
	id _delegate;				/* NOT retained: it owns the download, as Apple's does */
	NSString *_destination;			/* retained, once one has been chosen or defaulted */
	NSData *_resumeData;			/* retained, when a cancelled download produced some */
	id _session;				/* the NSURLSession this download owns */
	id _task;				/* the NSURLSessionDownloadTask carrying the bytes */
	BOOL _allowOverwrite;
	BOOL _deletesFileUponFailure;
	BOOL _cancelled;
	BOOL _finished;
	BOOL _started;
}

/* NO: A DECODED RESUME IS A DIFFERENT PROMISE, and nothing here decodes. Byte-level resume is supported - see the
 * note above. */
+ (BOOL)canResumeDownloadDecodedWithEncodingMIMEType:(NSString *)MIMEType;

/* BEGIN. The request is copied, the delegate is not retained, and the download starts before this returns. */
- (instancetype)initWithRequest:(NSURLRequest *)request
		       delegate:(nullable id <NSURLDownloadDelegate>)delegate;

/* BEGIN FROM RESUME DATA, with the destination the resumed download is to land in. */
- (nullable instancetype)initWithResumeData:(NSData *)resumeData
				   delegate:(nullable id <NSURLDownloadDelegate>)delegate
				       path:(NSString *)path;

/* WHERE IT LANDS. A destination set here wins over the delegate's decision, and setting one AFTER the bytes have
 * arrived moves the file then - which is what makes Apple's asynchronous answer expressible. */
- (void)setDestination:(NSString *)path allowOverwrite:(BOOL)allowOverwrite;

- (void)cancel;
- (NSURLRequest *)request;
- (nullable NSData *)resumeData;
- (BOOL)deletesFileUponFailure;
- (void)setDeletesFileUponFailure:(BOOL)deletes;

@end

NS_ASSUME_NONNULL_END
