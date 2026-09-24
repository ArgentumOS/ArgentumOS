/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_filemanager, unit of 1 — F13.14's acceptance for NSFileManager.
 * docs/design/foundation-plan.md §10.
 *
 * ONE unit, importing only <Foundation/Foundation.h>.
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

#import <Foundation/Foundation.h>

#include <stdio.h>
#include <unistd.h>		/* symlink(2): the LINK is made here, not by the service */
#include <sys/stat.h>		/* stat(2): the inode numbers of a directory and its parent (F13.22) */
#include <fcntl.h>		/* AT_FDCWD / AT_REMOVEDIR: the OTHER door onto rmdir (F13.22) */
#include <errno.h>
#include <string.h>		/* memset/strncpy: the bound socket's address (§60 slice 3c) */
#include <sys/socket.h>		/* ... and socket(2)/bind(2), which make an S_IFSOCK inode */
#include <sys/un.h>

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

/* W8 SLICE 3 WORKS IN A TREE OF ITS OWN, in the same temp directory but BESIDE the one above: the
 * cleanup legs assert things about PROBE_ROOT's own pieces (including this kernel's rmdir behaviour),
 * and a fixture added inside it would end up measuring that assertion as much as the slice. */
#define S3_ROOT "/System/Temporary Files/nsfilemanager-probe-s3"

static NSString *fn_s3(NSString *relative)
{
	return [NSString stringWithFormat:@"%s/%@", S3_ROOT, relative];
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
		 * the DIRECTORIES themselves are refused by THIS KERNEL. That is a finding about the file
		 * system and not about this library: kernel/syscalls/rmdir.c refuses with EPERM when the
		 * target's inode compares EQUAL to its parent's, and it is the only one of the two callers
		 * that checks unconditionally (unlink.c puts the same test in an `else`). Recorded in
		 * docs/design/foundation-plan.md, F13.14 and F13.22.
		 *
		 * WHAT IS ASSERTED HERE IS THE PART THIS LIBRARY OWNS: an attempt that fails answers NO and
		 * fills in an ERROR, never a silent zero — the same contract the rest of this file is
		 * built on. And the files really are gone, which the first two terms check.
		 *
		 * THE DETAIL ALSO CARRIES THE DISCRIMINATOR (F13.22), because it is the only instrument
		 * userland has for the question "which EPERM is it?": an EMPTY directory is refused with
		 * EPERM, and so is a NON-EMPTY one — but a file system that answers for itself must say
		 * "not empty" for the second, never a permission refusal. TWO EPERMS SEPARATE NOTHING; the
		 * PAIR separates "rmdir(2)'s own guard fired" from "the file system answered". */
		BOOL truthful = (copyGone == ![manager fileExistsAtPath:fn_path(@"copy")]) &&
				(innerGone == ![manager fileExistsAtPath:fn_path(@"inner")]) &&
				(rootGone == ![manager fileExistsAtPath:root]) &&
				((copyGone && copyError == nil) || (!copyGone && copyError != nil)) &&
				((rootGone && rootError == nil) || (!rootGone && rootError != nil));

		{
			NSString *emptyDir = fn_path(@"empty");
			NSString *fullDir = fn_path(@"full");
			NSError *emptyError = nil;
			NSError *fullError = nil;
			BOOL emptyGone, fullGone;
			NSString *emptyWhy, *fullWhy;

			/* THE TREE IS RE-CREATED BEFORE THESE MEASUREMENTS, and that is not tidiness: the
			 * recursive remove above NOW SUCCEEDS (F13.23), so `root` and everything in it is gone
			 * by this point and every path below would answer "No such file or directory" —
			 * measurements describing a deletion instead of a refusal. These four blocks are about
			 * what the kernel and the service DO, so they need something to do it to. */
			[manager createDirectoryAtPath:root
				       withIntermediateDirectories:YES
						    attributes:nil
							 error:NULL];

			[manager createDirectoryAtPath:emptyDir
				       withIntermediateDirectories:NO
						    attributes:nil
							 error:NULL];
			[manager createDirectoryAtPath:fullDir
				       withIntermediateDirectories:NO
						    attributes:nil
							 error:NULL];
			[manager createFileAtPath:[fullDir stringByAppendingPathComponent:@"occupant"]
					 contents:nil attributes:nil];

			/* THE KERNEL'S OWN ANSWER FOR A NON-EMPTY DIRECTORY, TAKEN DIRECTLY (F13.23): the
			 * service recurses, so it can never show this, and it is the specific property F13.14
			 * recorded as impossible. rmdir(2) must refuse with ENOTEMPTY and the directory must
			 * SURVIVE. */
			{
				NSString *occupied = fn_path(@"occupied");
				int notEmptyErr = 0;
				BOOL survived;

				[manager createDirectoryAtPath:occupied
					       withIntermediateDirectories:NO
							    attributes:nil
								 error:NULL];
				[manager createFileAtPath:[occupied stringByAppendingPathComponent:@"occupant"]
						 contents:nil attributes:nil];
				errno = 0;
				if(rmdir([occupied UTF8String]) != 0) {
					notEmptyErr = errno;
				}
				survived = [manager fileExistsAtPath:occupied];
				printf("FOUNDATION-FILEMANAGER fs-rmdir-nonempty-enotempty: errno=%d survived=%d\n",
				       notEmptyErr, (int)survived);
				truthful = truthful && notEmptyErr == ENOTEMPTY && survived;
			}

			emptyGone = [manager removeItemAtPath:emptyDir error:&emptyError];
			fullGone = [manager removeItemAtPath:fullDir error:&fullError];
			emptyWhy = emptyError != nil ? [emptyError localizedDescription] : @"no-error";
			fullWhy = fullError != nil ? [fullError localizedDescription] : @"no-error";
			/* THE SERVICE REMOVES BOTH, RECURSIVELY — that is what -removeItemAtPath: IS — and
			 * neither attempt may carry an error. `!fullGone` stood here before, and it was true
			 * ONLY while directories could not be removed at all: a check inverted by a bug is
			 * still a bug, and it failed the moment the bug was fixed. */
			truthful = truthful && emptyGone && fullGone && emptyError == nil &&
					fullError == nil;

			/* PRINTED UNCONDITIONALLY, because a check's detail is only shown when it FAILS and
			 * this pair is the measurement rather than a verdict (F13.22). */
			printf("FOUNDATION-FILEMANAGER fs-rmdir-discriminator: EMPTY removed=%d '%s' | "
			       "NON-EMPTY removed=%d '%s'\n", (int)emptyGone, [emptyWhy UTF8String],
			       (int)fullGone, [fullWhy UTF8String]);

			/* AND THE SECOND MEASUREMENT: does the FILE SYSTEM ITSELF think a directory has its
			 * parent's inode number? `empty/..` resolves to the parent, so the comparison needs no
			 * knowledge of the parent's name. If these are EQUAL then the aliasing is in the
			 * file system and `rmdir` is innocent; if they DIFFER then the file system is right
			 * and the alias is being created inside namei's own pointers. Two different bugs, and
			 * nothing else in userland tells them apart. */
			{
				struct stat st_child, st_parent;

				if(stat([emptyDir UTF8String], &st_child) == 0 &&
				   stat([[emptyDir stringByAppendingPathComponent:@".."] UTF8String],
					&st_parent) == 0) {
					printf("FOUNDATION-FILEMANAGER fs-ino: dir=%llu parent=%llu %s\n",
					       (unsigned long long)st_child.st_ino,
					       (unsigned long long)st_parent.st_ino,
					       st_child.st_ino == st_parent.st_ino ? "ALIASED" : "distinct");
				} else {
					printf("FOUNDATION-FILEMANAGER fs-ino: stat failed\n");
				}
			}

			/* AND THE THIRD MEASUREMENT (F13.22), which is the experiment rather than a probe of
			 * one: a RELATIVE, SINGLE-COMPONENT path. `PROBE_ROOT/rel` walks several components
			 * and `do_namei`'s handoff between them runs at least twice; a bare `rel` from inside
			 * PROBE_ROOT is ONE component and crosses that boundary once. If the alias comes from
			 * the handoff, the one-component path should SUCCEED where the multi-component one
			 * fails — and nothing else in userland tells those two apart. */
			{
				char cwd[1024];
				NSError *relError = nil;
				BOOL relGone = NO;
				BOOL stayed = NO;

				[manager createDirectoryAtPath:fn_path(@"rel")
					       withIntermediateDirectories:NO
							    attributes:nil
								 error:NULL];
				if(getcwd(cwd, sizeof(cwd)) != NULL && chdir(PROBE_ROOT) == 0) {
					relGone = [manager removeItemAtPath:@"rel" error:&relError];
					stayed = (chdir(cwd) == 0);
				}
				printf("FOUNDATION-FILEMANAGER fs-relative: removed=%d '%s' (cwd-restored=%d)\n",
				       (int)relGone,
				       relError != nil ? [[relError localizedDescription] UTF8String]
						       : "no-error", (int)stayed);
			}

			check("fs-cleanup",
			      truthful,
			      [NSString stringWithFormat:@"copy=%d(%@) inner=%d(%@) root=%d(%@) | EMPTY dir: removed=%d '%@' | NON-EMPTY dir: removed=%d '%@'",
				(int)copyGone,
				copyError != nil ? [copyError localizedDescription] : @"ok", (int)innerGone,
				innerError != nil ? [innerError localizedDescription] : @"ok", (int)rootGone,
				rootError != nil ? [rootError localizedDescription] : @"ok",
				(int)emptyGone, emptyWhy, (int)fullGone, fullWhy]);
		}
	}

	{
		/* THE FOURTH MEASUREMENT (F13.22): THE *OTHER* CALLER OF THE SAME BLOCK. `rm -rf` removes
		 * directories through unlinkat(AT_REMOVEDIR), and unlink.c runs the SAME rmdir checks in
		 * its own copy — `i == dir` there is an `else if`. If THAT succeeds while rmdir(2) fails,
		 * the difference is inside sys_rmdir; if it fails the same way, the two callers agree and
		 * the alias is being seen by the block they share. Called directly, because this is the
		 * only place in userland where the two doors can be compared. */
		NSString *viaDir = fn_path(@"viadir");
		int rc;

		[manager createDirectoryAtPath:viaDir
			       withIntermediateDirectories:NO
					    attributes:nil
						 error:NULL];
		errno = 0;
		rc = unlinkat(AT_FDCWD, [viaDir UTF8String], AT_REMOVEDIR);
		printf("FOUNDATION-FILEMANAGER fs-unlinkat-removedir: rc=%d errno=%d (%s) still-there=%d\n",
		       rc, errno, rc == 0 ? "removed" : "refused",
		       (int)[manager fileExistsAtPath:viaDir]);
	}

	{
		/* THE FIFTH MEASUREMENT (F13.22), AND THE ONE THAT SHOULD HAVE COME FIRST: the SYSCALLS
		 * THEMSELVES, called directly rather than through the service. Every measurement before
		 * this one went through NSFileManager, and if the service is what refuses — not the
		 * kernel — then the whole chain above has been describing the LIBRARY's behaviour and
		 * calling it the file system's. `rmdir(2)` and `unlink(2)` on a directory side by side is
		 * the comparison that says which. */
		NSString *syscallDir = fn_path(@"scdir");
		int rmdirRc, unlinkRc, rmdirErr, unlinkErr;

		[manager createDirectoryAtPath:syscallDir
			       withIntermediateDirectories:NO
					    attributes:nil
						 error:NULL];
		errno = 0;
		rmdirRc = rmdir([syscallDir UTF8String]);
		rmdirErr = errno;
		errno = 0;
		unlinkRc = unlink([syscallDir UTF8String]);
		unlinkErr = errno;
		printf("FOUNDATION-FILEMANAGER fs-syscalls: rmdir(2) rc=%d errno=%d | unlink(2) rc=%d errno=%d | still-there=%d\n",
		       rmdirRc, rmdirErr, unlinkRc, unlinkErr,
		       (int)[manager fileExistsAtPath:syscallDir]);
	}

	/* --- W8 SLICE 3: THE FILE'S BYTES, THE EQUALITY RULE, THE TWO REFUSALS, AND THE OTHER LINK ------ */
	{
		/* EVERY QUESTION BELOW IS APPLE'S CONTRACT rather than a taste, and TWO OF THEM ARE FIXES: the
		 * copy and the move used to REPLACE an existing destination silently - O_CREAT|O_TRUNC for the
		 * copy, and rename(2) for the move, which does it by design. So the check that matters most
		 * here is not that the call answers NO but that THE FILE THAT WAS ALREADY THERE IS STILL
		 * THERE, BYTE FOR BYTE: a refusal that destroyed the data anyway would pass any check that
		 * only read the return value.
		 */
		NSFileManager *fm = [NSFileManager defaultManager];
		NSData *hello = [NSData dataWithBytes:"hello" length:5];
		NSData *original = [NSData dataWithBytes:"original" length:8];
		NSError *copyOntoError = nil;
		NSError *moveOntoError = nil;
		NSError *doorError = nil;
		NSData *bytes;
		NSData *survivor;
		NSString *linkTarget;
		BOOL copiedOnto;
		BOOL movedOnto;
		BOOL linkMade;
		BOOL cleaned;

		[fm removeItemAtPath:@S3_ROOT error:NULL];	/* a previous run's litter, if any */
		[fm createDirectoryAtPath:fn_s3(@"tree/sub")
	      withIntermediateDirectories:YES
			   attributes:nil
				error:NULL];
		[fm createFileAtPath:fn_s3(@"file.txt") contents:hello attributes:nil];
		[fm createFileAtPath:fn_s3(@"tree/a.txt")
			     contents:[@"a" dataUsingEncoding:NSUTF8StringEncoding]
			   attributes:nil];
		[fm createFileAtPath:fn_s3(@"tree/sub/b.txt")
			     contents:[@"bb" dataUsingEncoding:NSUTF8StringEncoding]
			   attributes:nil];
		/* TWO SPECIAL ITEMS AND ONE SIBLING, for slice 3c: the FIFO has NO Apple type value and the
		 * SOCKET HAS ONE (so the pair shows both halves of the vocabulary rule), and `treebc` exists
		 * only so that the relationship rule can be asked about a name that PREFIXES another. */
		mkfifo([fn_s3(@"pipe") UTF8String], 0644);
		{
			int fd = socket(AF_UNIX, SOCK_STREAM, 0);

			if (fd >= 0) {
				struct sockaddr_un address;

				memset(&address, 0, sizeof(address));
				address.sun_family = AF_UNIX;
				strncpy(address.sun_path, [fn_s3(@"sock") UTF8String],
					sizeof(address.sun_path) - 1);
				if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
					printf("FOUNDATION-FILEMANAGER fs-s3 DIAG bind: %s\n", strerror(errno));
				}
				/* THE INODE OUTLIVES THE DESCRIPTOR: a bound socket is a NAME on the file system,
				 * and closing the fd does not unlink it - which is what makes it visible to
				 * -attributesOfItemAtPath: at all. */
				close(fd);
			}
		}
		[fm createDirectoryAtPath:fn_s3(@"treebc") withIntermediateDirectories:NO attributes:nil error:NULL];

		/* THE BYTES, AND THE ONE EXCLUSION APPLE NAMES: a DIRECTORY answers nil. A path that is not
		 * there answers nil too, which is this door's whole error channel - it has none, in Cocoa
		 * either. */
		bytes = [fm contentsAtPath:fn_s3(@"file.txt")];
		check("fs-contents-at-path",
		      bytes != nil && [bytes isEqualToData:hello] &&
		      [fm contentsAtPath:fn_s3(@"tree")] == nil &&
		      [fm contentsAtPath:fn_s3(@"not-here.txt")] == nil,
		      [NSString stringWithFormat:@"file=%lu bytes, directory=%s, missing=%s",
			(unsigned long)(bytes != nil ? [bytes length] : 0),
			[fm contentsAtPath:fn_s3(@"tree")] == nil ? "nil" : "not nil",
			[fm contentsAtPath:fn_s3(@"not-here.txt")] == nil ? "nil" : "not nil"]);

		/* THE EQUALITY RULE'S SIX ANSWERS. Two links are built to ONE target, because "compares the
		 * links themselves" is only observable when the targets agree; and the differing tree differs
		 * in a SUBDIRECTORY, which is the half of Apple's sentence a shallow implementation misses
		 * ("contents of subdirectories are also compared"). */
		[fm createSymbolicLinkAtPath:fn_s3(@"link-one")
		     withDestinationPath:fn_s3(@"file.txt")
				   error:NULL];
		[fm createSymbolicLinkAtPath:fn_s3(@"link-two")
		     withDestinationPath:fn_s3(@"file.txt")
				   error:NULL];
		[fm copyItemAtPath:fn_s3(@"tree") toPath:fn_s3(@"tree-same") error:NULL];
		[fm copyItemAtPath:fn_s3(@"tree") toPath:fn_s3(@"tree-diff") error:NULL];
		[fm createFileAtPath:fn_s3(@"tree-diff/sub/b.txt")
			     contents:[@"XX" dataUsingEncoding:NSUTF8StringEncoding]
			   attributes:nil];
		check("fs-contents-equal",
		      [fm contentsEqualAtPath:fn_s3(@"file.txt") andPath:fn_s3(@"file.txt")] &&
		      [fm contentsEqualAtPath:fn_s3(@"tree") andPath:fn_s3(@"tree-same")] &&
		      ![fm contentsEqualAtPath:fn_s3(@"tree") andPath:fn_s3(@"tree-diff")] &&
		      ![fm contentsEqualAtPath:fn_s3(@"tree/a.txt") andPath:fn_s3(@"tree/sub/b.txt")] &&
		      [fm contentsEqualAtPath:fn_s3(@"link-one") andPath:fn_s3(@"link-two")] &&
		      ![fm contentsEqualAtPath:fn_s3(@"link-one") andPath:fn_s3(@"file.txt")],
		      [NSString stringWithFormat:@"same=%d trees=%d differing-tree=%d different-bytes=%d "
			"two-links=%d link-vs-target=%d",
			(int)[fm contentsEqualAtPath:fn_s3(@"file.txt") andPath:fn_s3(@"file.txt")],
			(int)[fm contentsEqualAtPath:fn_s3(@"tree") andPath:fn_s3(@"tree-same")],
			(int)[fm contentsEqualAtPath:fn_s3(@"tree") andPath:fn_s3(@"tree-diff")],
			(int)[fm contentsEqualAtPath:fn_s3(@"tree/a.txt") andPath:fn_s3(@"tree/sub/b.txt")],
			(int)[fm contentsEqualAtPath:fn_s3(@"link-one") andPath:fn_s3(@"link-two")],
			(int)[fm contentsEqualAtPath:fn_s3(@"link-one") andPath:fn_s3(@"file.txt")]]);

		/* THE OTHER KIND OF LINK, MADE TO SOMETHING THAT DOES NOT EXIST - which is the point of
		 * Apple's own sentence about this door, and the thing a "check the target first"
		 * implementation could not do. */
		linkMade = [fm createSymbolicLinkAtPath:fn_s3(@"dangling")
			    withDestinationPath:fn_s3(@"nowhere-at-all")
					  error:&doorError];
		linkTarget = [fm destinationOfSymbolicLinkAtPath:fn_s3(@"dangling") error:NULL];
		check("fs-symbolic-link-door",
		      linkMade && linkTarget != nil &&
		      [linkTarget isEqualToString:fn_s3(@"nowhere-at-all")] &&
		      ![fm fileExistsAtPath:fn_s3(@"nowhere-at-all")],
		      [NSString stringWithFormat:@"made=%d target=%@ exists=%d error=%@", (int)linkMade,
			linkTarget, (int)[fm fileExistsAtPath:fn_s3(@"nowhere-at-all")],
			doorError != nil ? [doorError localizedDescription] : @"(none)"]);

		/* THE TWO REFUSALS, AND THE BYTES THAT MUST SURVIVE THEM. */
		[fm createFileAtPath:fn_s3(@"exists.txt") contents:original attributes:nil];
		[fm createFileAtPath:fn_s3(@"other.txt")
			     contents:[@"replacement" dataUsingEncoding:NSUTF8StringEncoding]
			   attributes:nil];
		copiedOnto = [fm copyItemAtPath:fn_s3(@"other.txt")
					 toPath:fn_s3(@"exists.txt")
					  error:&copyOntoError];
		survivor = [fm contentsAtPath:fn_s3(@"exists.txt")];
		check("fs-copy-refuses-an-existing-destination",
		      !copiedOnto && copyOntoError != nil && [copyOntoError code] == EEXIST &&
		      survivor != nil && [survivor isEqualToData:original] &&
		      [fm fileExistsAtPath:fn_s3(@"other.txt")],
		      [NSString stringWithFormat:@"copied=%d code=%ld survivor=%@ source-still-there=%d",
			(int)copiedOnto, (long)(copyOntoError != nil ? [copyOntoError code] : -1),
			survivor != nil ? [survivor description] : @"(nil)",
			(int)[fm fileExistsAtPath:fn_s3(@"other.txt")]]);

		movedOnto = [fm moveItemAtPath:fn_s3(@"other.txt")
					toPath:fn_s3(@"exists.txt")
					 error:&moveOntoError];
		survivor = [fm contentsAtPath:fn_s3(@"exists.txt")];
		check("fs-move-refuses-an-existing-destination",
		      !movedOnto && moveOntoError != nil && [moveOntoError code] == EEXIST &&
		      [fm fileExistsAtPath:fn_s3(@"other.txt")] &&
		      survivor != nil && [survivor isEqualToData:original],
		      [NSString stringWithFormat:@"moved=%d code=%ld source-still-there=%d survivor=%@",
			(int)movedOnto, (long)(moveOntoError != nil ? [moveOntoError code] : -1),
			(int)[fm fileExistsAtPath:fn_s3(@"other.txt")],
			survivor != nil ? [survivor description] : @"(nil)"]);

		/* A SYMLINK IS COPIED AS A LINK, AND THE DANGLING ONE IS THE PROOF: a "copy" that followed
		 * the link would have nothing to read at all, so the dangling case is the sharpest form of the
		 * question. Apple's copy pages name this only for a copy's DESTINATION, so what is asserted
		 * here is the reading §60 records - the same family treats links as items ("does not traverse
		 * symbolic links, but compares the links themselves"), and a copy that followed one would make
		 * a copy of the link indistinguishable from a copy of its target. */
		[fm copyItemAtPath:fn_s3(@"link-one") toPath:fn_s3(@"link-copy") error:NULL];
		[fm copyItemAtPath:fn_s3(@"dangling") toPath:fn_s3(@"dangling-copy") error:NULL];
		check("fs-copy-of-a-symlink-is-a-link",
		      [[fm destinationOfSymbolicLinkAtPath:fn_s3(@"link-copy") error:NULL]
			isEqualToString:fn_s3(@"file.txt")] &&
		      [[fm destinationOfSymbolicLinkAtPath:fn_s3(@"dangling-copy") error:NULL]
			isEqualToString:fn_s3(@"nowhere-at-all")] &&
		      [fm contentsEqualAtPath:fn_s3(@"link-one") andPath:fn_s3(@"link-copy")] &&
		      [[fm contentsAtPath:fn_s3(@"link-copy")] isEqualToData:hello],
		      [NSString stringWithFormat:@"link-copy->%@ dangling-copy->%@ equal=%d bytes=%@",
			[fm destinationOfSymbolicLinkAtPath:fn_s3(@"link-copy") error:NULL],
			[fm destinationOfSymbolicLinkAtPath:fn_s3(@"dangling-copy") error:NULL],
			(int)[fm contentsEqualAtPath:fn_s3(@"link-one") andPath:fn_s3(@"link-copy")],
			[fm contentsAtPath:fn_s3(@"link-copy")] != nil ?
				[[fm contentsAtPath:fn_s3(@"link-copy")] description] : @"(nil)"]);

		/* ---- W8 SLICE 3c: THE FILE SYSTEM'S OWN NUMBERS, THE INODE KEYS, THE MISSING TYPE WORDS AND
		 * THE RELATIONSHIP RULE - and Apple states both of this doors' traps itself, so neither is a
		 * judgement call: THE SIZES ARE BYTES ("the size of the file system in bytes") and the
		 * FILE-SYSTEM NUMBER is `st_dev` ("the value corresponds to the value of st_dev, as returned by
		 * stat(2)"), which is NOT the statfs(2) field a reader reaches for first. */
		{
			id fsAttributes = [fm attributesOfFileSystemForPath:fn_s3(@"file.txt") error:NULL];
			id itemAttributes = [fm attributesOfItemAtPath:fn_s3(@"file.txt") error:NULL];
			struct stat st;
			unsigned long long size = [[fsAttributes objectForKey:NSFileSystemSize] unsignedLongLongValue];
			unsigned long long freeSize = [[fsAttributes objectForKey:NSFileSystemFreeSize] unsignedLongLongValue];
			unsigned long long nodes = [[fsAttributes objectForKey:NSFileSystemNodes] unsignedLongLongValue];
			BOOL statOK = (lstat([fn_s3(@"file.txt") UTF8String], &st) == 0);

			check("fs-attributes-of-file-system",
			      fsAttributes != nil && size > 0 && freeSize > 0 && freeSize <= size && nodes > 0 &&
			      statOK &&
			      [[fsAttributes objectForKey:NSFileSystemNumber] unsignedLongLongValue] ==
				(unsigned long long)st.st_dev,
			      [NSString stringWithFormat:@"size=%llu free=%llu nodes=%llu number=%llu st_dev=%llu",
				size, freeSize, nodes,
				[[fsAttributes objectForKey:NSFileSystemNumber] unsignedLongLongValue],
				statOK ? (unsigned long long)st.st_dev : 0ULL]);

			/* THE THREE KEYS THAT ARE A stat(2) FIELD BY APPLE'S OWN NAMING, plus the key that is
			 * PUBLISHED AND NEVER FILLED: this substrate keeps no birth time, and an absent entry is
			 * how a file system says it has no such attribute. */
			check("fs-item-attributes-name-the-inode",
			      itemAttributes != nil && statOK &&
			      [[itemAttributes objectForKey:NSFileSystemFileNumber] unsignedLongLongValue] ==
				(unsigned long long)st.st_ino &&
			      [[itemAttributes objectForKey:NSFileReferenceCount] unsignedLongLongValue] ==
				(unsigned long long)st.st_nlink &&
			      [[itemAttributes objectForKey:NSFileDeviceIdentifier] unsignedLongLongValue] ==
				(unsigned long long)st.st_dev &&
			      [itemAttributes objectForKey:NSFileCreationDate] == nil,
			      [NSString stringWithFormat:@"ino=%llu/%llu links=%llu/%llu dev=%llu/%llu creation=%@",
				[[itemAttributes objectForKey:NSFileSystemFileNumber] unsignedLongLongValue],
				statOK ? (unsigned long long)st.st_ino : 0ULL,
				[[itemAttributes objectForKey:NSFileReferenceCount] unsignedLongLongValue],
				statOK ? (unsigned long long)st.st_nlink : 0ULL,
				[[itemAttributes objectForKey:NSFileDeviceIdentifier] unsignedLongLongValue],
				statOK ? (unsigned long long)st.st_dev : 0ULL,
				[itemAttributes objectForKey:NSFileCreationDate] == nil ? @"absent" : @"present"]);

			/* AND THE TYPE VOCABULARY, WHICH WAS MISSING THREE WORDS: a SOCKET is one of them and the
			 * probe can make one, while a FIFO is NOT - Apple publishes no value for a fifo, so the
			 * honest answer to a question whose vocabulary has no word is the unknown word. Both are
			 * asserted, because "we name sockets now" and "we still do not invent a fifo" are two
			 * halves of the same rule. */
			check("fs-a-socket-is-named-and-a-fifo-is-not",
			      [[itemAttributes objectForKey:NSFileType] isEqualToString:NSFileTypeRegular] &&
			      [[[fm attributesOfItemAtPath:fn_s3(@"sock") error:NULL] objectForKey:NSFileType]
				isEqualToString:NSFileTypeSocket] &&
			      [[[fm attributesOfItemAtPath:fn_s3(@"pipe") error:NULL] objectForKey:NSFileType]
				isEqualToString:NSFileTypeUnknown],
			      [NSString stringWithFormat:@"regular=%@ socket=%@ fifo=%@",
				[itemAttributes objectForKey:NSFileType],
				[[fm attributesOfItemAtPath:fn_s3(@"sock") error:NULL] objectForKey:NSFileType],
				[[fm attributesOfItemAtPath:fn_s3(@"pipe") error:NULL] objectForKey:NSFileType]]);

			/* THE RELATIONSHIP RULE IS A PATH RULE, and the case that catches a careless
			 * implementation is the SIBLING WHOSE NAME ONLY PREFIXES the directory: a plain
			 * `hasPrefix:` would call `/…/treebc` "inside" `/…/tree`. */
			{
				NSURLRelationship relationship = NSURLRelationshipOther;
				NSURLRelationship same = NSURLRelationshipOther;
				NSURLRelationship sibling = NSURLRelationshipContains;
				NSURLRelationship missing = NSURLRelationshipOther;
				BOOL nested = [fm getRelationship:&relationship
					       ofDirectoryAtPath:fn_s3(@"tree")
						 toItemAtPath:fn_s3(@"tree/sub/b.txt")
						      error:NULL];
				BOOL self = [fm getRelationship:&same
					  ofDirectoryAtPath:fn_s3(@"tree")
					    toItemAtPath:fn_s3(@"tree")
						 error:NULL];
				BOOL prefixed = [fm getRelationship:&sibling
					      ofDirectoryAtPath:fn_s3(@"tree")
						toItemAtPath:fn_s3(@"treebc")
						     error:NULL];
				NSError *missingError = nil;
				BOOL absent = [fm getRelationship:&missing
					    ofDirectoryAtPath:fn_s3(@"tree")
					      toItemAtPath:fn_s3(@"not-here")
						   error:&missingError];

				check("fs-relationship-is-about-locations",
				      nested && relationship == NSURLRelationshipContains &&
				      self && same == NSURLRelationshipSame &&
				      prefixed && sibling == NSURLRelationshipOther &&
				      !absent && missingError != nil && [missingError code] == ENOENT,
				      [NSString stringWithFormat:@"nested=%d(%d) self=%d(%d) prefixed=%d(%d) "
					"missing=%d code=%ld", (int)nested, (int)relationship, (int)self, (int)same,
					(int)prefixed, (int)sibling, (int)absent,
					(long)(missingError != nil ? [missingError code] : -1)]);
			}
		}

		/* AND THE FIXTURE GOES, INCLUDING THE DANGLING LINK - which the recursive remove reaches
		 * because it lstat(2)s and unlinks rather than following anything. */
		cleaned = [fm removeItemAtPath:@S3_ROOT error:NULL];
		if (!cleaned) {
			printf("FOUNDATION-FILEMANAGER fs-s3-cleanup: the fixture is still at %s\n", S3_ROOT);
		}
	}

	/* --- AND WHERE A TEMPORARY FILE GOES, WHICH THE FSH ANSWERS RATHER THAN A CLASS ------------------ */
	{
		NSString *temporary = NSTemporaryDirectory();
		BOOL isDirectory = NO;
		BOOL exists = [[NSFileManager defaultManager] fileExistsAtPath:temporary
							  isDirectory:&isDirectory];

		/* THE PATH AND THE TRAILING SEPARATOR ARE BOTH ASSERTED: Apple's spelling ends in a separator, and
		 * a caller that appends a name without one must not be given `...Filesname`. */
		check("temporary-directory-is-the-fsh-path",
		      [temporary isEqualToString:@"/System/Temporary Files/"],
		      @"NSTemporaryDirectory() answers the FSH's own temporary directory");

		/* AND THE DIRECTION THAT MATTERS: THE PATH IT ANSWERS IS REAL. A constant that named a directory
		 * nothing creates would pass the check above and be useless. */
		check("temporary-directory-exists",
		      exists && isDirectory,
		      @"the directory NSTemporaryDirectory() names exists and is a directory");
	}

	printf("FOUNDATION-FILEMANAGER RESULT ok=%d fail=%d\n", okc, failc);
	/* The exit status, in the probe's OWN output. After a probe the console can stop
	 * serving INPUT for a while (the tier residual), so an `echo $?` that the harness
	 * types may never run. The probe knows exactly what it returns, so it says so - and
	 * this is the same value: failc ? 1 : 0 is the return statement below. */
	printf("FOUNDATION-FILEMANAGER-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-FILEMANAGER DONE\n");
	return failc ? 1 : 0;
}
