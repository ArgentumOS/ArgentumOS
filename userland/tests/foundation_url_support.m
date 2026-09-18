/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_url, unit 1 of 2 — the support unit (MRR).
 *
 * It imports ONLY <foundation/Foundation.h>, which is what proves the umbrella is
 * complete: if NSURL were missing from it, this unit would not compile.
 */

#import "foundation_url.h"

NSURL *foundation_url_http(void)
{
	return [NSURL URLWithString:@"https://example.com:8443/a/b?x=1#top"];
}

NSURL *foundation_url_file(void)
{
	return [NSURL fileURLWithPath:@"/System/Temporary Files/probe.txt"];
}
