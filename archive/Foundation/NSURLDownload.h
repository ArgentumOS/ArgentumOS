/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * NSURLDownload — A DOWNLOAD AS AN OBJECT, WITH A DELEGATE THAT DECIDES WHERE IT LANDS (§62.82), and the last
 * rows of `Networking / Legacy / URL Download`.
 *
 * THE ENGINE IS REAL AND, SINCE §63.157, IT IS `NSURLProtocol`: this class finds the protocol class that claims
 * the request, creates ONE instance with ITSELF AS THE CLIENT, calls `-startLoading`, and translates the client
 * doors into the legacy protocol's promises: BEGIN, the response, per-chunk progress, the destination decision,
 * the destination created, FINISH and FAILURE. **IT USED TO DRIVE AN `NSURLSessionDownloadTask` THROUGH A SESSION
 * IT OWNED** — the same §63.140 shape `NSURLConnection` was in: a 10.2 keeper built on a class the 10.2 surface
 * cut removes. The seam hands over chunks rather than a file, so THE BODY IS ACCUMULATED AND WRITTEN ONCE AT THE
 * ENDING, and the destination logic below is unchanged: it still moves a file from where the bytes are to where
 * the caller wants them.
 *
 * ⚠⚠ AND THAT RE-BASE MADE FIVE DOORS LIVE THAT THE SESSION ENGINE COULD NOT FEED. `-download:didReceiveResponse:`
 * (the seam HAS a response door), `-download:didReceiveDataOfLength:` (the seam reports every chunk, so per-chunk
 * progress is real), `-download:willSendRequest:redirectResponse:` (the seam reports the 3xx and this class runs
 * the next transfer) and the two authentication doors
 * (`-download:canAuthenticateAgainstProtectionSpace:` and `-download:didReceiveAuthenticationChallenge:`, answered
 * through the challenge's SENDER) are all driven now. **FOUR REMAIN REFUSED, EACH WITH ITS GROUND STATED:**
 * `-download:didCancelAuthenticationChallenge:` (one challenge at a time, answered synchronously),
 * `-download:downloadShouldUseCredentialStorage:` (the loading system consults no credential store),
 * `-download:shouldDecodeSourceDataOfMIMEType:` (nothing here decodes) and
 * `-download:willResumeWithResponse:fromByte:` (resume, below).
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
 * ⚠⚠ RESUME IS REFUSED AT RUNTIME, AND THE DECLARATIONS STAY BECAUSE APPLE'S DO (§63.157). `-cancel` cannot ask
 * the transport for resume data and `-initWithResumeData:delegate:path:` has nothing that can consume any: resume
 * data is produced and consumed by the SESSION, and this class no longer owns one. SO `-initWithResumeData:…`
 * ANSWERS NIL, `-cancel` cancels, `-resumeData` stays nil, and the two declarations remain because they are
 * Apple's surface — a caller that passes resume data is told the truth (a nil object) rather than handed a
 * download that would silently start from the beginning. `+canResumeDownloadDecodedWithEncodingMIMEType:` still
 * answers NO for its own reason: a DECODED resume is a different promise, and nothing here decodes.
 *
 * THE DELEGATE IS CALLED ON THE TRANSPORT'S THREAD, the deviation NSURLConnection.h documents: the run loop
 * has no door to hand work to another thread, so Apple's "the thread you started it on" cannot be honoured and a
 * caller that shares state with its delegate must synchronise.
 */

#import <Foundation/NSObject.h>

NS_ASSUME_NONNULL_BEGIN

@class NSData;
@class NSMutableData;
@class NSError;
@class NSString;
@class NSURLAuthenticationChallenge;
@class NSURLProtectionSpace;
@class NSURLProtocol;
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
	/* THE TRANSPORT INSTANCE this download drives, retained; nil before the transfer starts and after
	 * the ending. IT RETAINS THIS OBJECT AS ITS CLIENT, so the pair is a cycle and the ending is where it
	 * is broken — the same shape NSURLConnection uses (§63.153). */
	NSURLProtocol *_protocol;
	NSMutableData *_body;			/* THE BODY AS IT ARRIVES: the seam hands over chunks and this
						 * class's contract is a FILE, so the bytes are accumulated and written
						 * once at the ending. */
	NSURLResponse *_response;		/* retained: the ending needs its suggested filename */
	NSString *_tempPath;			/* retained: where `_body` was written, and the path the move
						 * reads from — the file is GONE once it has been moved */
	unsigned long long _received;		/* how much has arrived, for the progress door */
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
