/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_url, unit 2 of 2 — the checks (ARC). docs/design/foundation-plan.md, F8.
 *
 *   url-parse          the RFC 3986 parts, and the spelling round trip
 *   url-refusals       THE PARSE IS THE REFUSAL: what is not a URL answers nil
 *   url-file           the FSH rule: file:/// + an encoded path, and -path back
 *   url-file-refusals  a relative or empty path is not a path this system names
 *   url-append-path    the path arithmetic, adding
 *   url-delete-path    the path arithmetic, removing (with Cocoa's dot rules)
 *   url-equality       by SPELLING, case-sensitively, as the header states
 *   url-identity       -absoluteURL, -relativeString, -description
 *   cross-tu           a URL built in the other unit behaves locally
 */

#import "foundation_url.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#import <objc/runtime.h>

static int okc, failc;

static void check(const char *name, int ok, const char *detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-URL %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-URL %s FAIL %s\n", name, detail ? detail : "");
	}
}

/* THE DETAIL CARRIES THE MEASUREMENT (the plan's §9 lesson, applied from the
 * start this time): a failure says what the parts actually were. */
static const char *fn_why(NSURL *url)
{
	static char why[200];

	snprintf(why, sizeof why, "absoluteString=%s path=%s host=%s scheme=%s",
		 url == nil ? "(nil)" : [[url absoluteString] UTF8String],
		 url == nil ? "-" : [[url path] UTF8String],
		 (url == nil || [url host] == nil) ? "(nil)" : [[url host] UTF8String],
		 url == nil ? "-" : [[url scheme] UTF8String]);
	return why;
}


/* §63.110: the probe's own client, because the door's contract is DEFINED BY WHAT THE CLIENT RECEIVES. It implements
 * only the failure message, which is also the point: a client is NOT required to implement any of the three. */
@interface FNURLProbeClient : NSObject
{
	@public
	BOOL notified;
	BOOL finishedInstead;
}
@end

@implementation FNURLProbeClient

- (void)URL:(NSURL *)sender resourceDidFailLoadingWithReason:(NSString *)reason
{
	notified = YES;
}

- (void)URLResourceDidFinishLoading:(NSURL *)sender
{
	finishedInstead = YES;
}

@end

int main(void)
{

	{
		/* §63.110: THE LAST OF THE RESOURCE-DATA TRIO, OBSERVED THROUGH ITS OWN CONTRACT — a client that receives the
		 * FAILURE notification for an unreachable URL, and does NOT receive a finish. ⚠ NO FILESYSTEM CALL: the URL is
		 * unreachable on purpose, so the answer comes from the door and not from a fixture. */
		FNURLProbeClient *client = [[FNURLProbeClient alloc] init];
		NSURL *h = [NSURL URLWithString:@"https://example.invalid/nothing"];

		[h loadResourceDataNotifyingClient:client usingCache:NO];
		check("url-load-resource-data-notifies-the-client-of-failure",
		      client->notified == YES && client->finishedInstead == NO,
		      [[NSString stringWithFormat:@"notified=%d finished=%d", (int)client->notified,
			(int)client->finishedInstead] UTF8String]);
	}


	{
		/* §63.109: TWO OF THE DEPRECATED RESOURCE-DATA TRIO. ⚠ NEITHER CHECK TOUCHES THE FILESYSTEM — both work on a
		 * NON-file URL, where each door's answer is determined by its own contract rather than by a fixture: a load of
		 * something that is not a resource answers nil, and a WRITE to a URL that cannot hold data answers NO. */
		NSURL *h = [NSURL URLWithString:@"https://example.invalid/nothing"];
		BOOL wrote = [h setResourceData:[[NSData alloc] init]];
		NSData *loaded = [h resourceDataUsingCache:NO];

		check("url-set-resource-data-answers-no-for-a-url-that-cannot-hold-data",
		      wrote == NO,
		      [[NSString stringWithFormat:@"wrote=%d", (int)wrote] UTF8String]);
		check("url-resource-data-using-cache-answers-nil-for-an-unreachable-url",
		      loaded == nil,
		      [[NSString stringWithFormat:@"loaded=%@", loaded] UTF8String]);
	}


	{
		/* §63.108: THE SECURITY-SCOPED PAIR, ASSERTED BY ITS CONSEQUENCE RATHER THAN BY ITS RETURN ALONE. The NO is
		 * the claim "no access was granted"; the second check asserts THAT THE ANSWER DOES NOT ACCUMULATE, because a
		 * door counting references it cannot honour would make the paired stop a lie. No filesystem call is made, so
		 * the check cannot fail for a reason that is not this pair's. */
		NSURL *s = [NSURL fileURLWithPath:@"/System"];
		BOOL granted = [s startAccessingSecurityScopedResource];
		BOOL again;

		[s stopAccessingSecurityScopedResource];
		again = [s startAccessingSecurityScopedResource];
		check("url-security-scoped-pair-answers-no",
		      granted == NO && again == NO && [[s path] isEqualToString:@"/System"],
		      [[NSString stringWithFormat:@"granted=%d again=%d path=%@", (int)granted, (int)again, [s path]] UTF8String]);
		check("url-security-scoped-stop-balances-nothing",
		      [s isFileURL],
		      [[NSString stringWithFormat:@"isFileURL=%d after an unbalanced stop", (int)[s isFileURL]] UTF8String]);
	}


	{
		/* §63.107: THE PROMISED-ITEM TRIO. On a system with no ubiquity a promised item is an ordinary file, so
		 * what is asserted is THAT THEY AGREE WITH THE ORDINARY DOORS — and, for the one rule Apple publishes, that
		 * a content-tied key ANSWERS YES WITH A NIL VALUE rather than failing. ⚠ The detail arguments are `const char *`
		 * in THIS probe. */
		NSURL *p = [NSURL fileURLWithPath:@"/System"];
		id ordinary = nil;
		id promised = nil;
		NSError *e1 = nil;
		NSError *e2 = nil;
		BOOL gotOrdinary = [p getResourceValue:&ordinary forKey:NSURLIsDirectoryKey error:&e1];
		BOOL gotPromised = [p getPromisedItemResourceValue:&promised forKey:NSURLIsDirectoryKey error:&e2];
		id tied = @"not-nil";
		NSError *e3 = nil;
		BOOL tiedYes = [p getPromisedItemResourceValue:&tied forKey:@"NSURLContentAccessDateKey" error:&e3];
		BOOL reachable = [p checkPromisedItemIsReachableAndReturnError:NULL];

		check("url-promised-item-value-agrees-with-the-ordinary-door",
		      gotOrdinary == gotPromised && gotOrdinary,
		      [[NSString stringWithFormat:@"ordinary=%d promised=%d", (int)gotOrdinary, (int)gotPromised] UTF8String]);
		check("url-promised-item-content-tied-key-answers-yes-with-nil",
		      tiedYes == YES && tied == nil && e3 == nil,
		      [[NSString stringWithFormat:@"yes=%d value=%@", (int)tiedYes, tied] UTF8String]);
		check("url-promised-item-reachability-delegates",
		      reachable == [p checkResourceIsReachableAndReturnError:NULL],
		      [[NSString stringWithFormat:@"promised=%d ordinary=%d", (int)reachable,
			(int)[p checkResourceIsReachableAndReturnError:NULL]] UTF8String]);
		check("url-promised-item-bulk-read-answers-a-dictionary",
		      [[p promisedItemResourceValuesForKeys:[NSArray arrayWithObject:NSURLIsDirectoryKey]
						      error:NULL] isKindOfClass:[NSDictionary class]],
		      "the bulk door answers a dictionary");
	}


	{
		/* §63.106: THE SEVEN BOOKMARK DOORS REFUSE BY NAME (dec-412cc6306e238994), EACH ASSERTED ALONE — one check
		 * over seven doors says only that SOMETHING raised, and a door that stopped raising would hide behind its
		 * siblings. ⚠ The probe is ARC. */
		NSURL *bu = [NSURL fileURLWithPath:@"/"];
		NSData *bd = [[NSData alloc] init];
		BOOL r1 = NO, r2 = NO, r3 = NO, r4 = NO, r5 = NO, r6 = NO, r7 = NO;

		@try { (void)[NSURL URLByResolvingAliasFileAtURL:bu options:0 error:NULL]; } @catch (NSException *e) { r1 = YES; }
		@try { (void)[NSURL URLByResolvingBookmarkData:bd options:0 relativeToURL:nil bookmarkDataIsStale:NULL error:NULL]; } @catch (NSException *e) { r2 = YES; }
		@try { (void)[NSURL bookmarkDataWithContentsOfURL:bu error:NULL]; } @catch (NSException *e) { r3 = YES; }
		@try { (void)[NSURL resourceValuesForKeys:[NSArray array] fromBookmarkData:bd]; } @catch (NSException *e) { r4 = YES; }
		@try { (void)[NSURL writeBookmarkData:bd toURL:bu options:0 error:NULL]; } @catch (NSException *e) { r5 = YES; }
		@try { (void)[bu bookmarkDataWithOptions:0 includingResourceValuesForKeys:nil relativeToURL:nil error:NULL]; } @catch (NSException *e) { r6 = YES; }
		@try { (void)[[NSURL alloc] initByResolvingBookmarkData:bd options:0 relativeToURL:nil bookmarkDataIsStale:NULL error:NULL]; } @catch (NSException *e) { r7 = YES; }

		check("url-bookmark-alias-resolution-refuses", r1, [[NSString stringWithFormat:@"raised=%d", (int)r1] UTF8String]);
		check("url-bookmark-resolution-refuses", r2, [[NSString stringWithFormat:@"raised=%d", (int)r2] UTF8String]);
		check("url-bookmark-data-with-contents-refuses", r3, [[NSString stringWithFormat:@"raised=%d", (int)r3] UTF8String]);
		check("url-bookmark-resource-values-refuses", r4, [[NSString stringWithFormat:@"raised=%d", (int)r4] UTF8String]);
		check("url-bookmark-write-refuses", r5, [[NSString stringWithFormat:@"raised=%d", (int)r5] UTF8String]);
		check("url-bookmark-creation-refuses", r6, [[NSString stringWithFormat:@"raised=%d", (int)r6] UTF8String]);
		check("url-bookmark-init-by-resolving-refuses", r7, [[NSString stringWithFormat:@"raised=%d", (int)r7] UTF8String]);
	}

	{
		NSURL *u = [NSURL URLWithString:@"http://user@example.com:8080/a/b?q=1#frag"];
		NSNumber *port = u != nil ? [u port] : nil;

		check("url-parse",
		      u != nil &&
		      [[u scheme] isEqualToString:@"http"] &&
		      [[u host] isEqualToString:@"example.com"] &&
		      port != nil && [port intValue] == 8080 &&
		      [[u user] isEqualToString:@"user"] &&
		      [[u path] isEqualToString:@"/a/b"] &&
		      [[u query] isEqualToString:@"q=1"] &&
		      [[u fragment] isEqualToString:@"frag"] &&
		      [[u absoluteString] isEqualToString:
			@"http://user@example.com:8080/a/b?q=1#frag"] &&
		      ![u isFileURL],
		      fn_why(u));
	}

	{
		/* A SCHEME IS REQUIRED, and it has to be a scheme: these are the shapes
		 * this class refuses rather than repairing. */
		check("url-refusals",
		      [NSURL URLWithString:@"no-scheme-here"] == nil &&
		      [NSURL URLWithString:@"/absolute/path"] == nil &&
		      [NSURL URLWithString:@"1http://x"] == nil &&
		      [NSURL URLWithString:@"ht tp://x"] == nil &&
		      [NSURL URLWithString:@""] == nil &&
		      [NSURL URLWithString:@":"] == nil,
		      "a string that is not an absolute URL answers nil");

		/*
		 * WHAT IS ABSENT AND WHAT IS SHIPPED, SPLIT FROM THE REFUSALS (2026-09-18).
		 *
		 * THIS CHECK USED TO ASSERT BOTH IN ONE CONJUNCTION, AND TWO OF ITS ABSENCE
		 * CLAIMS HAD GONE STALE: `NSURLComponents` and `+URLWithString:relativeToURL:`
		 * both ship (F13.15), and nothing told the probe — it had been failing ever
		 * since, and the failure could not say WHICH claim was false. That is §11.2's
		 * lesson twice over: an absence assertion is a fact about the TREE, and a
		 * conjunction of them is a fact nobody can localise.
		 *
		 * objc_getClass is the RUNTIME's lookup, which is what a probe should use even
		 * now that NSClassFromString SHIPS (W2a put the C accessors in): asking the
		 * runtime directly is the claim about the RUNTIME, and it does not depend on a
		 * Foundation function being right.
		 */
		/*
		 * WHAT IS STILL ABSENT, AND WHAT ARRIVED — because the FIRST version of this check was a
		 * conjunction that W7 made half-false, and an absence assertion has to be kept as carefully as
		 * any other claim. It asserted that `NSURLRequest` too was NULL, and W7 slice 1
		 * (docs/design/foundation-plan.md §46) is exactly what shipped it. THE LESSON IS THE ONE THIS
		 * FILE'S COMMENT ABOVE ALREADY NAMED: an absence assertion is a fact about the TREE, and a
		 * landing that changes the tree has to revisit it. So the halves are separate now — and it has
		 * HAPPENED A SECOND TIME: `NSURLProtocol` was in this conjunction too, and W7 slice 2a shipped
		 * it. What remains absent is the thing that PERFORMS an exchange on its own, which is the
		 * distinction the request/response values already rest on.
		 */
		/* \u26a0\u26a0 AND THIS CHECK IS GONE, BECAUSE ITS SUBJECT LANDED \u2014 THE FOURTH TIME AN ABSENCE ASSERTION HERE HAS
		 * BEEN MADE HALF-FALSE, and the comment below it had already counted three (NSURLRequest, NSURLProtocol, then
		 * the session). **AN ABSENCE ASSERTION IS A FACT ABOUT THE TREE, SO A LANDING MAKES IT FALSE, AND A CHECK THAT
		 * HAS TO BE MOVED EVERY TIME SOMETHING LANDS IS A CHECK WITH A LIFETIME.**
		 * \u26a0 AND IT IS DELETED RATHER THAN RE-TARGETED ON PURPOSE: replacing it with another door I believe to be
		 * absent would be exactly this session's recurring mistake \u2014 WRITING FROM A REMEMBERED SHAPE INSTEAD OF A
		 * MEASURED ONE \u2014 and the absent list it was standing in for is the ledger's job, not a probe's. */

		/* ⚠⚠ AND THE SESSION'S ARRIVAL CHECK IS GONE (§63.159) — THE FIFTH TIME THIS FILE'S OWN RULE HAS
		 * APPLIED TO ITSELF: "an absence assertion is a fact about the TREE, and a landing that changes the
		 * tree revisits it". The session family ARRIVED (`url-session-shipped`, W7 slice 2c) and has now LEFT
		 * with the 10.2 surface cut, so the assertion is not subtracted from — IT IS REMOVED, with its
		 * subject. What stays is the half that is still true and still the point: the DEPRECATED family ships. */
		check("urlconnection-shipped",
		      objc_getClass("NSURLConnection") != NULL &&
		      [(id)objc_getClass("NSURLConnection") respondsToSelector:sel_registerName(
			  "sendSynchronousRequest:returningResponse:error:")],
		      "NSURLConnection ships (§62.25): the first row of §62.24's work list to land, and since "
		      "§63.153 it drives NSURLProtocol rather than a session");

		/* AND THE SEAM ARRIVED, WHICH IS SLICE 2a: an NSURLProtocol subclass is how ANY protocol plugs
		 * in, so the plug-in point comes before the transport that will be its first implementation.
		 *
		 * THE CLIENT PROTOCOL IS NOT ASSERTED HERE, AND THAT IS A MEASUREMENT RATHER THAN AN OMISSION.
		 * objc_getProtocol("NSURLProtocolClient") answers NULL IN THIS BINARY — measured, with the other
		 * three conjuncts answering 1 — because this probe never ADOPTS the protocol, so its metadata is
		 * never registered for a lookup by name. Asserting it here would therefore be asserting the
		 * LINKER's behaviour rather than the library's: the protocol is asserted where a class actually
		 * conforms to it (foundation_urlprotocol's urlprotocol-client-protocol-shape), which is the only
		 * place the question can be asked honestly. */
		check("urlprotocol-seam-shipped",
		      objc_getClass("NSURLProtocol") != NULL &&
		      objc_getClass("NSCachedURLResponse") != NULL &&
		      [NSURLProtocol respondsToSelector:sel_registerName("registerClass:")],
		      "the transport SEAM ships (W7 slice 2a): NSURLProtocol and the cached-response value - "
		      "the point a transport ATTACHES to, not one that performs a fetch");

		/* AND THE REQUEST/RESPONSE VALUES, WHICH W7 SLICE 1 SHIPPED AND THE CHECK ABOVE USED TO DENY. */
		check("url-request-values-shipped",
		      objc_getClass("NSURLRequest") != NULL &&
		      objc_getClass("NSMutableURLRequest") != NULL &&
		      objc_getClass("NSURLResponse") != NULL &&
		      objc_getClass("NSHTTPURLResponse") != NULL,
		      "the request/response VALUE classes ship (W7 slice 1, §46) - a request DESCRIBES an "
		      "exchange and a response is its answer's metadata; the class that PERFORMS one belongs to "
		      "the loading system above, and that is still absent");

		check("url-shipped",
		      objc_getClass("NSURLComponents") != NULL &&
		      [NSURL respondsToSelector:sel_registerName("URLWithString:relativeToURL:")] &&
		      [[[NSURL URLWithString:@"b"
			      relativeToURL:[NSURL URLWithString:@"http://h/a/"]]
			  absoluteString] isEqualToString:@"http://h/a/b"],
		      "NSURLComponents and relative resolution SHIP (F13.15), demanded rather than merely not-denied");
	}

	{
		NSURL *file = [NSURL fileURLWithPath:@"/System/Temporary Files/probe.txt"];

		check("url-file",
		      file != nil &&
		      [file isFileURL] &&
		      [[file scheme] isEqualToString:@"file"] &&
		      [file host] == nil &&
		      [[file absoluteString] isEqualToString:
			@"file:///System/Temporary%20Files/probe.txt"] &&
		      [[file path] isEqualToString:@"/System/Temporary Files/probe.txt"],
		      fn_why(file));
	}

	{
		check("url-file-refusals",
		      [NSURL fileURLWithPath:@"relative/x"] == nil &&
		      [NSURL fileURLWithPath:@""] == nil,
		      "a relative or empty path is not a path this system names");
	}

	{
		NSURL *base = [NSURL fileURLWithPath:@"/a/b"];
		NSURL *added = base != nil ? [base URLByAppendingPathComponent:@"c d"] : nil;
		NSURL *extended = base != nil ? [base URLByAppendingPathExtension:@"txt"] : nil;

		check("url-append-path",
		      added != nil &&
		      [[added absoluteString] isEqualToString:@"file:///a/b/c%20d"] &&
		      [[added path] isEqualToString:@"/a/b/c d"] &&
		      extended != nil &&
		      [[extended path] isEqualToString:@"/a/b.txt"],
		      fn_why(added));
	}

	{
		NSURL *file = [NSURL fileURLWithPath:@"/a/b/c.txt"];
		NSURL *dir = [NSURL fileURLWithPath:@"/a/b/"];
		NSURL *hidden = [NSURL fileURLWithPath:@"/a/.hidden"];
		NSURL *parent = file != nil ? [file URLByDeletingLastPathComponent] : nil;
		NSURL *stem = file != nil ? [file URLByDeletingPathExtension] : nil;
		NSURL *dirParent = dir != nil ? [dir URLByDeletingLastPathComponent] : nil;
		NSURL *hiddenStem = hidden != nil ? [hidden URLByDeletingPathExtension] : nil;

		check("url-delete-path",
		      parent != nil && [[parent path] isEqualToString:@"/a/b"] &&
		      stem != nil && [[stem path] isEqualToString:@"/a/b/c"] &&
		      /* A trailing slash is not a component: "/a/b/" up is "/a". */
		      dirParent != nil && [[dirParent path] isEqualToString:@"/a"] &&
		      /* A dot-led name has no extension to strip. */
		      hiddenStem != nil && [[hiddenStem path] isEqualToString:@"/a/.hidden"],
		      /* The detail names the clause most likely to give way, which is this
		       * one — reporting `parent` (which passed) made the first run harder to
		       * read than it needed to be. */
		      fn_why(hiddenStem));
	}

	{
		NSURL *a = [NSURL URLWithString:@"http://example.com/x"];
		NSURL *b = [NSURL URLWithString:@"http://example.com/x"];
		NSURL *caps = [NSURL URLWithString:@"http://EXAMPLE.com/x"];
		NSURL *c = [NSURL URLWithString:@"http://example.com/y"];

		check("url-equality",
		      a != nil && b != nil && caps != nil && c != nil &&
		      [a isEqual:b] && [a hash] == [b hash] &&
		      ![a isEqual:c] &&
		      /* BY SPELLING, case-sensitively: nothing here resolves a host. */
		      ![a isEqual:caps] && ![a isEqual:@"http://example.com/x"],
		      fn_why(caps));
	}

	{
		NSURL *u = [NSURL URLWithString:@"http://example.com/x"];
		NSString *described = u != nil ? [u description] : nil;

		check("url-identity",
		      u != nil &&
		      [u absoluteURL] == u &&
		      [[u relativeString] isEqualToString:[u absoluteString]] &&
		      described != nil &&
		      [described rangeOfString:@"http://example.com/x"].location != NSNotFound,
		      described == nil ? "(no description)"
			: [described UTF8String]);
	}

	{
		NSURL *theirs = foundation_url_http();
		NSURL *theirFile = foundation_url_file();
		NSURL *expected = [NSURL URLWithString:@"https://example.com:8443/a/b?x=1#top"];

		/* The nullable constructor is BOUND and GUARDED rather than passed inline:
		 * -isEqual:'s argument is a nonnull position, and the conversion flag says so. */
		check("cross-tu",
		      theirs != nil && theirFile != nil && expected != nil &&
		      [[theirs scheme] isEqualToString:@"https"] &&
		      [theirs port] != nil && [[theirs port] intValue] == 8443 &&
		      [theirs isEqual:expected] &&
		      [[theirFile path] isEqualToString:@"/System/Temporary Files/probe.txt"],
		      fn_why(theirFile));
	}

	/* ---- THE REST OF THE VALUE SURFACE (2026-09-30) -------------------------------------------
	 *
	 * These doors ride on the substrate this tree ALREADY HAD (the parse, the FSH rules, the relative
	 * resolver), so each check is a VALUE fact. Nullable getters are BOUND before use - this probe's
	 * standing rule, since a nullable in a nonnull position is a warning and this unit is built with
	 * ZERO DIAGNOSTICS. */

	{
		NSURL *u = [NSURL URLWithString:@"http://user:secret@example.com:8080/a/b.txt?q=1#frag"];
		NSString *user = u != nil ? [u user] : nil;
		NSString *password = u != nil ? [u password] : nil;
		NSString *last = u != nil ? [u lastPathComponent] : nil;
		NSString *ext = u != nil ? [u pathExtension] : nil;
		NSString *rel = u != nil ? [u relativePath] : nil;
		NSString *spec = u != nil ? [u resourceSpecifier] : nil;
		NSArray *pc = u != nil ? [u pathComponents] : nil;

		check("url-parts-extra",
		      u != nil &&
		      [u baseURL] == nil &&
		      user != nil && [user isEqualToString:@"user"] &&
		      password != nil && [password isEqualToString:@"secret"] &&
		      last != nil && [last isEqualToString:@"b.txt"] &&
		      ext != nil && [ext isEqualToString:@"txt"] &&
		      pc != nil && [pc count] >= 2 && [pc containsObject:@"a"] &&
		      [pc containsObject:@"b.txt"] &&
		      rel != nil && [rel isEqualToString:@"/a/b.txt?q=1#frag"] &&
		      spec != nil && [spec isEqualToString:@"//user:secret@example.com:8080/a/b.txt?q=1#frag"],
		      fn_why(u));
	}

	{
		NSURL *http = [NSURL URLWithString:@"http://h/a/b/../c/./d"];
		NSURL *file = [NSURL fileURLWithPath:@"/a/b/../c"];

		check("url-standardized",
		      http != nil && file != nil &&
		      [[[http standardizedURL] absoluteString] isEqualToString:@"http://h/a/c/d"] &&
		      [[[file standardizedURL] path] isEqualToString:@"/a/c"],
		      fn_why(http));
	}

	{
		NSURL *f = [NSURL fileURLWithPath:@"/System/Temporary Files/x"];
		NSURL *h = [NSURL URLWithString:@"http://h/x"];
		const char *rep = f != nil ? [f fileSystemRepresentation] : NULL;
		char buf[1024];
		char tiny[4];
		BOOL got = f != nil && [f getFileSystemRepresentation:buf maxLength:sizeof buf];
		BOOL toosmall = f != nil && [f getFileSystemRepresentation:tiny maxLength:sizeof tiny];

		check("url-file-system-representation",
		      f != nil && rep != NULL && strcmp(rep, "/System/Temporary Files/x") == 0 &&
		      got && strcmp(buf, "/System/Temporary Files/x") == 0 &&
		      !toosmall &&
		      h != nil && [h fileSystemRepresentation] == NULL,
		      fn_why(f));
	}

	{
		NSURL *rel = [NSURL URLWithString:@"b" relativeToURL:[NSURL URLWithString:@"http://h/a/"]];
		NSURL *abs2 = [[NSURL alloc] initWithString:@"http://h/x"
					     relativeToURL:[NSURL URLWithString:@"http://other/"]];
		NSURL *encoded = [NSURL URLWithString:@"http://h/a b" encodingInvalidCharacters:YES];
		NSURL *strict = [NSURL URLWithString:@"http://h/a b" encodingInvalidCharacters:NO];

		check("url-create-relative",
		      rel != nil && [[rel absoluteString] isEqualToString:@"http://h/a/b"] &&
		      abs2 != nil && [[abs2 absoluteString] isEqualToString:@"http://h/x"] &&
		      encoded != nil && [[encoded absoluteString] isEqualToString:@"http://h/a%20b"] &&
		      strict != nil && [[strict absoluteString] isEqualToString:@"http://h/a b"],
		      fn_why(rel));
	}

	{
		NSURL *base = [NSURL URLWithString:@"http://example.com/x"];
		NSData *data = base != nil ? [base dataRepresentation] : nil;
		NSURL *back = data != nil ? [NSURL URLWithDataRepresentation:data relativeToURL:nil] : nil;
		NSURL *absBack = data != nil ? [NSURL absoluteURLWithDataRepresentation:data relativeToURL:nil] : nil;

		check("url-data-representation",
		      base != nil && data != nil && back != nil && absBack != nil &&
		      [[back absoluteString] isEqualToString:@"http://example.com/x"] &&
		      [[absBack absoluteString] isEqualToString:@"http://example.com/x"],
		      fn_why(base));
	}

	{
		NSURL *dir = [NSURL fileURLWithPath:@"/a/b" isDirectory:YES];
		NSURL *comps = [NSURL fileURLWithPathComponents:
			[NSArray arrayWithObjects:@"a", @"b", nil]];
		NSURL *relf = [NSURL fileURLWithPath:@"rel/x"
					 relativeToURL:[NSURL fileURLWithPath:@"/base"]];
		NSURL *absf = [NSURL fileURLWithPath:@"/a b/c"
					 relativeToURL:[NSURL fileURLWithPath:@"/base"]];

		check("url-file-create",
		      dir != nil && [[dir absoluteString] isEqualToString:@"file:///a/b/"] &&
		      comps != nil && [[comps absoluteString] isEqualToString:@"file:///a/b"] &&
		      relf == nil &&
		      absf != nil && [[absf path] isEqualToString:@"/a b/c"],
		      fn_why(dir));
	}

	{
		NSURL *fromRep = [NSURL fileURLWithFileSystemRepresentation:"/a/b" isDirectory:NO relativeToURL:nil];
		NSURL *fromRepDir = [NSURL fileURLWithFileSystemRepresentation:"/a/b" isDirectory:YES relativeToURL:nil];

		check("url-file-fs-rep",
		      fromRep != nil && [[fromRep absoluteString] isEqualToString:@"file:///a/b"] &&
		      fromRepDir != nil && [[fromRepDir absoluteString] isEqualToString:@"file:///a/b/"],
		      fn_why(fromRep));
	}

	{
		NSURL *f = [NSURL fileURLWithPath:@"/a/b"];
		NSURL *fd = [NSURL fileURLWithPath:@"/a/b/"];
		NSURL *h = [NSURL URLWithString:@"http://h/x"];

		check("url-convert",
		      f != nil && fd != nil && h != nil &&
		      [f fileURL] == f && [f filePathURL] == f &&
		      [h fileURL] == nil && [h filePathURL] == nil &&
		      ![f isFileReferenceURL] && ![h isFileReferenceURL] &&
		      ![f hasDirectoryPath] && [fd hasDirectoryPath],
		      fn_why(f));
	}

	{
		NSURL *ap = [[NSURL fileURLWithPath:@"/a"] URLByAppendingPathComponent:@"b" isDirectory:YES];
		NSURL *ap2 = [[NSURL fileURLWithPath:@"/a"] URLByAppendingPathComponent:@"b" isDirectory:NO];

		check("url-append-component-dir",
		      ap != nil && [[ap absoluteString] isEqualToString:@"file:///a/b/"] &&
		      ap2 != nil && [[ap2 absoluteString] isEqualToString:@"file:///a/b"],
		      fn_why(ap));
	}

	{
		NSURL *sys = [NSURL fileURLWithPath:@"/System"];
		NSURL *resolved = sys != nil ? [sys URLByResolvingSymlinksInPath] : nil;
		char rb[1024];
		const char *rp = realpath("/System", rb);
		NSURL *up = [NSURL fileURLWithPath:@"/a/../b"];
		NSURL *dot = [NSURL fileURLWithPath:@"/a/./b"];

		check("url-resolve-symlinks",
		      sys != nil && resolved != nil && rp != NULL &&
		      strcmp([[resolved path] UTF8String], rp) == 0 &&
		      up != nil && [[[up URLByStandardizingPath] path] isEqualToString:@"/b"] &&
		      dot != nil && [[[dot URLByStandardizingPath] path] isEqualToString:@"/a/b"],
		      fn_why(resolved));
	}

	{
		NSURL *parts = [[NSURL alloc] initWithScheme:@"http" host:@"example.com" path:@"/a"];
		NSString *host = parts != nil ? [parts host] : nil;

		check("url-init-with-parts",
		      parts != nil &&
		      [[parts scheme] isEqualToString:@"http"] &&
		      host != nil && [host isEqualToString:@"example.com"] &&
		      [[parts path] isEqualToString:@"/a"] &&
		      [[parts absoluteString] isEqualToString:@"http://example.com/a"],
		      fn_why(parts));
	}

	{
		/* THE NSURLHandle-BACKED DEPRECATED DOORS (2026-10-01): the three the header now closes are
		 * DELEGATIONS to the handle this library already ships, and each is a VALUE fact that asks for NO
		 * fetch - the handle is CONSTRUCTED without one, and a property that was SET reads back from the
		 * bag BEFORE NSURLHandle would load (its -propertyForKey: checks the dictionary first). So this
		 * probe never opens a socket. Nullable getters are BOUND before use, this probe's standing rule. */
		NSURL *u = [NSURL URLWithString:@"http://example.com/handle"];
		NSURLHandle *h = u != nil ? [u URLHandleUsingCache:YES] : nil;
		NSURLHandle *h2 = u != nil ? [u URLHandleUsingCache:YES] : nil;
		BOOL roundtrip = NO;

		if (u != nil) {
			[u setProperty:@"seven" forKey:@"probe-key"];
			roundtrip = [[u propertyForKey:@"probe-key"] isEqual:@"seven"];
		}

		check("url-handle-deprecated",
		      u != nil && h != nil && h2 != nil && h == h2 &&
		      [[h URL] isEqual:u] &&
		      roundtrip &&
		      [[u propertyForKey:@"probe-key"] isEqual:@"seven"],
		      "the two cached YES calls answer ONE handle, and a set property round-trips");
	}

	{
		/* THE PARAMETER STRING (Apple 10.4, deprecated and PURE): the path's tail after its FIRST ';',
		 * taken raw from the parse exactly as -query/-fragment are, and nil when the path has none. */
		NSURL *p = [NSURL URLWithString:@"http://example.com/a;v=1;w=2?q=1#f"];
		NSURL *n = [NSURL URLWithString:@"http://example.com/a/b"];
		NSString *ps = p != nil ? [p parameterString] : nil;
		NSString *ns = n != nil ? [n parameterString] : nil;

		check("url-parameter-string",
		      p != nil && n != nil &&
		      ps != nil && [ps isEqualToString:@"v=1;w=2"] &&
		      ns == nil,
		      /* REASONED, NOT MEASURED: that the tail is taken WITHOUT the leading ';' (RFC 2396's
		       * `segment` puts the ';' in the grammar, not the value) and that a path with none answers
		       * NIL - Apple's page for this deprecated door is terse, so the guest run settles it. */
		      ps == nil ? "(nil parameterString)" : [ps UTF8String]);
	}

	printf("FOUNDATION-URL RESULT ok=%d fail=%d\n", okc, failc);
	/* The exit status, in the probe's OWN output. After a probe the console can stop
	 * serving INPUT for a while (the tier residual), so an `echo $?` that the harness
	 * types may never run. The probe knows exactly what it returns, so it says so - and
	 * this is the same value: failc ? 1 : 0 is the return statement below. */
	printf("FOUNDATION-URL-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-URL DONE\n");
	return failc ? 1 : 0;
}
