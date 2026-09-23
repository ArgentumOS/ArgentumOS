/*
 * NSURLSessionWebSocketTask.h
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE CLOSE CODES, AND ONLY THE CLOSE CODES SO FAR. Apple publishes NSURLSessionWebSocketCloseCode on
 * NSURLSessionWebSocketTask's page, so this header is its home - but the TASK ITSELF LANDS IN SLICE 3 OF §59,
 * and nothing here declares it: a header that declared the class before its doors exist would make the family
 * ledger read "shipped" for a class that is not, which is the one thing that ledger is for.
 *
 * WHERE THE NUMBERS COME FROM, AND IT IS NOT APPLE. RFC 6455 §7.4.1 DEFINES 1000-1015, and these names are
 * Apple's spellings of exactly those codes, so the values below are THE RFC'S - a standard, not a vendor's
 * choice. The one exception is `...Invalid`, which is NOT in the RFC: Apple documents the name and nothing
 * else, so its value is OURS under D2 (§11.6) - and it is 0 for the reason every container needs: it is what a
 * reader that has no close code at all starts from, and what an unknown one becomes. That matches the only
 * published account of the behaviour rather than being guessed from it.
 *
 * THREE OF THESE ARE RESERVED BY THE RFC AND MUST NEVER BE SENT IN A CLOSE FRAME (1005 noStatusReceived,
 * 1006 abnormalClosure, 1015 tlsHandshakeFailure): they EXIST for a reader to report what happened to it, not
 * for a writer to say. The framing layer enforces that (slice 2); the constants are here because a caller
 * reads them.
 */
#ifndef _FOUNDATION_NSURLSESSIONWEBSOCKETTASK_H
#define _FOUNDATION_NSURLSESSIONWEBSOCKETTASK_H

#import <Foundation/NSObject.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, NSURLSessionWebSocketCloseCode) {
	NSURLSessionWebSocketCloseCodeInvalid = 0,		/* ours (D2): no close code, or one we do not know */
	NSURLSessionWebSocketCloseCodeNormalClosure = 1000,	/* the RFC's */
	NSURLSessionWebSocketCloseCodeGoingAway = 1001,
	NSURLSessionWebSocketCloseCodeProtocolError = 1002,
	NSURLSessionWebSocketCloseCodeUnsupportedData = 1003,
	NSURLSessionWebSocketCloseCodeNoStatusReceived = 1005,	/* reserved: reportable, never sent */
	NSURLSessionWebSocketCloseCodeAbnormalClosure = 1006,	/* reserved: reportable, never sent */
	NSURLSessionWebSocketCloseCodeInvalidFramePayloadData = 1007,
	NSURLSessionWebSocketCloseCodePolicyViolation = 1008,
	NSURLSessionWebSocketCloseCodeMessageTooBig = 1009,
	NSURLSessionWebSocketCloseCodeMandatoryExtensionMissing = 1010,
	NSURLSessionWebSocketCloseCodeInternalServerError = 1011,
	NSURLSessionWebSocketCloseCodeTLSHandshakeFailure = 1015	/* reserved: reportable, never sent */
};

NS_ASSUME_NONNULL_END

#endif /* _FOUNDATION_NSURLSESSIONWEBSOCKETTASK_H */
