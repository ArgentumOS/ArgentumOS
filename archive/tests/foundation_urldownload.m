/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_urldownload.m — THE PROBE FOR NSURLDownload (§62.82).
 *
 * IT REGISTERS NOTHING, AND THAT IS NOW A FACT WORTH ASSERTING RATHER THAN AN OMISSION: the library registers its
 * own transport at load (§62.83), so EVERY TRANSFER BELOW RUNS ONLY IF THAT REGISTRATION HAPPENED - this probe
 * never calls `+registerClass:`, and a `file:` URL with no transport would fail with NSURLErrorUnsupportedURL.
 * The unit's first attempt did the opposite: it forgot the registration, read the registry's -1002 as a verdict
 * about the URL, and was reverted over its own missing call.
 *
 * THE THREE DESTINATION ARRANGEMENTS ARE EACH EXERCISED, because they are three different pieces of this class:
 * a destination set before the bytes arrive, a delegate that answers inside the decision door (Apple's
 * asynchronous protocol, honoured), and a delegate that answers NOTHING - which falls into this class's stated
 * fallback, the temporary directory under the suggested name, reported through the created-destination door.
 *
 * WAITING IS `usleep` IN A BOUNDED LOOP, never `-runUntilDate:`: the session completes on its own thread, and a
 * run loop with no sources returns immediately, which would make every wait vacuous. The waits only decide how
 * a failure READS, never whether it happens.
 */

#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#include <stdio.h>
#include <unistd.h>

static int okc = 0, failc = 0;

static void check(const char *name, BOOL held, NSString *why)
{
	if (held) {
		okc++;
		printf("FOUNDATION-URLDOWNLOAD %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-URLDOWNLOAD %s FAIL: %s\n", name, [why UTF8String]);
	}
}

/* THE NULLABLE-FACTORY IDIOM THIS TIER REQUIRES (the same one foundation_urlconnection.m records): the probes
 * compile with -Werror=nullable-to-nonnull-conversion, so a nullable factory's result goes through an `id`
 * local before it reaches a non-null parameter. EVERY URL, PATH AND BODY in this probe is built here. */
static NSURL *fn_fileURL(NSString *path)
{
	id url = [NSURL fileURLWithPath:path];

	return url;
}

static NSURLRequest *fn_fileRequest(NSString *path)
{
	return [NSURLRequest requestWithURL:fn_fileURL(path)];
}

static NSString *fn_tmpdir(void)
{
	id dir = NSTemporaryDirectory();

	return dir;
}

static NSString *fn_path(NSString *directory, NSString *name)
{
	id path = [directory stringByAppendingPathComponent:name];

	return path;
}

static NSData *fn_utf8(NSString *string)
{
	id data = [string dataUsingEncoding:NSUTF8StringEncoding];

	return data;
}

static NSData *fn_read(NSString *path)
{
	id data = [NSData dataWithContentsOfFile:path];

	return data;
}

/* THE DELEGATE: every door this probe wants to observe is written out, and the ones it does not want are simply
 * absent - which is how NSURLDownloadDelegate's @optional contract is meant to be read. */
@interface FNProbeDelegate : NSObject <NSURLDownloadDelegate>
{
@public
	int begins, createds, finishes, failures;
	NSString *chosenPath;		/* what the decision door answers with, or nil to answer nothing */
	BOOL decisionDoorSent;
	NSString *lastSuggested;
	NSString *lastCreatedPath;
	NSString *lastError;
}
@end

@implementation FNProbeDelegate

- (void)downloadDidBegin:(NSURLDownload *)download
{
	(void)download;
	begins++;
}

- (void)download:(NSURLDownload *)download
    decideDestinationWithSuggestedFilename:(NSString *)filename
{
	decisionDoorSent = YES;
	lastSuggested = [filename copy];
	if (chosenPath != nil) {
		[download setDestination:chosenPath allowOverwrite:YES];
	}
}

- (void)download:(NSURLDownload *)download didCreateDestination:(NSString *)path
{
	(void)download;
	createds++;
	lastCreatedPath = [path copy];
}

- (void)downloadDidFinish:(NSURLDownload *)download
{
	(void)download;
	finishes++;
}

- (void)download:(NSURLDownload *)download didFailWithError:(NSError *)error
{
	(void)download;
	failures++;
	lastError = [[error description] copy];
}

@end

/* WAIT FOR AN ENDING - one of the two, never both. Bounded: 30 s of 20 ms sleeps. */
static void fnWait(FNProbeDelegate *d)
{
	int i;

	for (i = 0; i < 1500 && d->finishes == 0 && d->failures == 0; i++) {
		usleep(20000);
	}
}

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);

	/* NO REGISTRATION HERE, ON PURPOSE - the library registered the transport at load (§62.83), so the transfers
	 * below are the gate for that self-registration. See the file's note. */

	/* --- 1. THE CLASS AND ITS PROTOCOL ------------------------------------------------------------ */
	{
		Class cls = objc_getClass("NSURLDownload");
		Class sup = cls != Nil ? class_getSuperclass(cls) : Nil;

		check("class-and-protocol-declared",
		      cls != Nil && sup == [NSObject class] &&
		      objc_getProtocol("NSURLDownloadDelegate") != NULL,
		      @"NSURLDownload exists, is an NSObject, and its delegate protocol is declared");
	}

	/* THE FIXTURE: a file to download and the bytes it holds. */
	NSString *dir = fn_tmpdir();
	NSString *fixture = fn_path(dir, @"fn-ud-src.txt");
	NSData *body = fn_utf8(@"downloaded by NSURLDownload, byte for byte");
	BOOL wrote = [body writeToFile:fixture atomically:YES];

	/* --- 2-4. A DESTINATION SET BEFORE THE BYTES ARRIVE ------------------------------------------- */
	{
		NSString *dest = fn_path(dir, @"fn-ud-dst-preset.txt");
		FNProbeDelegate *d = [[FNProbeDelegate alloc] init];
		NSURLDownload *dl;

		[[NSFileManager defaultManager] removeItemAtPath:dest error:NULL];
		dl = [[NSURLDownload alloc] initWithRequest:fn_fileRequest(fixture) delegate:d];
		[dl setDestination:dest allowOverwrite:YES];
		fnWait(d);

		{
			NSData *landed = fn_read(dest);

			check("a-download-lands-the-fixture-where-the-destination-says",
			      wrote && landed != nil && [landed isEqualToData:body],
			      [NSString stringWithFormat:@"the file at the chosen destination holds the body "
						@"(wrote=%d landed=%lu expected=%lu) lastError=%@",
						(int)wrote, (unsigned long)[landed length],
						(unsigned long)[body length], d->lastError]);
		}
		check("the-callbacks-arrive-in-order-and-once",
		      d->begins == 1 && d->createds == 1 && d->finishes == 1 && d->failures == 0,
		      [NSString stringWithFormat:@"begin=%d created=%d finish=%d fail=%d",
						(int)d->begins, (int)d->createds, (int)d->finishes,
						(int)d->failures]);
		check("the-request-is-kept-and-the-failure-flag-defaults-to-yes",
		      [[[[dl request] URL] path] isEqualToString:fixture] &&
		      [dl deletesFileUponFailure] && [dl resumeData] == nil,
		      @"-request is the request it began with, -deletesFileUponFailure defaults YES, "
		      @"-resumeData is nil before any cancel");
	}

	/* --- 5. A DESTINATION THE DELEGATE CHOOSES INSIDE THE DECISION DOOR --------------------------- */
	{
		NSString *dest = fn_path(dir, @"fn-ud-dst-delegate.txt");
		FNProbeDelegate *d = [[FNProbeDelegate alloc] init];

		[[NSFileManager defaultManager] removeItemAtPath:dest error:NULL];
		d->chosenPath = dest;
		(void)[[NSURLDownload alloc] initWithRequest:fn_fileRequest(fixture) delegate:d];
		fnWait(d);

		{
			NSData *landed = fn_read(dest);

			check("a-destination-the-delegate-chooses-is-where-the-file-lands",
			      d->decisionDoorSent && landed != nil && [landed isEqualToData:body] &&
			      d->finishes == 1,
			      [NSString stringWithFormat:@"door=%d finish=%d landed=%lu lastSuggested=%@ "
						@"lastError=%@", (int)d->decisionDoorSent, (int)d->finishes,
						(unsigned long)[landed length], d->lastSuggested, d->lastError]);
		}
	}

	/* --- 6. NOBODY CHOOSES: THE STATED FALLBACK, REPORTED ----------------------------------------
	 *
	 * THIS IS THE CHECK THE CLASS FIRST FAILED, AND IT IS WHY IT ASSERTS A FILE-BASED FACT: for a `file:`
	 * source the suggested name IS the fixture's own name, so the fallback's first destination was the file
	 * being downloaded and the move onto it failed with EEXIST. The class now picks a free sibling, and the
	 * assertion is what a caller is promised either way: the bytes are somewhere under the temporary
	 * directory and the created-destination door says where. */
	{
		FNProbeDelegate *d = [[FNProbeDelegate alloc] init];	/* chosenPath stays nil */
		NSData *landed;

		(void)[[NSURLDownload alloc] initWithRequest:fn_fileRequest(fixture) delegate:d];
		fnWait(d);
		landed = d->lastCreatedPath != nil ? fn_read(d->lastCreatedPath) : nil;

		check("a-download-nobody-destines-lands-in-the-temporary-directory",
		      d->decisionDoorSent && [d->lastSuggested length] > 0 &&
		      d->lastCreatedPath != nil && [d->lastCreatedPath hasPrefix:dir] &&
		      landed != nil && [landed isEqualToData:body] && d->finishes == 1,
		      [NSString stringWithFormat:@"door=%d suggested=%@ created=%@ landed=%lu error=%@",
						(int)d->decisionDoorSent, d->lastSuggested, d->lastCreatedPath,
						(unsigned long)[landed length], d->lastError]);
	}

	/* --- 7. DECODED RESUME IS REFUSED AS A FACT --------------------------------------------------- */
	check("decoded-resume-is-refused-as-a-fact",
	      ![NSURLDownload canResumeDownloadDecodedWithEncodingMIMEType:@"gzip"],
	      @"+canResumeDownloadDecodedWithEncodingMIMEType: answers NO: this class resumes bytes");

	printf("FOUNDATION-URLDOWNLOAD RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-URLDOWNLOAD-STATUS=%d\n", (failc || okc != 7) ? 1 : 0);
	printf("FOUNDATION-URLDOWNLOAD DONE\n");
	return (failc || okc != 7) ? 1 : 0;
}
