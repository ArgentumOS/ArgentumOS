/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_fileversion, unit of 1 — W8 slice 8a's acceptance. docs/design/foundation-plan.md §60.
 *
 * ONE unit, importing only <Foundation/Foundation.h> plus the POSIX calls its fixture makes. It works in a
 * tree of its own under /System/Temporary Files and removes it at the end and at the start.
 *
 * THE CLASS IS WRITTEN AROUND A VERSION STORE, WHICH THIS SYSTEM DOES NOT HAVE, so the probe is built
 * around the difference between what a store is needed for and what it is not: the CURRENT version is a
 * fact about a file, the collections are EMPTY because nothing stores versions, and the doors that would
 * need somewhere to PUT a version are refused BY NAME rather than answering in silence.
 *
 *   version-the-current-version-describes-the-item  its URL, its name, its date (through TWO doors, the
 *                                          version's and NSFileManager's) and what is local about it;
 *   version-a-missing-item-has-no-current-version  nil, which is what Apple's nullable return allows;
 *   version-there-are-no-other-versions    an EMPTY ARRAY and not nil: "no versions other than the current
 *                                          one" is the postcondition;
 *   version-the-identifier-round-trips     the opaque identifier this class hands out can be handed back,
 *                                          and anything else is answered nil rather than matched loosely;
 *   version-there-are-no-conflicts         the conflict collection is empty for the same reason;
 *   version-removing-other-versions-is-already-true  the postcondition holds, so the door succeeds;
 *   version-the-store-doors-refuse-by-name  adding, removing THIS version and staging a new one are all
 *                                          refused WITH an error (or nil where Apple gives no error
 *                                          parameter), because there is nowhere to put a version;
 *   version-nonlocal-versions-answer-empty  the completion handler is CALLED, with an empty array: the
 *                                          question was asked and answered rather than left hanging for a
 *                                          cloud that will never arrive;
 *   probe-tree-removed                     the tree is gone.
 */

#import <Foundation/Foundation.h>

#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#define PROBE_ROOT "/System/Temporary Files/nsfileversion-probe"

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-FILEVERSION %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-FILEVERSION %s FAIL %s\n", name, detail != nil ? [detail UTF8String] : "");
	}
}

static id fn_url(NSString *path)
{
	return [NSURL fileURLWithPath:path];
}

/* THE NULLABLE IDENTIFIER ROUTED THROUGH `id`, this tier's rule for -Werror=nullable-to-nonnull. */
static id fn_identifier(NSFileVersion *version)
{
	return [version persistentIdentifier];
}

static NSString *fn_path(NSString *relative)
{
	return [NSString stringWithFormat:@"%s/%@", PROBE_ROOT, relative];
}

int main(void)
{
	NSFileManager *manager = [NSFileManager defaultManager];
	NSURL *item = fn_url(fn_path(@"versioned.txt"));
	NSFileVersion *current;

	[manager removeItemAtPath:@PROBE_ROOT error:NULL];
	mkdir(PROBE_ROOT, 0755);
	{
		FILE *f = fopen([fn_path(@"versioned.txt") UTF8String], "w");

		if (f != NULL) {
			fputs("a version of a file", f);
			fclose(f);
		}
	}
	current = [NSFileVersion currentVersionOfItemAtURL:item];

	{
		NSDictionary *attributes = [manager attributesOfItemAtPath:fn_path(@"versioned.txt") error:NULL];
		id byManager = [attributes objectForKey:NSFileModificationDate];

		check("version-the-current-version-describes-the-item",
		      current != nil && [[current URL] isEqual:item] &&
		      [[current localizedName] isEqual:@"versioned.txt"] &&
		      [[current modificationDate] isEqual:byManager] &&
		      [current hasLocalContents] && ![current hasThumbnail] && ![current isConflict] &&
		      [[current persistentIdentifier] length] > 0,
		      [NSString stringWithFormat:@"name=%@ date=%@ (manager says %@) id=%@",
			[current localizedName], [current modificationDate], byManager,
			[current persistentIdentifier]]);
	}

	check("version-a-missing-item-has-no-current-version",
	      [NSFileVersion currentVersionOfItemAtURL:fn_url(fn_path(@"never-created"))] == nil,
	      @"an item that is not there has no current version");

	{
		NSArray *others = [NSFileVersion otherVersionsOfItemAtURL:item];

		check("version-there-are-no-other-versions",
		      others != nil && [others count] == 0,
		      others == nil ? @"nil, where the postcondition is an empty array" : @"empty");
	}

	{
		NSFileVersion *found = [NSFileVersion versionOfItemAtURL:item
						     forPersistentIdentifier:fn_identifier(current)];
		NSFileVersion *bogus = [NSFileVersion versionOfItemAtURL:item
						     forPersistentIdentifier:@"not-an-identifier-we-handed-out"];

		check("version-the-identifier-round-trips",
		      found != nil && [[found URL] isEqual:item] && bogus == nil,
		      [NSString stringWithFormat:@"found=%@ bogus=%@", found, bogus]);
	}

	{
		NSArray *conflicts = [NSFileVersion unresolvedConflictVersionsOfItemAtURL:item];

		check("version-there-are-no-conflicts",
		      conflicts != nil && [conflicts count] == 0,
		      conflicts == nil ? @"nil, where the postcondition is an empty array" : @"empty");
	}

	{
		NSError *error = nil;
		BOOL removed = [NSFileVersion removeOtherVersionsOfItemAtURL:item error:&error];

		check("version-removing-other-versions-is-already-true", removed && error == nil,
		      [NSString stringWithFormat:@"removed=%d error=%@", (int)removed, error]);
	}

	{
		NSError *addError = nil;
		NSError *removeError = nil;
		NSFileVersion *added = [NSFileVersion addVersionOfItemAtURL:item
						     withContentsOfURL:item
							       options:0
								 error:&addError];
		NSURL *staging = [NSFileVersion temporaryDirectoryURLForNewVersionOfItemAtURL:item];
		BOOL refused = ![current removeAndReturnError:&removeError];

		check("version-the-store-doors-refuse-by-name",
		      added == nil && addError != nil && staging == nil && refused &&
		      removeError != nil && [addError code] == ENOTSUP && [removeError code] == ENOTSUP,
		      [NSString stringWithFormat:@"add=%@ (%ld) staging=%@ remove=%ld", addError,
			(long)(addError != nil ? [addError code] : -1), staging,
			(long)(removeError != nil ? [removeError code] : -1)]);
	}

	{
		__block NSUInteger calls = 0;
		__block BOOL empty = NO;
		__block BOOL errored = YES;

		[NSFileVersion getNonlocalVersionsOfItemAtURL:item
				    completionHandler:^(NSArray *nonlocalVersions, NSError *error) {
			calls++;
			empty = nonlocalVersions != nil && [nonlocalVersions count] == 0;
			errored = error != nil;
		}];
		check("version-nonlocal-versions-answer-empty", calls == 1 && empty && !errored,
		      [NSString stringWithFormat:@"calls=%lu empty=%d error=%d", (unsigned long)calls,
			(int)empty, (int)errored]);
	}

	{
		NSError *cleanupError = nil;
		BOOL removed = [manager removeItemAtPath:@PROBE_ROOT error:&cleanupError];

		check("probe-tree-removed", removed && ![manager fileExistsAtPath:@PROBE_ROOT],
		      cleanupError != nil ? [cleanupError localizedDescription] : @"still there");
	}

	printf("FOUNDATION-FILEVERSION RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-FILEVERSION DONE\n");
	return failc == 0 ? 0 : 1;
}
