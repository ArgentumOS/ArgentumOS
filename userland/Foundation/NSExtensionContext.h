/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * NSExtensionContext — THE HOST'S REQUEST, AS THE EXTENSION SEES IT (§62.92), with `NSExtensionRequestHandling` and
 * `NSExtensionItemsAndErrorsKey`. This closes `App Support / Extension Support`.
 *
 * **THE EXTENSION'S HALF IS REAL; THE HOST'S HALF IS NOT HERE, AND THE SHAPE IS THE ONE §62.80 ESTABLISHED.** The
 * context holds the items the host sent, and an extension ENDS the request one of two ways — complete it with
 * result items, or cancel it with an error. All of that is implemented and enforced. What this system has no
 * HOST for is the other end of the conversation: nothing here starts an extension, hands it a context, or
 * receives its ending. So the host's two halves live in the internal seam `FNExtensionContext.h` — the device
 * §62.85 introduced — which makes the whole flow exercisable today without a host process:
 *   * `FNExtensionContextMakeWithInputItems()` plays the host that CREATES the context (which is why there is no
 *     public initialiser: Apple is explicit that a context comes from the host);
 *   * `FNExtensionContextBeginRequestWithHandler()` plays the host that HANDS IT OVER, which is the one door of
 *     `NSExtensionRequestHandling`;
 *   * and the ending is readable there (`-HasEnded`, `-EndingItems`, `-EndingError`) because a host is what
 *     receives it.
 *
 * THREE THINGS ARE ENFORCED RATHER THAN DESCRIBED, and each has a ground rather than a taste behind it:
 *   1. **A REQUEST HAS ONE ENDING.** Completing or cancelling a context that has already ended RAISES. Apple does
 *      not document the second call, and the reason to refuse it is the same one §62.91 used for a distant
 *      request's second reply: the host has already been told, and a second ending goes to nobody.
 *   2. **A CANCELLATION MUST SAY WHY** — a nil error RAISES. Apple's own documentation for the door is about the
 *      error it carries, and a cancellation nobody can explain is not a report.
 *   3. **THE COMPLETION HANDLER RUNS BEFORE THE DOOR RETURNS**, with `expired` = NO. Apple performs that block as
 *      a background-priority task; with no host and no queue here, the honest local equivalent is to run it now,
 *      and a caller that assumed asynchrony is not broken by that while a caller that REQUIRED it should not
 *      assume it.
 *
 * `NSExtensionItemsAndErrorsKey` IS EXPORTED HERE (Apple lists it beside this class): the key an error's userInfo
 * uses to carry the items AND the per-item errors of a request that ended badly.
 */

#import <Foundation/NSObject.h>
#import <Foundation/NSURL.h>	/* the parameter of -openURL:completionHandler: below */

NS_ASSUME_NONNULL_BEGIN

@class NSArray;
@class NSError;
@class NSExtensionContext;

/* THE EXTENSION'S LIFECYCLE HOOK: an extension's principal object implements it, and the host calls its one door
 * with the context. REQUIRED, not optional — it is the protocol's entire purpose. */
@protocol NSExtensionRequestHandling <NSObject>
- (void)beginRequestWithExtensionContext:(NSExtensionContext *)context;
@end

@interface NSExtensionContext : NSObject
{
@private
	id _inputItems;		/* copied: what the host sent */
	id _endingItems;	/* copied, once completed */
	id _endingError;	/* retained, once cancelled */
	BOOL _ended;
}

/* WHAT THE HOST SENT. Never nil: an empty array is "the host sent nothing", which is a fact worth representing. */
- (NSArray *)inputItems;

/* END IT, ONE WAY OR THE OTHER. See the file's note for the three enforced rules; the objects are `NSExtensionItem`
 * instances in Apple's system, and they are copied here because a caller's later mutation is not the host's
 * business. */
- (void)completeRequestReturningItems:(nullable NSArray *)items
		    completionHandler:(nullable void (^)(BOOL expired))completionHandler;
- (void)cancelRequestWithError:(NSError *)error;

/* ⚠⚠ `-openURL:completionHandler:`, READ FROM THE CORPUS BEFORE IT WAS WRITTEN (§63.96's instrument, and this time
 * the SEARCH ITSELF had to be got right: the door is `- (void)openURL:(NSURL *)URL completionHandler:…` at line 25 of
 * the corpus's own `NSExtensionContext.h` — **A SELECTOR IS NOT A WORD. `openURL:completionHandler:` IS HOW A HUMAN
 * WRITES IT; A DECLARATION PUTS PARAMETER TYPES BETWEEN THE PARTS, SO SEARCHING FOR THE FIRST FORM FINDS NOTHING.**)
 *
 *   APPLE'S COMMENT, VERBATIM: "Asks the host to open a URL on the extension's behalf"
 *   AND ITS SIGNATURE CARRIES THE ANSWER: `completionHandler:(void (^)(BOOL success))` — AN ASK WHOSE REPLY IS
 *   WHETHER IT WORKED.
 *
 * ⚠ SO THE DOOR'S OWN PARAMETER SAYS WHAT TO DO HERE: there is no extension host in this system, so the request cannot
 * be honoured, **AND THE TRUTHFUL REPLY IS THE ONE THE COMPLETION TAKES: `NO`.** That is the same shape as the
 * security-scoped pair (§63.108) and the inflection door (§63.99): **THE DOOR ANSWERS A QUESTION ABOUT A CAPABILITY, AND
 * THE HONEST ANSWER IS THE ONE THE PARAMETER EXISTS FOR. A REFUSAL WOULD BE WRONG HERE — THE CALLER ASKED TO BE TOLD,
 * AND BEING TOLD IS THE WHOLE CONTRACT.** */
- (void)openURL:(NSURL *)URL completionHandler:(void (^)(BOOL success))completionHandler;
@end

/* THE KEY BESIDE THE CLASS: an `NSError`'s userInfo uses it to carry the items AND the per-item errors of a
 * request that ended badly. */
FOUNDATION_EXPORT NSString * const NSExtensionItemsAndErrorsKey;

NS_ASSUME_NONNULL_END
