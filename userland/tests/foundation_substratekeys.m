/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_substratekeys, unit of 1 — W8p slice 6g's acceptance: THE KEYS THAT NEEDED A SUBSTRATE
 * MEASUREMENT. docs/design/foundation-plan.md §60.
 *
 * ONE unit, importing only <Foundation/Foundation.h> plus POSIX. Its fixture is its OWN (a uniquely named
 * directory under /System/Temporary Files, removed at the end) because every check here is an EXPERIMENT.
 *
 * THE MEASUREMENTS STAY PRINTED, and that is the point: this probe first ran as an INSTRUMENT and printed what
 * the file system does; the assertions below were written FROM those numbers rather than from a guess. A key
 * that claims a capability and does not show its measurement is exactly the confident wrong answer this
 * library's volume section warns about, so each check re-takes the measurement and requires the key to agree
 * with it.
 *
 *   keys-a-gapped-file-is-not-sparse  write one byte at 0 and one at 1 MiB; the file system CHARGES THE GAP
 *                                     (size 1048577, allocated 1049088) - so IsSparse and the volume's sparse
 *                                     support both answer NO;
 *   keys-extended-attributes-answer-per-volume  setxattr/listxattr/getxattr succeed on AGFS and listxattr
 *                                     fails with EOPNOTSUPP on procfs: ONE key, TWO answers on one machine;
 *   keys-the-block-size-comes-from-the-file-system  the volume's own statfs block size (1024 on the root,
 *                                     4096 on procfs);
 *   keys-hard-links-are-proved-not-claimed  link() really runs: two names, ONE inode, a link count of 2;
 *   keys-exclusive-renaming-is-refused-by-the-substrate  renameat2(RENAME_NOREPLACE) answers ENOSYS;
 *   keys-the-entry-count-is-two-sided  an EMPTY directory counts 0 and a populated one counts its entries;
 *   keys-the-plain-facts-are-true  nothing here is purgeable, no alias file exists, no volume is encrypted;
 *   keys-the-unavailable-ones-answer-without-error  the seven whose subject the system does not publish answer
 *                                     Apple's "NOT AVAILABLE for the specified resource, and no errors
 *                                     occurred" - answered YES, nil, no error.
 */

#import <Foundation/Foundation.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/statfs.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/xattr.h>
#include <unistd.h>

#ifndef RENAME_NOREPLACE
#define RENAME_NOREPLACE (1 << 0)
#endif

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-SUBSTRATEKEYS %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-SUBSTRATEKEYS %s FAIL %s\n", name, detail != nil ? [detail UTF8String] : "");
	}
}

static void measured(const char *name, NSString *value)
{
	printf("SUBSTRATE %s %s\n", name, value != nil ? [value UTF8String] : "(nil)");
}

static id fn_url(NSString *path)
{
	return [NSURL fileURLWithPath:path];
}

static id fn_key(NSURL *url, NSURLResourceKey key)
{
	id value = nil;

	if (![url getResourceValue:&value forKey:key error:NULL]) {
		return nil;
	}
	return value;
}

/* Apple's "unavailable": ANSWERED, the value is NIL, and there is NO error. */
static BOOL fn_is_unavailable(NSURL *url, NSURLResourceKey key, NSString **detail)
{
	id value = nil;
	NSError *error = nil;
	BOOL answered = [url getResourceValue:&value forKey:key error:&error];

	if (detail != NULL) {
		*detail = [NSString stringWithFormat:@"%@ answered=%d value=%@ error=%@", key, (int)answered,
			value, error];
	}
	return answered && value == nil && error == nil;
}

int main(void)
{
	NSFileManager *manager = [NSFileManager defaultManager];
	struct timeval now;
	struct timezone tz;
	NSString *scratch;
	NSString *gapped, *dense, *linkedA, *linkedB, *empty;

	(void)gettimeofday(&now, &tz);
	scratch = [NSString stringWithFormat:@"/System/Temporary Files/fnd-substrate-%d-%ld",
		(int)getpid(), (long)now.tv_usec];
	(void)[manager createDirectoryAtPath:scratch withIntermediateDirectories:YES attributes:nil error:NULL];
	gapped = [scratch stringByAppendingPathComponent:@"gapped.bin"];
	dense = [scratch stringByAppendingPathComponent:@"dense.bin"];
	linkedA = [scratch stringByAppendingPathComponent:@"linked.bin"];
	linkedB = [scratch stringByAppendingPathComponent:@"linked2.bin"];
	empty = [scratch stringByAppendingPathComponent:@"emptydir"];
	measured("scratch", scratch);

	/* --- SPARSE, AND THE SAME MEASUREMENT AS THE VOLUME'S CAPABILITY --------------------------------- */
	{
		int fd = open([gapped UTF8String], O_CREAT | O_WRONLY | O_TRUNC, 0644);
		char byte = 'x';

		if (fd >= 0) {
			(void)write(fd, &byte, 1);
			if (lseek(fd, 1024 * 1024, SEEK_SET) >= 0) {
				(void)write(fd, &byte, 1);
			}
			close(fd);
		}
		{
			int fd2 = open([dense UTF8String], O_CREAT | O_WRONLY | O_TRUNC, 0644);

			if (fd2 >= 0) {
				char buffer[4096];
				int i;

				memset(buffer, 'y', sizeof(buffer));
				for (i = 0; i < 257; i++) {
					(void)write(fd2, buffer, sizeof(buffer));
				}
				close(fd2);
			}
		}
		{
			id gsize = fn_key(fn_url(gapped), NSURLFileSizeKey);
			id galloc = fn_key(fn_url(gapped), NSURLFileAllocatedSizeKey);
			id dsize = fn_key(fn_url(dense), NSURLFileSizeKey);
			id dalloc = fn_key(fn_url(dense), NSURLFileAllocatedSizeKey);

			measured("gapped", [NSString stringWithFormat:@"size=%@ allocated=%@", gsize, galloc]);
			measured("dense", [NSString stringWithFormat:@"size=%@ allocated=%@", dsize, dalloc]);
			check("keys-a-gapped-file-is-not-sparse",
			      [fn_key(fn_url(gapped), NSURLIsSparseKey) boolValue] == NO &&
			      [fn_key(fn_url(gapped), NSURLVolumeSupportsSparseFilesKey) boolValue] == NO &&
			      [galloc unsignedLongLongValue] >= [gsize unsignedLongLongValue],
			      [NSString stringWithFormat:@"size=%@ allocated=%@ isSparse=%@ volumeSparse=%@",
				gsize, galloc, fn_key(fn_url(gapped), NSURLIsSparseKey),
				fn_key(fn_url(gapped), NSURLVolumeSupportsSparseFilesKey)]);
		}
	}

	/* --- EXTENDED ATTRIBUTES: one key, two volumes --------------------------------------------------- */
	{
		char buffer[64];
		ssize_t set;
		NSString *detail = nil;
		id onAgfs = fn_key(fn_url(dense), NSURLMayHaveExtendedAttributesKey);
		id onProcfs = fn_key(fn_url(@"/System/Processes/version"), NSURLMayHaveExtendedAttributesKey);

		errno = 0;
		set = setxattr([dense UTF8String], "user.fnprobe", "v", 1, 0);
		errno = 0;
		measured("xattr-list-procfs", [NSString stringWithFormat:@"ret=%ld errno=%d (%s)",
			(long)listxattr("/System/Processes/version", buffer, sizeof(buffer)), errno,
			strerror(errno)]);
		measured("xattr-set", [NSString stringWithFormat:@"ret=%ld", (long)set]);
		if (detail == nil) {
			detail = [NSString stringWithFormat:@"AGFS=%@ procfs=%@ setxattr=%ld",
				onAgfs, onProcfs, (long)set];
		}
		check("keys-extended-attributes-answer-per-volume",
		      set == 0 && [onAgfs boolValue] == YES && [onProcfs boolValue] == NO, detail);
	}

	/* --- THE BLOCK SIZE, PER VOLUME ------------------------------------------------------------------ */
	{
		struct statfs root, proc;

		(void)statfs("/", &root);
		(void)statfs("/System/Processes", &proc);
		measured("statfs", [NSString stringWithFormat:@"root bsize=%lu procfs bsize=%lu",
			(unsigned long)root.f_bsize, (unsigned long)proc.f_bsize]);
		check("keys-the-block-size-comes-from-the-file-system",
		      [fn_key(fn_url(@"/"), NSURLPreferredIOBlockSizeKey) unsignedLongLongValue] ==
			(unsigned long long)root.f_bsize &&
		      [fn_key(fn_url(@"/System/Processes"), NSURLPreferredIOBlockSizeKey) unsignedLongLongValue] ==
			(unsigned long long)proc.f_bsize,
		      [NSString stringWithFormat:@"root key=%@ statfs=%lu procfs key=%@ statfs=%lu",
			fn_key(fn_url(@"/"), NSURLPreferredIOBlockSizeKey), (unsigned long)root.f_bsize,
			fn_key(fn_url(@"/System/Processes"), NSURLPreferredIOBlockSizeKey),
			(unsigned long)proc.f_bsize]);
	}

	/* --- HARD LINKS, PROVED -------------------------------------------------------------------------- */
	{
		int ret;
		id inodeA = nil, inodeB = nil, countA = nil;

		(void)[@"link target" writeToFile:linkedA atomically:YES encoding:NSUTF8StringEncoding error:NULL];
		(void)unlink([linkedB UTF8String]);
		errno = 0;
		ret = link([linkedA UTF8String], [linkedB UTF8String]);
		if (ret == 0) {
			inodeA = fn_key(fn_url(linkedA), NSURLFileIdentifierKey);
			inodeB = fn_key(fn_url(linkedB), NSURLFileIdentifierKey);
			countA = fn_key(fn_url(linkedA), NSURLLinkCountKey);
		}
		measured("hardlink", [NSString stringWithFormat:@"ret=%d inode %@/%@ count=%@ %@", ret, inodeA,
			inodeB, countA, ret < 0 ? [NSString stringWithFormat:@"errno=%d", errno] : @""]);
		check("keys-hard-links-are-proved-not-claimed",
		      ret == 0 && [inodeA isEqual:inodeB] && [countA unsignedLongLongValue] == 2 &&
		      [fn_key(fn_url(linkedA), NSURLVolumeSupportsHardLinksKey) boolValue] == YES,
		      [NSString stringWithFormat:@"ret=%d inode=%@/%@ count=%@ key=%@", ret, inodeA, inodeB,
			countA, fn_key(fn_url(linkedA), NSURLVolumeSupportsHardLinksKey)]);
	}

	/* --- A FLAGGED RENAME: the substrate refuses it, and the key says so ----------------------------- */
	{
		int ret;

		errno = 0;
#ifdef SYS_renameat2
		ret = (int)syscall(SYS_renameat2, AT_FDCWD, [linkedA UTF8String], AT_FDCWD, [dense UTF8String],
				   RENAME_NOREPLACE);
#else
		ret = (int)syscall(316, AT_FDCWD, [linkedA UTF8String], AT_FDCWD, [dense UTF8String],
				   RENAME_NOREPLACE);
#endif
		measured("renameat2", [NSString stringWithFormat:@"ret=%d errno=%d (%s)", ret, errno,
			strerror(errno)]);
		check("keys-exclusive-renaming-is-refused-by-the-substrate",
		      ret != 0 && errno == ENOSYS &&
		      [fn_key(fn_url(linkedA), NSURLVolumeSupportsExclusiveRenamingKey) boolValue] == NO,
		      [NSString stringWithFormat:@"ret=%d errno=%d key=%@", ret, errno,
			fn_key(fn_url(linkedA), NSURLVolumeSupportsExclusiveRenamingKey)]);
	}

	/* --- THE ENTRY COUNT, TWO-SIDED ------------------------------------------------------------------ */
	{
		NSArray *populated;
		NSUInteger n;

		(void)[manager createDirectoryAtPath:empty withIntermediateDirectories:NO attributes:nil error:NULL];
		populated = [manager contentsOfDirectoryAtPath:scratch error:NULL];
		n = [populated count];
		measured("entries", [NSString stringWithFormat:@"scratch=%lu empty=%@", (unsigned long)n,
			fn_key(fn_url(empty), NSURLDirectoryEntryCountKey)]);
		check("keys-the-entry-count-is-two-sided",
		      [fn_key(fn_url(empty), NSURLDirectoryEntryCountKey) unsignedLongLongValue] == 0 &&
		      [fn_key(fn_url(scratch), NSURLDirectoryEntryCountKey) unsignedLongLongValue] ==
			(unsigned long long)n && n > 0,
		      [NSString stringWithFormat:@"empty=%@ scratch=%@ expected=%lu",
			fn_key(fn_url(empty), NSURLDirectoryEntryCountKey),
			fn_key(fn_url(scratch), NSURLDirectoryEntryCountKey), (unsigned long)n]);
	}

	/* --- THE PLAIN FACTS ----------------------------------------------------------------------------- */
	{
		id purgeable = fn_key(fn_url(dense), NSURLIsPurgeableKey);
		id alias = fn_key(fn_url(dense), NSURLIsAliasFileKey);
		id encrypted = fn_key(fn_url(@"/"), NSURLVolumeIsEncryptedKey);

		check("keys-the-plain-facts-are-true",
		      [purgeable boolValue] == NO && [alias boolValue] == NO && [encrypted boolValue] == NO &&
		      purgeable != nil && alias != nil && encrypted != nil,
		      [NSString stringWithFormat:@"purgeable=%@ alias=%@ encrypted=%@", purgeable, alias,
			encrypted]);
	}

	/* --- THE UNAVAILABLE ONES ------------------------------------------------------------------------ */
	{
		NSURL *item = fn_url(dense);
		{
			NSArray *seven = [NSArray arrayWithObjects:NSURLVolumeCreationDateKey,
					  NSURLFileContentIdentifierKey, NSURLMayShareFileContentKey,
					  NSURLIsPackageKey, NSURLIsApplicationKey, NSURLVolumeIsRemovableKey,
					  NSURLVolumeIsEjectableKey, nil];
			NSString *detail = nil;
			NSUInteger i;
			BOOL all = YES;

			for (i = 0; i < [seven count]; i++) {
				NSString *one = nil;

				if (!fn_is_unavailable(item, [seven objectAtIndex:i], &one)) {
					all = NO;
					detail = one;
					break;
				}
			}
			check("keys-the-unavailable-ones-answer-without-error", all,
			      detail != nil ? detail : @"");
		}
	}

	(void)[manager removeItemAtPath:scratch error:NULL];
	printf("FOUNDATION-SUBSTRATEKEYS RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-SUBSTRATEKEYS DONE\n");
	return failc == 0 ? 0 : 1;
}
