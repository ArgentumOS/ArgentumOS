/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_directoryenumerator, unit of 1 — W8 slice 1's acceptance for NSDirectoryEnumerator.
 * docs/design/foundation-plan.md §60.
 *
 * ONE unit, importing only <Foundation/Foundation.h> (plus <unistd.h> for the symlink(2) the tree
 * needs). IT WORKS IN A TREE OF ITS OWN MAKING under /System/Temporary Files — this system's temp
 * directory, spelled the FSH way — and REMOVES THE WHOLE TREE AT THE END, because a probe that leaves
 * litter behind changes the system it is measuring.
 *
 * WHAT IT MEASURES, and each one is a RULE rather than an observation:
 *   walk-yields-the-whole-subtree            every descendant exactly once, pre-order, and nothing else;
 *   walk-paths-are-relative-to-the-directory  NOTHING starts with the directory's own path — Apple's
 *                                            own class page says the pathnames are relative;
 *   walk-paths-join-back-to-real-items       and appending one to the directory gives the real path,
 *                                            which is what Apple's own Objective-C example does;
 *   level-counts-the-enumerated-directory-as-zero  the directory is 0, so its children are 1 and the
 *                                            deepest item here is 3;
 *   file-attributes-are-the-most-recent-items  the CURRENT item's dictionary (a file's size, a
 *                                            directory's type), and nil before the first item;
 *   directory-attributes-are-the-starting-directorys  the dictionary the walk STARTED in, which is a
 *                                            different question from the one above;
 *   the-walk-does-not-resolve-symbolic-links both links answer as LINKS and neither is entered;
 *   a-file-enumerates-nothing                Apple's word: a spent enumerator, not an error and not nil;
 *   all-objects-is-the-rest-of-the-walk       the inherited cursor contract;
 *   skip-descendents-prunes-one-level        the pruning is exactly the subtree, and the item stays;
 *   skip-descendants-is-the-same-method      the two spellings are one method;
 *   subpaths-agree-with-the-enumerator       the array doors and the walk are one walk;
 *   a-symbolic-link-given-as-the-path-is-traversed  a link AS the path IS followed (opendir(2) does);
 *   an-empty-directory-answers-an-empty-array-not-nil  "no items" is not "not a directory";
 *   subpaths-refuse-what-is-not-a-directory  nil AND the errno, for a file (ENOTDIR) and for a name
 *                                            that is not there (ENOENT);
 *   post-order-is-not-what-this-slice-builds  the boundary this slice NAMES rather than hides;
 *   probe-tree-removed                       the tree is gone.
 */

#import <Foundation/Foundation.h>

#include <stdio.h>
#include <unistd.h>		/* symlink(2): the links are made here, not by the service */
#include <fcntl.h>		/* open(2) for the fixture files */
#include <sys/stat.h>		/* mkdir(2) */
#include <errno.h>
#include <string.h>

#define PROBE_ROOT "/System/Temporary Files/nsdirectoryenumerator-probe"

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-DIRECTORYENUMERATOR %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-DIRECTORYENUMERATOR %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

static NSString *fn_path(NSString *relative)
{
	return [NSString stringWithFormat:@"%s/%@", PROBE_ROOT, relative];
}

/* THE FIXTURE IS BUILT WITH POSIX CALLS on purpose: what is under test is the WALK, and a probe that
 * built its tree through the class under test could not tell a walk bug from a create bug. */
static void fn_make_dir(NSString *path)
{
	if (mkdir([path UTF8String], 0755) != 0) {
		printf("FOUNDATION-DIRECTORYENUMERATOR DIAG mkdir %s failed: %s\n",
		       [path UTF8String], strerror(errno));
	}
}

static void fn_make_file(NSString *path, const char *contents)
{
	int fd = open([path UTF8String], O_WRONLY | O_CREAT | O_TRUNC, 0644);

	if (fd < 0) {
		printf("FOUNDATION-DIRECTORYENUMERATOR DIAG open %s failed: %s\n",
		       [path UTF8String], strerror(errno));
		return;
	}
	if (write(fd, contents, strlen(contents)) != (ssize_t)strlen(contents)) {
		printf("FOUNDATION-DIRECTORYENUMERATOR DIAG write %s failed\n", [path UTF8String]);
	}
	close(fd);
}

static void fn_make_link(NSString *target, NSString *at)
{
	if (symlink([target UTF8String], [at UTF8String]) != 0) {
		printf("FOUNDATION-DIRECTORYENUMERATOR DIAG symlink %s failed: %s\n",
		       [at UTF8String], strerror(errno));
	}
}

/* THE WALK, COLLECTED: every item the enumerator yields, in the order it yielded them. */
static NSArray *fn_walk(NSString *path)
{
	NSMutableArray *items = [NSMutableArray array];
	NSDirectoryEnumerator *walk = [[NSFileManager defaultManager] enumeratorAtPath:path];
	NSString *item;

	while ((item = [walk nextObject]) != nil) {
		[items addObject:item];
	}
	return items;
}

static BOOL fn_same_items(NSArray *left, NSArray *right)
{
	NSSet *a = [NSSet setWithArray:left];
	NSSet *b = [NSSet setWithArray:right];
	NSUInteger i;

	if ([a count] != [b count]) {
		return NO;
	}
	for (i = 0; i < [right count]; i++) {
		if (![a containsObject:[right objectAtIndex:i]]) {
			return NO;
		}
	}
	return YES;
}

int main(void)
{
	NSFileManager *manager = [NSFileManager defaultManager];
	/* THE WHOLE SUBTREE, in the spelling the enumerator must answer with - RELATIVE. `link` is here
	 * and `link/b.txt` is NOT: the walk does not recurse through a symlinked directory. */
	NSArray *expected = @[ @"a.txt", @"dir1", @"dir1/b.txt", @"dir1/sub", @"dir1/sub/c.txt",
			       @"dir2", @"link", @"zlink" ];

	[manager removeItemAtPath:@PROBE_ROOT error:NULL];	/* a previous run's litter, if any */
	fn_make_dir(@PROBE_ROOT);
	fn_make_file(fn_path(@"a.txt"), "aaa");
	fn_make_dir(fn_path(@"dir1"));
	fn_make_file(fn_path(@"dir1/b.txt"), "bb");
	fn_make_dir(fn_path(@"dir1/sub"));
	fn_make_file(fn_path(@"dir1/sub/c.txt"), "c");
	fn_make_dir(fn_path(@"dir2"));
	fn_make_link(@"dir1", fn_path(@"link"));
	fn_make_link(@"a.txt", fn_path(@"zlink"));

	{
		/* ONE WALK, ONE PASS, and every per-item question is collected DURING it because that is the
		 * only moment the answers exist: -fileAttributes and -level are about the current item. */
		NSDirectoryEnumerator *walk = [manager enumeratorAtPath:@PROBE_ROOT];
		NSMutableArray *items = [NSMutableArray array];
		NSMutableArray *wrongLevels = [NSMutableArray array];
		NSMutableArray *notRelative = [NSMutableArray array];
		NSMutableArray *missingOnDisk = [NSMutableArray array];
		NSMutableArray *enteredLinks = [NSMutableArray array];
		NSString *fileType = @"(never saw a.txt)";
		NSString *dirType = @"(never saw dir1)";
		NSString *linkType = @"(never saw link)";
		unsigned long long fileSize = 0;
		id attributesBeforeFirst = [walk fileAttributes];
		NSString *item;

		while ((item = [walk nextObject]) != nil) {
			NSDictionary *attributes = [walk fileAttributes];
			NSUInteger depth = [[item componentsSeparatedByString:@"/"] count] - 1;

			[items addObject:item];
			if ([walk level] != depth + 1) {
				[wrongLevels addObject:[NSString stringWithFormat:@"%@ at level %lu (want %lu)",
					item, (unsigned long)[walk level], (unsigned long)(depth + 1)]];
			}
			if ([item hasPrefix:@"/"] || [item containsString:@"nsdirectoryenumerator-probe"]) {
				[notRelative addObject:item];
			}
			if (![manager fileExistsAtPath:fn_path(item)]) {
				[missingOnDisk addObject:item];
			}
			if ([item hasPrefix:@"link/"]) {
				[enteredLinks addObject:item];
			}
			if ([item isEqualToString:@"a.txt"]) {
				fileType = [attributes objectForKey:NSFileType];
				fileSize = [[attributes objectForKey:NSFileSize] unsignedLongLongValue];
			}
			if ([item isEqualToString:@"dir1"]) {
				dirType = [attributes objectForKey:NSFileType];
			}
			if ([item isEqualToString:@"link"]) {
				linkType = [attributes objectForKey:NSFileType];
			}
		}
		printf("FOUNDATION-DIRECTORYENUMERATOR DIAG walk: %lu item(s): %s\n",
		       (unsigned long)[items count], [[items componentsJoinedByString:@" "] UTF8String]);

		check("walk-yields-the-whole-subtree", fn_same_items(items, expected),
		      [NSString stringWithFormat:@"got %lu item(s): %@",
			(unsigned long)[items count], [items componentsJoinedByString:@" "]]);
		check("walk-paths-are-relative-to-the-directory", [notRelative count] == 0,
		      [NSString stringWithFormat:@"these carry the directory's own path: %@",
			[notRelative componentsJoinedByString:@" "]]);
		check("walk-paths-join-back-to-real-items", [missingOnDisk count] == 0,
		      [NSString stringWithFormat:@"appending these to the directory found nothing: %@",
			[missingOnDisk componentsJoinedByString:@" "]]);
		check("level-counts-the-enumerated-directory-as-zero",
		      [wrongLevels count] == 0 && [items containsObject:@"dir1/sub/c.txt"],
		      [NSString stringWithFormat:@"wrong levels: %@",
			[wrongLevels componentsJoinedByString:@" | "]]);
		check("file-attributes-are-the-most-recent-items",
		      attributesBeforeFirst == nil && [fileType isEqualToString:NSFileTypeRegular] &&
		      fileSize == 3 && [dirType isEqualToString:NSFileTypeDirectory],
		      [NSString stringWithFormat:@"before-first=%@ a.txt type=%@ size=%llu dir1 type=%@",
			attributesBeforeFirst == nil ? @"nil" : @"a dictionary",
			fileType, fileSize, dirType]);
		{
			NSDictionary *started = [walk directoryAttributes];
			NSDictionary *asked = [manager attributesOfItemAtPath:@PROBE_ROOT error:NULL];
			/* `id` LOCALS, because both sides come out of a dictionary and the compiler is right
			 * that they may be nil: -isEqual: takes them and answers NO for a nil, which is the
			 * comparison this check wants anyway. */
			id startedType = [started objectForKey:NSFileType];
			id askedType = [asked objectForKey:NSFileType];
			id startedSize = [started objectForKey:NSFileSize];
			id askedSize = [asked objectForKey:NSFileSize];

			check("directory-attributes-are-the-starting-directorys",
			      started != nil && asked != nil &&
			      startedType != nil && [startedType isEqual:askedType] &&
			      startedSize != nil && [startedSize isEqual:askedSize],
			      [NSString stringWithFormat:@"walk type=%@ size=%@ | manager type=%@ size=%@",
				startedType, startedSize, askedType, askedSize]);
		}
		check("the-walk-does-not-resolve-symbolic-links",
		      [enteredLinks count] == 0 && [linkType isEqualToString:NSFileTypeSymbolicLink],
		      [NSString stringWithFormat:@"link type=%@ entered=%@", linkType,
			[enteredLinks componentsJoinedByString:@" "]]);
		check("post-order-is-not-what-this-slice-builds", ![walk isEnumeratingDirectoryPostOrder],
		      @"this slice's walk is pre-order and its doors pass no options (§60 slice 6)");
	}

	{
		/* APPLE'S OWN WORD FOR THIS CASE, and it is not an error and not nil: "If `path` is a
		 * filename, the method returns an enumerator object that enumerates no files - the first
		 * call to -nextObject will return nil." */
		NSDirectoryEnumerator *spent = [manager enumeratorAtPath:fn_path(@"a.txt")];

		check("a-file-enumerates-nothing",
		      spent != nil && [spent nextObject] == nil && [[spent allObjects] count] == 0,
		      spent == nil ? @"the door answered nil instead of a spent enumerator"
				   : @"-nextObject answered an item for a FILE path");
	}

	{
		NSDirectoryEnumerator *walk = [manager enumeratorAtPath:@PROBE_ROOT];
		NSArray *rest;

		[walk nextObject];	/* spend one, and -allObjects must answer only what is LEFT */
		rest = [walk allObjects];	/* READ ONCE: the cursor is spent by reading it */

		check("all-objects-is-the-rest-of-the-walk", [rest count] == [expected count] - 1,
		      [NSString stringWithFormat:@"%lu of %lu after one -nextObject",
			(unsigned long)[rest count], (unsigned long)([expected count] - 1)]);
	}

	{
		/* THE PRUNING, IN BOTH SPELLINGS, against the ONE subtree it must remove: `dir1` and
		 * everything under it, and nothing else. */
		NSDirectoryEnumerator *old = [manager enumeratorAtPath:@PROBE_ROOT];
		NSDirectoryEnumerator *modern = [manager enumeratorAtPath:@PROBE_ROOT];
		NSMutableArray *oldItems = [NSMutableArray array];
		NSMutableArray *modernItems = [NSMutableArray array];
		NSString *item;
		NSArray *pruned = @[ @"a.txt", @"dir1", @"dir2", @"link", @"zlink" ];

		while ((item = [old nextObject]) != nil) {
			[oldItems addObject:item];
			if ([item isEqualToString:@"dir1"]) {
				[old skipDescendents];
			}
		}
		while ((item = [modern nextObject]) != nil) {
			[modernItems addObject:item];
			if ([item isEqualToString:@"dir1"]) {
				[modern skipDescendants];
			}
		}
		check("skip-descendents-prunes-one-level", fn_same_items(oldItems, pruned),
		      [NSString stringWithFormat:@"got %@", [oldItems componentsJoinedByString:@" "]]);
		check("skip-descendants-is-the-same-method", fn_same_items(modernItems, oldItems),
		      [NSString stringWithFormat:@"modern %@ vs old %@",
			[modernItems componentsJoinedByString:@" "],
			[oldItems componentsJoinedByString:@" "]]);
	}

	{
		NSArray *byWalk = fn_walk(@PROBE_ROOT);
		NSArray *bySubpaths = [manager subpathsAtPath:@PROBE_ROOT];
		NSArray *bySubpathsWithError = [manager subpathsOfDirectoryAtPath:@PROBE_ROOT error:NULL];

		check("subpaths-agree-with-the-enumerator",
		      bySubpaths != nil && bySubpathsWithError != nil &&
		      fn_same_items(bySubpaths, byWalk) && fn_same_items(bySubpathsWithError, byWalk),
		      [NSString stringWithFormat:@"walk %lu, subpaths %lu, subpathsWithError %lu",
			(unsigned long)[byWalk count], (unsigned long)[bySubpaths count],
			(unsigned long)[bySubpathsWithError count]]);

		check("a-symbolic-link-given-as-the-path-is-traversed",
		      fn_same_items(fn_walk(fn_path(@"link")), @[ @"b.txt", @"sub", @"sub/c.txt" ]) &&
		      fn_same_items([manager subpathsAtPath:fn_path(@"link")],
				    @[ @"b.txt", @"sub", @"sub/c.txt" ]),
		      @"a link AS the path is followed by both doors (opendir(2) follows it)");

		check("an-empty-directory-answers-an-empty-array-not-nil",
		      [manager subpathsAtPath:fn_path(@"dir2")] != nil &&
		      [[manager subpathsAtPath:fn_path(@"dir2")] count] == 0,
		      @"an empty directory LISTS fine: 'no items' is not 'not a directory'");
	}

	{
		NSError *fileError = nil;
		NSError *missingError = nil;
		NSArray *forFile = [manager subpathsOfDirectoryAtPath:fn_path(@"a.txt") error:&fileError];
		NSArray *forMissing = [manager subpathsOfDirectoryAtPath:fn_path(@"gone") error:&missingError];

		check("subpaths-refuse-what-is-not-a-directory",
		      forFile == nil && fileError != nil && [fileError code] == ENOTDIR &&
		      forMissing == nil && missingError != nil && [missingError code] == ENOENT,
		      [NSString stringWithFormat:@"file=%s code=%ld, missing=%s code=%ld",
			forFile == nil ? "nil" : "an array", (long)(fileError != nil ? [fileError code] : -1),
			forMissing == nil ? "nil" : "an array",
			(long)(missingError != nil ? [missingError code] : -1)]);
	}

	{
		NSError *cleanupError = nil;
		BOOL removed = [manager removeItemAtPath:@PROBE_ROOT error:&cleanupError];

		check("probe-tree-removed",
		      removed && ![manager fileExistsAtPath:@PROBE_ROOT],
		      cleanupError != nil ? [cleanupError localizedDescription] : @"the tree is still there");
	}

	printf("FOUNDATION-DIRECTORYENUMERATOR RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-DIRECTORYENUMERATOR DONE\n");
	return failc == 0 ? 0 : 1;
}
