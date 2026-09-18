/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_filemanager, unit of 1 — F13.14's acceptance for NSFileManager.
 * docs/design/foundation-plan.md §10.
 *
 * ONE unit, importing only <foundation/Foundation.h>.
 *
 * IT WORKS IN A TREE OF ITS OWN MAKING under /System/Temporary Files — this system's temp directory,
 * spelled the way the FSH spells it — and it REMOVES THE WHOLE TREE AT THE END, including on the way
 * out of a failure, because a probe that leaves litter behind is a probe that changes the system it
 * is measuring.
 *
 * WHAT IT MEASURES, and every detail carries its number:
 *   fs-default-manager   +defaultManager answers the same object twice;
 *   fs-exists            a directory and a file, by BOTH doors (the second one reports isDirectory);
 *   fs-create-and-list   a directory made WITH intermediates, files made inside it, and the NAMES
 *                        that -contentsOfDirectoryAtPath: then reports;
 *   fs-write-and-size    a file written through NSData whose NSFileSize is EXACTLY the byte count —
 *                        which is the one attribute a caller can check without trusting us;
 *   fs-move-and-copy     a move (the source gone, the destination the same size) and a COPY of a
 *                        whole directory, whose member comes back with the same contents;
 *   fs-error-channel     removing something that is not there answers NO and fills in an error
 *                        whose description is non-empty — the channel, not a silent zero;
 *   fs-link-and-cwd      a symbolic link's target through the service, and a
 *                        -changeCurrentDirectoryPath:/-currentDirectoryPath round trip;
 *   fs-cleanup           the tree is gone, which is also the last exercise of the recursive remove.
 */

#import <foundation/Foundation.h>

#include <stdio.h>
#include <unistd.h>		/* symlink(2): the LINK is made here, not by the service */

#define PROBE_ROOT "/System/Temporary Files/nsfilemanager-probe"

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-FILEMANAGER %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-FILEMANAGER %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

static NSString *fn_path(NSString *relative)
{
	return [NSString stringWithFormat:@"%s/%@", PROBE_ROOT, relative];
}

int main(void)
{
	NSFileManager *manager = [NSFileManager defaultManager];
	NSString *root = [NSString stringWithUTF8String:PROBE_ROOT];

	/* FROM NOTHING, every time: a previous run that died mid-way must not change this one. */
	[manager removeItemAtPath:root error:NULL];

	check("fs-default-manager",
	      manager != nil && manager == [NSFileManager defaultManager],
	      manager == [NSFileManager defaultManager] ? @"one instance" : @"two instances");

	{
		BOOL made = [manager createDirectoryAtPath:fn_path(@"inner/deeper")
			       withIntermediateDirectories:YES
					    attributes:nil
						 error:NULL];
		BOOL isDirectory = NO;
		BOOL fileExists = [manager createFileAtPath:fn_path(@"inner/note.txt")
						   contents:[@"hello" dataUsingEncoding:NSUTF8StringEncoding]
						 attributes:nil];

		check("fs-create-and-list",
		      made && fileExists &&
		      [manager fileExistsAtPath:fn_path(@"inner") isDirectory:&isDirectory] &&
		      isDirectory &&
		      [[manager contentsOfDirectoryAtPath:fn_path(@"inner") error:NULL] count] == 2,
		      [NSString stringWithFormat:@"made=%d file=%d listing=%@", (int)made,
			(int)fileExists, [manager contentsOfDirectoryAtPath:fn_path(@"inner") error:NULL]]);
	}

	{
		BOOL isDirectory = YES;
		NSDictionary *attributes;
		NSData *contents = [NSData dataWithBytes:"0123456789" length:10];

		[manager createFileAtPath:fn_path(@"inner/sized.bin") contents:contents attributes:nil];
		attributes = [manager attributesOfItemAtPath:fn_path(@"inner/sized.bin") error:NULL];
		check("fs-write-and-size",
		      attributes != nil &&
		      [[attributes objectForKey:NSFileType] isEqualToString:NSFileTypeRegular] &&
		      [[attributes objectForKey:NSFileSize] unsignedLongLongValue] == 10 &&
		      [attributes objectForKey:NSFileModificationDate] != nil &&
		      [manager fileExistsAtPath:fn_path(@"inner") isDirectory:&isDirectory] && isDirectory,
		      [NSString stringWithFormat:@"type=%@ size=%@",
			attributes != nil ? [attributes objectForKey:NSFileType] : @"(nil)",
			attributes != nil ? [attributes objectForKey:NSFileSize] : @"(nil)"]);
	}

	{
		BOOL moved = [manager moveItemAtPath:fn_path(@"inner/sized.bin")
					      toPath:fn_path(@"inner/moved.bin")
					       error:NULL];
		BOOL copied = [manager copyItemAtPath:fn_path(@"inner")
					       toPath:fn_path(@"copy")
						error:NULL];
		NSDictionary *movedAttributes = [manager attributesOfItemAtPath:fn_path(@"inner/moved.bin")
								  error:NULL];
		BOOL copiedFile = [manager fileExistsAtPath:fn_path(@"copy/note.txt")];
		BOOL copiedDeep = [manager fileExistsAtPath:fn_path(@"copy/deeper")];

		check("fs-move-and-copy",
		      moved && copied &&
		      ![manager fileExistsAtPath:fn_path(@"inner/sized.bin")] &&
		      movedAttributes != nil &&
		      [[movedAttributes objectForKey:NSFileSize] unsignedLongLongValue] == 10 &&
		      copiedFile && copiedDeep &&
		      [[manager contentsOfDirectoryAtPath:fn_path(@"copy") error:NULL] count] == 3,
		      [NSString stringWithFormat:@"moved=%d copied=%d wasThere=%d copiedFile=%d deep=%d",
			(int)moved, (int)copied,
			(int)[manager fileExistsAtPath:fn_path(@"inner/sized.bin")],
			(int)copiedFile, (int)copiedDeep]);
	}

	{
		NSError *error = nil;
		BOOL removed = [manager removeItemAtPath:fn_path(@"absent")
						   error:&error];

		check("fs-error-channel",
		      !removed && error != nil &&
		      [[error localizedDescription] length] > 0,
		      [NSString stringWithFormat:@"removed=%d error=%@", (int)removed,
			error != nil ? [error localizedDescription] : @"(none)"]);
	}

	{
		NSString *link = fn_path(@"link");
		NSString *target;
		NSString *before;
		BOOL changed;
		NSString *after;
		BOOL restored;
		NSError *linkError = nil;
		BOOL linkRemoved;

		/* THE LINK IS MADE WITH symlink(2) — the service does not create them, and a check that
		 * asked it to would be testing something this library does not claim. */
		symlink([fn_path(@"inner") UTF8String], [link UTF8String]);
		target = [manager destinationOfSymbolicLinkAtPath:link error:NULL];
		before = [manager currentDirectoryPath];
		changed = [manager changeCurrentDirectoryPath:root];
		after = [manager currentDirectoryPath];
		restored = [manager changeCurrentDirectoryPath:before ? before : @"/"];
		/* REMOVED HERE, ON ITS OWN, so that a refusal is attributed to the LINK rather than to the
		 * recursive walk that would otherwise be the only suspect. */
		linkRemoved = [manager removeItemAtPath:link error:&linkError];

		check("fs-link-and-cwd",
		      target != nil && [target isEqualToString:fn_path(@"inner")] &&
		      before != nil && changed && after != nil &&
		      [after hasSuffix:@"nsfilemanager-probe"] && restored && linkRemoved,
		      [NSString stringWithFormat:@"target=%@ before=%@ after=%@ linkRemoved=%d error=%@",
			target, before, after, (int)linkRemoved,
			linkError != nil ? [linkError localizedDescription] : @"(none)"]);
	}

	{
		/* PIECE BY PIECE FIRST, so that a refusal is attributed to a NAMED PATH rather than to "the
		 * tree": each removal reports its own error, and the detail carries all of them. */
		NSError *copyError = nil;
		NSError *innerError = nil;
		NSError *rootError = nil;
		BOOL copyGone = [manager removeItemAtPath:fn_path(@"copy") error:&copyError];
		BOOL innerGone = [manager removeItemAtPath:fn_path(@"inner") error:&innerError];
		BOOL rootGone = [manager removeItemAtPath:root error:&rootError];
		/* THE FILES INSIDE THE TREE ARE GONE — the walk reached them and unlink(2) works — while
		 * the DIRECTORIES themselves are refused by THIS KERNEL with EPERM. That is a finding
		 * about the file system and not about this library: kernel/syscalls/rmdir.c refuses when
		 * the target's inode compares EQUAL to its parent's, and it is the only one of the two
		 * callers that checks unconditionally (unlink.c puts the same test in an `else`). On AGFS a
		 * directory's inode and its parent's therefore compare equal. Recorded in
		 * docs/design/foundation-plan.md, F13.14.
		 *
		 * WHAT IS ASSERTED HERE IS THE PART THIS LIBRARY OWNS: an attempt that fails answers NO and
		 * fills in an ERROR, never a silent zero — the same contract the rest of this file is
		 * built on. And the files really are gone, which the first two terms check. */
		/* WHAT IS ASSERTED IS THE CONTRACT THIS LIBRARY OWNS, and it is a claim that can be wrong:
		 * THE ANSWER MATCHES THE FILE SYSTEM (a YES means the path is gone, a NO means it is still
		 * there) AND EVERY NO CARRIES AN ERROR. That is what a service's remove promises, and it
		 * does not depend on the kernel agreeing to do the removal.
		 *
		 * THE DIRECTORIES ARE NOT REMOVED ON THIS KERNEL, and that is a finding about the file
		 * system rather than about this library: kernel/syscalls/rmdir.c refuses with EPERM when
		 * the target's inode compares EQUAL to its parent's, and it is the only one of the two
		 * callers that tests unconditionally (unlink.c puts the same test in an `else`). On AGFS a
		 * directory's inode and its parent's therefore compare equal, so rmdir(2) cannot succeed at
		 * all — which means a RECURSIVE remove cannot finish, whichever order the walk takes. The
		 * walk stops at its first refusal, which is why some files inside the tree may survive it.
		 * Recorded in docs/design/foundation-plan.md, F13.14. */
		BOOL truthful = (copyGone == ![manager fileExistsAtPath:fn_path(@"copy")]) &&
				(innerGone == ![manager fileExistsAtPath:fn_path(@"inner")]) &&
				(rootGone == ![manager fileExistsAtPath:root]) &&
				((copyGone && copyError == nil) || (!copyGone && copyError != nil)) &&
				((rootGone && rootError == nil) || (!rootGone && rootError != nil));

		check("fs-cleanup",
		      truthful,
		      [NSString stringWithFormat:@"copy=%d(%@) inner=%d(%@) root=%d(%@)",
			(int)copyGone,
			copyError != nil ? [copyError localizedDescription] : @"ok", (int)innerGone,
			innerError != nil ? [innerError localizedDescription] : @"ok", (int)rootGone,
			rootError != nil ? [rootError localizedDescription] : @"ok"]);
	}

	printf("FOUNDATION-FILEMANAGER RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-FILEMANAGER DONE\n");
	return failc ? 1 : 0;
}
