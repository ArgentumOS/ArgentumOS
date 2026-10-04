/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * FNCURLURLProtocol — THE BRIDGE: libcurl behind the NSURLProtocol seam.
 * docs/design/foundation-plan.md W7; docs/design/foundation-transport-plan.md §4, slice 2c.
 *
 * THE FIRST IMPLEMENTATION OF THE SEAM SLICE 2a SHIPPED. Nothing about it is special to it: it is an
 * ordinary NSURLProtocol subclass whose transfer happens to be driven by curl, which is the whole point
 * of the ordering the plan chose — the plug-in point came first because every transport is a plug-in.
 *
 * THE SURFACE IS MEASURED, NOT INVENTED. `curl` on this system answers
 * `Protocols: file http https` (tools/curl-build.sh asserts it), and this class claims exactly those
 * three schemes: a request for `ftp://` is NOT claimed, which is what lets the seam answer "no protocol
 * handles this" instead of a protocol failing halfway through.
 *
 * THE THREE DECISIONS WORTH STATING:
 *
 *   1. CURL OWNS THE TRANSFER AND THE BRIDGE REPORTS IT. The callbacks a caller sees are the seam's own
 *      and nothing else — didReceiveResponse:cacheStoragePolicy: (once, when the header block ends),
 *      didLoadData: (once per chunk curl hands over, so a large body is streamed rather than accumulated)
 *      and EXACTLY ONE of URLProtocolDidFinishLoading: / didFailWithError:.
 *
 *   2. A REDIRECT IS REPORTED, NOT SILENTLY FOLLOWED (CURLOPT_FOLLOWLOCATION=0). The seam has a callback
 *      for precisely this — URLProtocol:wasRedirectedToRequest:redirectResponse: — and WHETHER to follow
 *      is the caller's decision, not the transport's. So a 3xx with a Location is reported there and the
 *      transfer STOPS at that point rather than being chased. (A session built on this seam will follow
 *      by default, which is where that policy belongs.)
 *
 *   3. THE TRANSFER RUNS ON ITS OWN THREAD. `curl_easy_perform` blocks, and the seam's contract is that
 *      -startLoading RETURNS and the callbacks arrive later. So -startLoading spawns a thread and the
 *      callbacks are delivered from it — Apple documents that a protocol's client callbacks may arrive on
 *      any thread, and this header says so rather than leaving a caller to discover it.
 *
 * REFUSED BY NAME: `-stopLoading` STOPS AT THE NEXT CHUNK rather than interrupting a blocked read. It is
 * the honest v1 (a cross-thread curl_easy call would be undefined behaviour, and there is no thread it
 * could safely be made from), it is enough to abandon a large download, and the limitation is stated here
 * rather than implied.
 */

#ifndef FOUNDATION_FNCURLURLPROTOCOL_H
#define FOUNDATION_FNCURLURLPROTOCOL_H

#import <Foundation/NSURLProtocol.h>

NS_ASSUME_NONNULL_BEGIN

@interface FNCURLURLProtocol : NSURLProtocol
{
	/* THE RUNNING TRANSFER, owned by the loading thread for as long as it runs: -stopLoading has to be
	 * able to reach a transfer it did not create, so the pointer lives here and the struct itself is
	 * the thread's. It is `void *` because the struct is this library's own business. */
	void *_transfer;
	/* SET BY -stopLoading EVEN WHEN THERE IS NO TRANSFER, because a CACHE HIT is delivered without one and
	 * still has to be cancellable: the response goes through the disposition door, a delegate may cancel it,
	 * and without this flag the hit carried on and delivered the body anyway (§49.3). */
	unsigned int _hitStopped:1;
}

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_FNCURLURLPROTOCOL_H */
