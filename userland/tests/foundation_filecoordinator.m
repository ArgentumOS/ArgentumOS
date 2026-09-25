/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_filecoordinator, unit of 1 — W8 slice 7b's acceptance: the SYNCHRONOUS ACCESSOR DOORS.
 * docs/design/foundation-plan.md §60.
 *
 * ONE unit, importing only <Foundation/Foundation.h> plus the POSIX calls its fixture makes. It builds a
 * tree of its own under /System/Temporary Files and removes it at the end and at the start.
 *
 * WHAT IT MEASURES, and each one is a rule from the doors' own page:
 *   coordinator-runs-the-read-accessor-once  the accessor runs EXACTLY once and is handed a URL; with no
 *                                          error the caller's out-error stays nil;
 *   coordinator-runs-the-write-accessor-once  the writing door has the same shape;
 *   coordinator-runs-a-read-and-a-write-in-one-accessor / coordinator-runs-two-writes-in-one-accessor
 *                                          the 2-item forms hand the accessor BOTH URLs, once;
 *   coordinator-resolves-a-symbolic-link-when-asked  NSFileCoordinatorReadingResolvesSymbolicLink, asserted
 *                                          FROM BOTH SIDES: with it the accessor sees the resolved item,
 *                                          without it the link itself;
 *   coordinator-refuses-without-running-the-accessor  "the error is returned in this parameter and the
 *                                          block ... is not executed" - so every refusal is asserted by
 *                                          counting the accessor's calls as well as the error;
 *   coordinator-refuses-a-missing-accessor  a nil block is refused rather than crashed into;
 *   coordinator-owes-the-asynchronous-and-presenter-doors  THE BOUNDARY, ASSERTED: the doors this slice
 *                                          does not ship do not exist (no daemon, no presenters yet, so no
 *                                          half-built door answers for them);
 *   probe-tree-removed                    the tree is gone.
 */

#import <Foundation/Foundation.h>

#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#define PROBE_ROOT "/System/Temporary Files/nsfilecoordinator-probe"

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-FILECOORDINATOR %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-FILECOORDINATOR %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

static id fn_url(NSString *path)
{
	return [NSURL fileURLWithPath:path];
}

static NSString *fn_path(NSString *relative)
{
	return [NSString stringWithFormat:@"%s/%@", PROBE_ROOT, relative];
}

/* A NON-FILE URL ROUTED THROUGH `id`, this tier's rule for -Werror=nullable-to-nonnull-conversion. */
static id fn_web(void)
{
	return [NSURL URLWithString:@"https://example.invalid/x"];
}

static void fn_write(NSString *path, const char *bytes)
{
	FILE *f = fopen([path UTF8String], "w");

	if (f != NULL) {
		fputs(bytes, f);
		fclose(f);
	}
}

int main(void)
{
	NSFileManager *manager = [NSFileManager defaultManager];
	NSFileCoordinator *coordinator = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
	NSURL *item = fn_url(fn_path(@"real.txt"));
	NSURL *link = fn_url(fn_path(@"link.txt"));

	[manager removeItemAtPath:@PROBE_ROOT error:NULL];
	mkdir(PROBE_ROOT, 0755);
	fn_write(fn_path(@"real.txt"), "content");
	fn_write(fn_path(@"other.txt"), "other");
	symlink("real.txt", [fn_path(@"link.txt") UTF8String]);

	{
		__block NSUInteger calls = 0;
		__block NSURL *handed = nil;
		NSError *error = nil;

		[coordinator coordinateReadingItemAtURL:item
						options:0
						  error:&error
					     byAccessor:^(NSURL *newURL) {
			calls++;
			handed = newURL;
		}];
		check("coordinator-runs-the-read-accessor-once",
		      calls == 1 && error == nil && [handed isEqual:item],
		      [NSString stringWithFormat:@"calls=%lu error=%@ handed=%@",
			(unsigned long)calls, error, handed]);
	}

	{
		__block NSUInteger calls = 0;
		__block NSURL *handed = nil;
		NSError *error = nil;

		[coordinator coordinateWritingItemAtURL:item
						options:NSFileCoordinatorWritingForDeleting
						  error:&error
					     byAccessor:^(NSURL *newURL) {
			calls++;
			handed = newURL;
		}];
		check("coordinator-runs-the-write-accessor-once",
		      calls == 1 && error == nil && [handed isEqual:item],
		      [NSString stringWithFormat:@"calls=%lu error=%@ handed=%@",
			(unsigned long)calls, error, handed]);
	}

	{
		__block NSUInteger calls = 0;
		__block NSURL *read = nil;
		__block NSURL *written = nil;
		NSError *error = nil;

		[coordinator coordinateReadingItemAtURL:item
						options:0
				       writingItemAtURL:fn_url(fn_path(@"other.txt"))
						options:NSFileCoordinatorWritingForMerging
						  error:&error
					     byAccessor:^(NSURL *newReadingURL, NSURL *newWritingURL) {
			calls++;
			read = newReadingURL;
			written = newWritingURL;
		}];
		check("coordinator-runs-a-read-and-a-write-in-one-accessor",
		      calls == 1 && error == nil && [read isEqual:item] &&
		      [[written path] hasSuffix:@"other.txt"],
		      [NSString stringWithFormat:@"calls=%lu read=%@ written=%@", (unsigned long)calls,
			read, written]);
	}

	{
		__block NSUInteger calls = 0;
		__block BOOL both = NO;
		NSError *error = nil;

		[coordinator coordinateWritingItemAtURL:item
						options:NSFileCoordinatorWritingForMoving
				       writingItemAtURL:fn_url(fn_path(@"other.txt"))
						options:NSFileCoordinatorWritingForReplacing
						  error:&error
					     byAccessor:^(NSURL *newURL1, NSURL *newURL2) {
			calls++;
			both = newURL1 != nil && newURL2 != nil && ![newURL1 isEqual:newURL2];
		}];
		check("coordinator-runs-two-writes-in-one-accessor",
		      calls == 1 && error == nil && both,
		      [NSString stringWithFormat:@"calls=%lu both=%d error=%@", (unsigned long)calls,
			(int)both, error]);
	}

	{
		/* THE ONE OPTION WITH A MEANING HERE, FROM BOTH SIDES: resolved when asked, and the LINK
		 * ITSELF when not - which is what makes the option observable rather than decorative. */
		__block NSString *withOption = nil;
		__block NSString *withoutOption = nil;
		NSError *error = nil;

		[coordinator coordinateReadingItemAtURL:link
						options:NSFileCoordinatorReadingResolvesSymbolicLink
						  error:&error
					     byAccessor:^(NSURL *newURL) {
			withOption = [newURL path];
		}];
		[coordinator coordinateReadingItemAtURL:link
						options:0
						  error:&error
					     byAccessor:^(NSURL *newURL) {
			withoutOption = [newURL path];
		}];
		check("coordinator-resolves-a-symbolic-link-when-asked",
		      [withOption isEqual:fn_path(@"real.txt")] &&
		      [withoutOption isEqual:fn_path(@"link.txt")],
		      [NSString stringWithFormat:@"with=%@ without=%@", withOption, withoutOption]);
	}

	{
		/* APPLE'S SENTENCE, ASSERTED BY COUNTING: "the error is returned in this parameter and the block
		 * in the [accessor] parameter IS NOT EXECUTED". */
		__block NSUInteger calls = 0;
		NSError *nilError = nil;
		NSError *nonFileError = nil;
		NSError *nilBlockError = nil;

		[coordinator coordinateReadingItemAtURL:nil
						options:0
						  error:&nilError
					     byAccessor:^(NSURL *newURL) {
			(void)newURL;
			calls++;
		}];
		[coordinator coordinateReadingItemAtURL:fn_web()
						options:0
						  error:&nonFileError
					     byAccessor:^(NSURL *newURL) {
			(void)newURL;
			calls++;
		}];
		[coordinator coordinateReadingItemAtURL:item
						options:0
						  error:&nilBlockError
					     byAccessor:(void (^)(NSURL *))NULL];
		check("coordinator-refuses-without-running-the-accessor",
		      calls == 0 && nilError != nil && nonFileError != nil && nilBlockError != nil &&
		      [nilError code] == EINVAL && [nonFileError code] == EINVAL,
		      [NSString stringWithFormat:@"calls=%lu nil=%ld nonfile=%ld nilblock=%ld",
			(unsigned long)calls,
			(long)(nilError != nil ? [nilError code] : -1),
			(long)(nonFileError != nil ? [nonFileError code] : -1),
			(long)(nilBlockError != nil ? [nilBlockError code] : -1)]);
	}

	{
		/* AND A REFUSAL THAT IS NOT ABOUT THE URL: a missing accessor is a caller error and reports as
		 * one, rather than being invoked as a null pointer. */
		NSError *error = nil;

		[coordinator coordinateWritingItemAtURL:item
						options:0
						  error:&error
					     byAccessor:(void (^)(NSURL *))NULL];
		check("coordinator-refuses-a-missing-accessor", error != nil && [error code] == EINVAL,
		      [NSString stringWithFormat:@"error=%@", error]);
	}

	{
		/* THE BOUNDARY, ASSERTED: the doors this slice does not ship do not EXIST on the object, so
		 * nothing half-built answers for the family's cross-process and presenter halves. */
		NSArray *owed = [NSArray arrayWithObjects:
			@"coordinateAccessWithIntents:queue:byAccessor:",
			@"prepareForReadingItemsAtURLs:options:writingItemsAtURLs:options:error:byAccessor:",
			@"itemAtURL:willMoveToURL:", @"itemAtURL:didMoveToURL:", @"cancel", nil];
		NSMutableArray *present = [NSMutableArray array];
		NSUInteger i;

		for (i = 0; i < [owed count]; i++) {
			NSString *name = [owed objectAtIndex:i];

			if ([coordinator respondsToSelector:NSSelectorFromString(name)]) {
				[present addObject:name];
			}
		}
		check("coordinator-owes-the-asynchronous-and-presenter-doors",
		      [present count] == 0,
		      [NSString stringWithFormat:@"this slice shipped more than it says: %@",
			[present componentsJoinedByString:@", "]]);
	}

	{
		NSError *cleanupError = nil;
		BOOL removed = [manager removeItemAtPath:@PROBE_ROOT error:&cleanupError];

		check("probe-tree-removed", removed && ![manager fileExistsAtPath:@PROBE_ROOT],
		      cleanupError != nil ? [cleanupError localizedDescription] : @"still there");
	}

	printf("FOUNDATION-FILECOORDINATOR RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-FILECOORDINATOR DONE\n");
	return failc == 0 ? 0 : 1;
}
