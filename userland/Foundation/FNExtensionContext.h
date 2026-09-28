/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * FNExtensionContext.h — THE HOST'S HALF OF A REQUEST (§62.92). INTERNAL.
 *
 * WHY IT EXISTS: `NSExtensionContext` is the host's request AS THE EXTENSION SEES IT, and this system has no host
 * — nothing starts an extension or receives its ending. Without these functions the class would be constructible
 * only from inside itself and its ending would be unobservable: the state §62.85 found for the spell server's
 * client half and §62.91 for a distant request's reply. Here they let the WHOLE flow run in one process: the host
 * makes a context, hands it to a principal object, and reads what came back.
 *
 * NOTHING IS FABRICATED: the items are the caller's, the handler is the caller's, and the ending is whatever the
 * extension actually did — these functions carry, they do not decide.
 */

#import <Foundation/NSObjCRuntime.h>

NS_ASSUME_NONNULL_BEGIN

@class NSArray;
@class NSError;
@class NSExtensionContext;

@protocol NSExtensionRequestHandling;

/* THE HOST CREATES THE CONTEXT. That is why the class has no public initialiser: Apple is explicit that a context
 * comes from the host, and this function is the host, in miniature. */
extern NSExtensionContext *FNExtensionContextMakeWithInputItems(NSArray *_Nullable inputItems);

/* THE HOST HANDS IT OVER — the one door of `NSExtensionRequestHandling`. */
extern void FNExtensionContextBeginRequestWithHandler(NSExtensionContext *context,
						      id <NSExtensionRequestHandling> handler);

/* WHAT THE EXTENSION DID. A host is what receives the ending, so reading it lives here rather than on the class. */
extern BOOL FNExtensionContextHasEnded(NSExtensionContext *context);
extern NSArray *_Nullable FNExtensionContextEndingItems(NSExtensionContext *context);
extern NSError *_Nullable FNExtensionContextEndingError(NSExtensionContext *context);

NS_ASSUME_NONNULL_END
