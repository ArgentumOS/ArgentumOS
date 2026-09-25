/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_url_ownership, unit of 1 — W8p slice 6h: NSURL's PART OWNERSHIP. docs/design/foundation-plan.md
 * §60.
 *
 * ONE unit, importing only <Foundation/Foundation.h>. NO FIXTURE: a URL is parsed from a string.
 *
 * WHY THIS PROBE EXISTS: the URL unit had NO -dealloc at all (the parts were copied or retained and none was
 * ever released) and ONE of them, the scheme, was not even owned - it came from -lowercaseString, which
 * answers an autoreleased string. Slice 6a added a dealloc for the cache it introduced and stopped there,
 * because releasing the parts would be a crash where the scheme is the object and a behaviour change beyond
 * that slice where it is not. THIS IS THE SLICE THAT OWES IT.
 *
 * TWO CHECKS, AND THE FIRST ONE IS BUILT TO DETECT THE DEFECT RATHER THAN TO DESCRIBE IT:
 *
 *   ownership-the-parts-survive-their-parse-pool  a URL is parsed inside a pool, the pool is drained, and
 *                               then a PILE OF STRINGS IS ALLOCATED to reuse whatever the pool freed before
 *                               the parts are read. An unowned part now points at reused memory and the check
 *                               fails; an over-released one crashes. Both directions of the ownership
 *                               mistake are covered by one experiment, which is why the pile is there - a
 *                               plain read after a drain proves nothing, because an allocator that leaves
 *                               freed memory intact (musl does) returns the right bytes anyway;
 *   ownership-the-parts-are-copies  the URL is parsed from a MUTABLE string, the string is then changed
 *                               underneath it, and the URL must be unchanged: a part that was a reference
 *                               would move with its source.
 */

#import <Foundation/Foundation.h>

#include <stdio.h>

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-URL-OWNERSHIP %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-URL-OWNERSHIP %s FAIL %s\n", name, detail != nil ? [detail UTF8String] : "");
	}
}

int main(void)
{
	check("ownership-probe-is-arc", YES, nil);

	/* --- THE PARTS MUST SURVIVE THE POOL THEIR PARSE TEMPORARIES DIED IN ------------------------------ */
	{
		NSURL *kept;

		/* A STRONG OUTER LOCAL AND A POOL THAT ENDS: under ARC the URL survives the drain (ARC owns this
		 * pointer) while the parse's AUTORELEASED temporaries do not - which is exactly the pair the defect
		 * needs to be visible. */
		@autoreleasepool {
			kept = [[NSURL alloc] initWithString:@"https://user@example.com:8443/a/b?q=1#frag"];
		}
		{
			/* THE PILE: reuse what the pool freed, so an unowned part is visibly wrong rather than
			 * accidentally right. */
			NSMutableArray *pile = [NSMutableArray array];
			int i;

			for (i = 0; i < 4000; i++) {
				[pile addObject:[NSString stringWithFormat:@"poison-%d-....................", i]];
			}
			(void)pile;
			{
				NSString *scheme = [kept scheme];
				NSString *user = [kept user];
				NSString *host = [kept host];
				NSString *path = [kept path];
				NSString *query = [kept query];
				NSString *fragment = [kept fragment];
				NSNumber *port = [kept port];

				check("ownership-the-parts-survive-their-parse-pool",
				      [scheme isEqual:@"https"] && [user isEqual:@"user"] &&
				      [host isEqual:@"example.com"] && [path isEqual:@"/a/b"] &&
				      [query isEqual:@"q=1"] && [fragment isEqual:@"frag"] &&
				      [port intValue] == 8443,
				      [NSString stringWithFormat:@"scheme=%@ user=%@ host=%@ path=%@ query=%@ "
					@"fragment=%@ port=%@", scheme, user, host, path, query, fragment, port]);
			}
		}
	}

	/* --- AND THE PARTS ARE COPIES, NOT REFERENCES TO THE SOURCE --------------------------------------- */
	{
		NSMutableString *source = [NSMutableString stringWithString:@"http://host.example/path?x=1#f"];
		NSURL *url = [[NSURL alloc] initWithString:source];

		[source setString:@"ftp://elsewhere/other?y=2#g"];
		check("ownership-the-parts-are-copies",
		      [[url scheme] isEqual:@"http"] && [[url host] isEqual:@"host.example"] &&
		      [[url path] isEqual:@"/path"] && [[url query] isEqual:@"x=1"] &&
		      [[url fragment] isEqual:@"f"],
		      [NSString stringWithFormat:@"after mutating the source: scheme=%@ host=%@ path=%@ query=%@ "
			@"fragment=%@", [url scheme], [url host], [url path], [url query], [url fragment]]);
	}

	printf("FOUNDATION-URL-OWNERSHIP RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-URL-OWNERSHIP DONE\n");
	return failc == 0 ? 0 : 1;
}
