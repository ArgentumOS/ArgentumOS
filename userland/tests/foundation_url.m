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

int main(void)
{
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
		 * objc_getClass is the RUNTIME's lookup, which is what a probe should use:
		 * NSClassFromString is a Foundation function this library does not claim.
		 */
		check("url-absent",
		      objc_getClass("NSURLSession") == NULL &&
		      objc_getClass("NSURLRequest") == NULL &&
		      objc_getClass("NSURLConnection") == NULL &&
		      ![NSURL respondsToSelector:sel_registerName(
			  "URLByResolvingBookmarkData:options:relativeToURL:"
			  "bookmarkDataIsStale:error:")],
		      "the URL LOADING system is absent: no session, request or connection class");

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

	printf("FOUNDATION-URL RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-URL DONE\n");
	return failc ? 1 : 0;
}
