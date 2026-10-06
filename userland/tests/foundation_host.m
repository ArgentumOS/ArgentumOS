/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_host — §62.63's acceptance: NSHost, the host as a name-service answer.
 *
 * ONE unit, importing only <Foundation/Foundation.h>.
 *
 * WHAT IS BEING ASSERTED IS THAT THE CLASS IS A VIEW OVER THE SYSTEM'S RESOLVER AND NOTHING MORE, which is what
 * Apple's own sentence makes it: the doors "do NOT CONTACT THE HOST ITSELF". So every check here is a question
 * the LOCAL name service can answer — "localhost" and the loopback addresses are in it by definition, and a name
 * reserved never to resolve (`.invalid`, RFC 2606) is not — and NOTHING here needs a network, which is what
 * makes the probe run in a guest with no NIC.
 *
 * AND THE TWO HALVES OF A HOST ARE CHECKED SEPARATELY, because Apple publishes that a host may have several of
 * each: the plural doors must answer the same thing the singular ones came from, and a host built from an
 * ADDRESS must be equal to one built from a NAME for the same host — which is the identity rule the class exists
 * to express.
 */

#import <Foundation/Foundation.h>

#include <stdio.h>

static int okc, failc;

static int lastcheck;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	lastcheck = ok;	/* read by covers() */
	if (ok) {
		okc++;
		printf("FOUNDATION-HOST %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-HOST %s FAIL %s\n", name, detail != nil ? [detail UTF8String] : "");
	}
}

/* covers("NSBundle", "resourcePath") - the behavioural claim, piggybacked on the check above it. */
static void covers_(const char *cls, const char *sel)
{
	if (lastcheck) {
		printf("COVERS %s %s\n", cls, sel);
	}
}
#define covers(cls, sel) covers_(cls, sel)

/* IS ONE OF THESE A LOOPBACK ADDRESS? 127.0.0.0/8 is, and so is ::1 — and this is written out rather than
 * compared against a constant because the SYSTEM decides which textual form it answers with. */
static BOOL fn_has_loopback(NSArray *addresses)
{
	NSUInteger i;

	for (i = 0; i < [addresses count]; i++) {
		NSString *address = [addresses objectAtIndex:i];

		if ([address hasPrefix:@"127."] || [address isEqualToString:@"::1"]) {
			return YES;
		}
	}
	return NO;
}

/* A NIL THE COMPILER CANNOT SEE THROUGH, for the check that a nil NAME has no host: passing a literal nil to a
 * nonnull parameter is `-Wnonnull`, and passing one through a call is the same experiment without the warning
 * (the printer-not-the-rule rule from the attributed-string probe). */
static NSString *fn_nothing(void)
{
	return nil;
}

int main(void)
{
	{
		/* THE HOST THE PROCESS IS RUNNING ON: a name from the kernel, resolved through the same service. */
		NSHost *host = [NSHost currentHost];
		NSString *name = host != nil ? [host name] : nil;

		check("the-current-host-answers-a-name",
		      host != nil && name != nil && [name length] > 0 &&
		      [[host names] containsObject:name],
		      [NSString stringWithFormat:@"name=%@ names=%@ addresses=%@",
			host != nil ? [host name] : @"(no host)", host != nil ? [host names] : @"-",
			host != nil ? [host addresses] : @"-"]);
	}

	{
		/* LOCALHOST IS IN EVERY NAME SERVICE, and both halves have to come back: at least one loopback
		 * address and at least one name.
		 *
		 * THE NULLABLE IS BOUND TO A LOCAL AND GUARDED IN THE CONJUNCTION, which is the shape this tree's own
		 * url probe records: `-address` is NULLABLE (a host may have no addresses), and the GUEST compiles
		 * with `-Werror=nullable-to-nonnull-conversion`, so passing it inline to a nonnull parameter fails
		 * there while the host build is happy. */
		NSHost *host = [NSHost hostWithName:@"localhost"];
		NSString *address = host != nil ? [host address] : nil;

		check("a-name-resolves-to-its-addresses-and-a-name",
		      host != nil && address != nil && [[host addresses] count] > 0 &&
		      fn_has_loopback([host addresses]) && [[host names] count] > 0 &&
		      [[host addresses] containsObject:address],
		      [NSString stringWithFormat:@"addresses=%@ names=%@",
			host != nil ? [host addresses] : @"(no host)", host != nil ? [host names] : @"-"]);
	covers("NSHost", "hostWithName:");
	}

	{
		/* THE ADDRESS DOOR, and the equality rule the class exists for: a host built from an ADDRESS and one
		 * built from a NAME must be the SAME host when they share an address — which is checked by rebuilding
		 * from the first host's own address, so the check does not depend on which loopback form the service
		 * answers with. */
		NSHost *byAddress = [NSHost hostWithAddress:@"127.0.0.1"];
		NSHost *byName = [NSHost hostWithName:@"localhost"];
		NSHost *rebuilt = byName != nil && [[byName addresses] count] > 0
			? [NSHost hostWithAddress:[[byName addresses] objectAtIndex:0]] : nil;

		check("an-address-resolves-to-its-names",
		      byAddress != nil && [[byAddress names] count] > 0 &&
		      [[byAddress addresses] containsObject:@"127.0.0.1"],
		      [NSString stringWithFormat:@"addresses=%@ names=%@",
			byAddress != nil ? [byAddress addresses] : @"(no host)",
			byAddress != nil ? [byAddress names] : @"-"]);
	covers("NSHost", "hostWithAddress:");
		check("a-host-is-equal-to-the-one-rebuilt-from-its-own-address",
		      byName != nil && rebuilt != nil && [byName isEqualToHost:rebuilt],
		      [NSString stringWithFormat:@"byName=%@ rebuilt=%@",
			byName != nil ? [byName addresses] : @"(no host)",
			rebuilt != nil ? [rebuilt addresses] : @"(no host)"]);
	}

	{
		/* TWO DIFFERENT ADDRESSES ARE TWO DIFFERENT HOSTS — the other half of the identity rule, and the half
		 * that a class answering YES to everything would fail. */
		NSHost *one = [NSHost hostWithAddress:@"127.0.0.1"];
		NSHost *two = [NSHost hostWithAddress:@"127.0.0.2"];

		check("two-addresses-are-not-one-host",
		      one != nil && two != nil && ![one isEqualToHost:two],
		      [NSString stringWithFormat:@"one=%@ two=%@",
			one != nil ? [one addresses] : @"(no host)",
			two != nil ? [two addresses] : @"(no host)"]);
	covers("NSHost", "isEqualToHost:");
	}

	{
		/* A NAME THE SERVICE DOES NOT KNOW HAS NO HOST. `.invalid` is reserved by RFC 2606 precisely so that it
		 * CANNOT resolve, which makes this the one such name that is safe to ask about. */
		NSHost *host = [NSHost hostWithName:@"this-host-does-not-exist.invalid"];

		check("a-name-the-service-does-not-know-has-no-host",
		      host == nil,
		      [NSString stringWithFormat:@"host=%@ (want nil)", host]);
	}

	{
		/* AND THE CLASS-SIDE DOORS TAKE NOTHING FOR GRANTED: a nil name or address has no host either. */
		check("a-nil-name-or-address-has-no-host",
		      [NSHost hostWithName:fn_nothing()] == nil &&
		      [NSHost hostWithAddress:fn_nothing()] == nil,
		      @"nil in, nil out");
	}

	{
		/* THE CACHE DOORS ANSWER AS APPLE DOCUMENTS THEM: "Caching no longer supported" is the class's own
		 * annotation on all three, so there is no cache to enable and nothing to flush. */
		[NSHost setHostCacheEnabled:YES];
		check("the-cache-doors-answer-as-apple-documents",
		      ![NSHost isHostCacheEnabled],
		      [NSString stringWithFormat:@"isHostCacheEnabled=%d after setHostCacheEnabled:YES",
			(int)[NSHost isHostCacheEnabled]]);
	covers("NSHost", "setHostCacheEnabled:");
		[NSHost flushHostCache];
	}

	printf("FOUNDATION-HOST RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-HOST DONE\n");
	return failc == 0 ? 0 : 1;
}
