/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_urlresourcevalues, unit of 1 — W8 slice 6a's acceptance for NSURL's resource values.
 * docs/design/foundation-plan.md §60.
 *
 * ONE unit, importing only <Foundation/Foundation.h>. IT WORKS IN A TREE OF ITS OWN MAKING under
 * /System/Temporary Files, built with POSIX calls so that a failure cannot be the fixture's fault, and
 * removes it at the end and at the start.
 *
 * WHAT IT MEASURES, and the last one is the reason this unit is worth its own probe:
 *   rv-the-keys-are-their-own-names   the key and type constants are the published names (D2's spelling),
 *                                     distinct, and non-nil;
 *   rv-a-file-answers-its-facts       name, path, size, the three kinds, the three access answers, the
 *                                     link count, the inode identifier, the resource type, the parent -
 *                                     each against what the SAME fixture's lstat says;
 *   rv-a-directory-answers-its-own    a directory is a directory and not a regular file;
 *   rv-a-link-is-about-the-link       lstat AND NOT stat, which is the whole reason the reader is
 *                                     lstat(2): a link's -fileSizeKey is the LENGTH OF ITS TARGET STRING;
 *   rv-the-dot-rule-makes-an-item-hidden  there is no hidden bit here, so the name's leading dot is the
 *                                     rule - asserted from BOTH sides;
 *   rv-a-missing-file-refuses-with-its-errno  NO and an ENOENT error, and the dictionary form answers
 *                                     EMPTY rather than nil (one unreadable key does not sink a set);
 *   rv-a-non-file-url-refuses         D9's precedent, from the other class;
 *   rv-an-unknown-key-refuses         a key this library does not answer is an ERROR in both shapes (a
 *                                     set with something unknown in it is a caller error, not a partial
 *                                     answer);
 *   rv-reachability-is-not-readability  access(F_OK) for -checkResourceIsReachableAndReturnError:, and a
 *                                     missing path answers NO with an error;
 *   rv-the-cache-is-part-of-the-contract  THE CENTREPIECE: read a size, CHANGE THE FILE ON DISK, read
 *                                     again and get the CACHED answer, then clear the cache and get the
 *                                     fresh one - which is Apple's documented behaviour and the only way
 *                                     to tell a cache from a re-read;
 *   rv-removing-everything-clears-everything  -removeAllCachedResourceValues;
 *   rv-a-temporary-value-is-not-on-disk  -setTemporaryResourceValue:forKey: answers what was set, a
 *                                     SECOND URL for the same file still answers the disk's truth, and
 *                                     nil takes the entry back out;
 *   rv-two-doors-agree                the URL's modification date and NSFileManager's attribute are the
 *                                     same fact through two doors;
 *   rv-a-set-writes-and-the-cache-forgets  THE WRITE SIDE: a real write to the disk, read back through
 *                                     the URL - and a set FORGETS what the cache knew, which is why the
 *                                     read that follows the write is fresh rather than stale;
 *   rv-a-set-is-visible-to-the-other-door  the same fact through NSFileManager, which is the delegation;
 *   rv-a-set-of-a-read-only-key-is-a-no-op / -an-unknown-key / -on-a-non-file-url  Apple's sentence for
 *                                     BOTH setters: such attempts "are ignored and are not considered
 *                                     errors", so the door answers YES and writes nothing;
 *   rv-a-set-of-many-applies-what-it-can  the dictionary form applies what it can and ignores the rest;
 *   rv-set-many-reports-what-it-could-not-set  and reports, under NSURLKeysOfUnsetValuesKey, the keys
 *                                     whose write REACHED the file system and failed;
 *   rv-a-value-the-key-cannot-hold-is-refused  the one refusal this door makes for its own reason;
 *   vol-capacity-is-the-filesystems-and-both-doors-agree  a volume key asked of a file URL is about the
 *                                     volume holding it, and the number agrees with NSFileManager's own
 *                                     file-system attributes (two doors, one superblock);
 *   vol-is-local-and-writable-here    every volume this system can mount is local, and the temp tree's
 *                                     volume is writable here;
 *   vol-supports-case-sensitive-names-and-the-proof / -persistent-ids / -symbolic-links  THE THREE
 *                                     CLAIMS THE SUBSTRATE REALLY SUPPORTS, each PROVED by something the
 *                                     fixture does: two names differing only in case becoming two inodes,
 *                                     one identifier answering from two independently built URLs, and a
 *                                     link reading back the name it points at;
 *   probe-tree-removed                the tree is gone.
 */

#import <Foundation/Foundation.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PROBE_ROOT "/System/Temporary Files/nsurl-resourcevalues-probe"

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-URLRESOURCEVALUES %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-URLRESOURCEVALUES %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

/* NULLABLE CONSTRUCTORS AND LOOKUPS ROUTED THROUGH `id`, which is this tier's rule for
 * -Werror=nullable-to-nonnull-conversion: `+fileURLWithPath:` and a dictionary lookup are both declared
 * nullable, and an `id`-returning helper's result carries no nullability for the gate to refuse. */
static id fn_url(NSString *path)
{
	return [NSURL fileURLWithPath:path];
}

static id fn_lookup(NSDictionary *dictionary, NSString *key)
{
	return [dictionary objectForKey:key];
}

static NSString *fn_path(NSString *relative)
{
	return [NSString stringWithFormat:@"%s/%@", PROBE_ROOT, relative];
}

/* THE FIXTURE IS BUILT WITH POSIX CALLS, because what is under test is the URL's reader and a fixture
 * built by the library under test could fail for the fixture's reasons. */
static void fn_write(NSString *path, const char *bytes)
{
	int fd = open([path UTF8String], O_WRONLY | O_CREAT | O_TRUNC, 0644);

	if (fd >= 0) {
		size_t length = strlen(bytes);
		ssize_t wrote = write(fd, bytes, length);

		(void)wrote;
		close(fd);
	}
}

static void fn_append(NSString *path, const char *bytes)
{
	int fd = open([path UTF8String], O_WRONLY | O_APPEND);

	if (fd >= 0) {
		size_t length = strlen(bytes);
		ssize_t wrote = write(fd, bytes, length);

		(void)wrote;
		close(fd);
	}
}

/* THE VALUE OF A BOOLEAN KEY, as a tri-state so that "absent" and "NO" cannot be confused: -1 is
 * absent, which is how this door set says "this substrate has nothing behind that key". */
static int fn_bool_key(NSURL *url, NSURLResourceKey key)
{
	id value = nil;

	if (![url getResourceValue:&value forKey:key error:NULL]) {
		return -1;
	}
	if (value == nil) {
		return -1;
	}
	return [value boolValue] ? 1 : 0;
}

static long long fn_number_key(NSURL *url, NSURLResourceKey key)
{
	id value = nil;

	if (![url getResourceValue:&value forKey:key error:NULL] || value == nil) {
		return -1;
	}
	return [value longLongValue];
}

int main(void)
{
	NSFileManager *manager = [NSFileManager defaultManager];
	NSString *root = [NSString stringWithUTF8String:PROBE_ROOT];

	[manager removeItemAtPath:root error:NULL];
	mkdir(PROBE_ROOT, 0755);
	mkdir([fn_path(@"inner") UTF8String], 0755);
	fn_write(fn_path(@"inner/note.txt"), "hello");
	fn_write(fn_path(@"inner/.hidden"), "h");
	symlink("note.txt", [fn_path(@"inner/link") UTF8String]);

	/* ---- THE CONSTANTS ------------------------------------------------------------------------- */
	{
		NSArray *keys = [NSArray arrayWithObjects:NSURLNameKey, NSURLLocalizedNameKey, NSURLPathKey,
				 NSURLIsRegularFileKey, NSURLIsDirectoryKey, NSURLFileSizeKey,
				 NSURLFileResourceTypeKey, NSURLFileIdentifierKey, NSURLParentDirectoryURLKey, nil];
		NSArray *types = [NSArray arrayWithObjects:NSURLFileResourceTypeRegular,
				  NSURLFileResourceTypeDirectory, NSURLFileResourceTypeSymbolicLink,
				  NSURLFileResourceTypeSocket, NSURLFileResourceTypeCharacterSpecial,
				  NSURLFileResourceTypeBlockSpecial, NSURLFileResourceTypeNamedPipe,
				  NSURLFileResourceTypeUnknown, nil];
		NSMutableSet *distinct = [NSMutableSet set];

		[distinct addObjectsFromArray:keys];
		[distinct addObjectsFromArray:types];
		check("rv-the-keys-are-their-own-names",
		      [NSURLNameKey isEqual:@"NSURLNameKey"] &&
		      [NSURLFileResourceTypeDirectory isEqual:@"NSURLFileResourceTypeDirectory"] &&
		      [distinct count] == [keys count] + [types count] &&
		      [NSURLFileSizeKey length] > 0,
		      [NSString stringWithFormat:@"%lu distinct names of %lu",
			(unsigned long)[distinct count], (unsigned long)([keys count] + [types count])]);
	}

	/* ---- A FILE'S FACTS, AGAINST THE SAME FIXTURE'S lstat -------------------------------------- */
	{
		NSURL *file = [NSURL fileURLWithPath:fn_path(@"inner/note.txt")];
		struct stat st;
		id name = nil, path = nil, identifier = nil, type = nil, parent = nil;
		int lstatOk = lstat([[file path] UTF8String], &st);

		[file getResourceValue:&name forKey:NSURLNameKey error:NULL];
		[file getResourceValue:&path forKey:NSURLPathKey error:NULL];
		[file getResourceValue:&identifier forKey:NSURLFileIdentifierKey error:NULL];
		[file getResourceValue:&type forKey:NSURLFileResourceTypeKey error:NULL];
		[file getResourceValue:&parent forKey:NSURLParentDirectoryURLKey error:NULL];
		check("rv-a-file-answers-its-facts",
		      lstatOk == 0 && [name isEqual:@"note.txt"] &&
		      [path isEqual:fn_path(@"inner/note.txt")] &&
		      [type isEqual:NSURLFileResourceTypeRegular] &&
		      [identifier unsignedLongLongValue] == (unsigned long long)st.st_ino &&
		      [parent isEqual:fn_url(fn_path(@"inner"))] &&
		      fn_number_key(file, NSURLFileSizeKey) == 5 &&
		      fn_number_key(file, NSURLLinkCountKey) == 1 &&
		      fn_number_key(file, NSURLTotalFileSizeKey) == 5 &&
		      fn_bool_key(file, NSURLIsRegularFileKey) == 1 &&
		      fn_bool_key(file, NSURLIsDirectoryKey) == 0 &&
		      fn_bool_key(file, NSURLIsSymbolicLinkKey) == 0 &&
		      fn_bool_key(file, NSURLIsReadableKey) == 1 &&
		      /* THE ACCESS ANSWERS ARE ASSERTED AGAINST access(2) AND NOT AGAINST A GUESS ABOUT THE
		       * USER, because this guest runs as uid 0 and THIS KERNEL GIVES ROOT EVERYTHING: its
		       * check_permission() returns 0 for uid 0 (kernel/syscalls.c, measured), so a 0644 file
		       * IS executable here. Hardcoding "not executable" would have pinned my assumption
		       * instead of the door's arithmetic; the uid is printed so the reading is honest. */
		      fn_bool_key(file, NSURLIsExecutableKey) ==
				(access([[file path] UTF8String], X_OK) == 0 ? 1 : 0),
		      [NSString stringWithFormat:@"name=%@ size=%lld inode=%llu mode=%o uid=%u exec=%d "
			@"(access(2) says %d)", name, fn_number_key(file, NSURLFileSizeKey),
			[identifier unsignedLongLongValue], (unsigned)st.st_mode, (unsigned)getuid(),
			fn_bool_key(file, NSURLIsExecutableKey),
			access([[file path] UTF8String], X_OK) == 0 ? 1 : 0]);
	}

	/* ---- A DIRECTORY, AND THE LINK WHOSE FACTS ARE THE LINK'S OWN ------------------------------ */
	{
		NSURL *directory = [NSURL fileURLWithPath:fn_path(@"inner")];
		NSURL *link = [NSURL fileURLWithPath:fn_path(@"inner/link")];

		check("rv-a-directory-answers-its-own",
		      fn_bool_key(directory, NSURLIsDirectoryKey) == 1 &&
		      fn_bool_key(directory, NSURLIsRegularFileKey) == 0 &&
		      [[NSURL fileURLWithPath:fn_path(@"inner")] isEqual:directory],
		      @"a directory is a directory and not a regular file");

		/* THE LENGTH OF THE TARGET STRING, 8 = strlen("note.txt") - which is what proves the reader is
		 * lstat(2): stat would have answered the FILE's size of 5. */
		check("rv-a-link-is-about-the-link",
		      fn_bool_key(link, NSURLIsSymbolicLinkKey) == 1 &&
		      fn_bool_key(link, NSURLIsDirectoryKey) == 0 &&
		      fn_number_key(link, NSURLFileSizeKey) == 8,
		      [NSString stringWithFormat:@"link size=%lld (a stat-based reader would say 5)",
			fn_number_key(link, NSURLFileSizeKey)]);
	}

	/* ---- THE DOT RULE, FROM BOTH SIDES --------------------------------------------------------- */
	{
		NSURL *hidden = [NSURL fileURLWithPath:fn_path(@"inner/.hidden")];
		NSURL *plain = [NSURL fileURLWithPath:fn_path(@"inner/note.txt")];

		check("rv-the-dot-rule-makes-an-item-hidden",
		      fn_bool_key(hidden, NSURLIsHiddenKey) == 1 && fn_bool_key(plain, NSURLIsHiddenKey) == 0,
		      [NSString stringWithFormat:@"hidden=%d plain=%d",
			fn_bool_key(hidden, NSURLIsHiddenKey), fn_bool_key(plain, NSURLIsHiddenKey)]);
	}

	/* ---- THE REFUSALS -------------------------------------------------------------------------- */
	{
		NSURL *missing = [NSURL fileURLWithPath:fn_path(@"inner/nothing-here")];
		NSURL *web = [NSURL URLWithString:@"https://example.invalid/thing"];
		id value = nil;
		NSError *error = nil;
		BOOL answered = [missing getResourceValue:&value forKey:NSURLFileSizeKey error:&error];
		BOOL refused = !answered && error != nil && [error code] == ENOENT;
		NSDictionary *set = [missing resourceValuesForKeys:
				     [NSArray arrayWithObjects:NSURLNameKey, NSURLFileSizeKey, nil] error:NULL];

		check("rv-a-missing-file-refuses-with-its-errno",
		      refused && set != nil && [set count] == 0,
		      [NSString stringWithFormat:@"code=%ld dictionary=%lu entries",
			(long)(error != nil ? [error code] : -1),
			(unsigned long)(set != nil ? [set count] : (NSUInteger)-1)]);

		error = nil;
		value = nil;
		check("rv-a-non-file-url-refuses",
		      ![web getResourceValue:&value forKey:NSURLNameKey error:&error] && error != nil &&
		      ![web checkResourceIsReachableAndReturnError:&error] && error != nil,
		      [NSString stringWithFormat:@"scheme=%@ refused with %@", [web scheme],
			error != nil ? [error localizedDescription] : (id)@"nothing"]);
	}

	{
		NSURL *file = [NSURL fileURLWithPath:fn_path(@"inner/note.txt")];
		id value = nil;
		NSError *single = nil;
		NSError *setError = nil;
		BOOL singleRefused = ![file getResourceValue:&value forKey:@"NSURLNotAKeyWeAnswer" error:&single];
		NSDictionary *set = [file resourceValuesForKeys:
				     [NSArray arrayWithObjects:NSURLNameKey, @"NSURLNotAKeyWeAnswer", nil]
							      error:&setError];

		check("rv-an-unknown-key-refuses",
		      singleRefused && single != nil && set == nil && setError != nil,
		      [NSString stringWithFormat:@"single=%d set=%p", (int)singleRefused, (void *)set]);
	}

	/* ---- REACHABILITY IS NOT READABILITY ------------------------------------------------------- */
	{
		NSURL *file = [NSURL fileURLWithPath:fn_path(@"inner/note.txt")];
		NSURL *missing = [NSURL fileURLWithPath:fn_path(@"inner/gone")];
		NSError *error = nil;

		check("rv-reachability-is-not-readability",
		      [file checkResourceIsReachableAndReturnError:NULL] &&
		      ![missing checkResourceIsReachableAndReturnError:&error] && error != nil &&
		      [error code] == ENOENT,
		      [NSString stringWithFormat:@"missing: %@",
			error != nil ? [error localizedDescription] : (id)@"no error"]);
	}

	/* ---- THE CACHE, WHICH IS THE REASON THIS UNIT IS PROBED THIS WAY --------------------------- */
	{
		NSURL *file = [NSURL fileURLWithPath:fn_path(@"inner/cache.txt")];
		long long first, second, third, fourth;

		fn_write(fn_path(@"inner/cache.txt"), "0123456789");
		first = fn_number_key(file, NSURLFileSizeKey);
		fn_append(fn_path(@"inner/cache.txt"), "abcdefghij");	/* 10 -> 20 bytes on disk */
		second = fn_number_key(file, NSURLFileSizeKey);
		[file removeCachedResourceValueForKey:NSURLFileSizeKey];
		third = fn_number_key(file, NSURLFileSizeKey);
		fn_append(fn_path(@"inner/cache.txt"), "xxxxx");		/* 20 -> 25 */
		[file removeAllCachedResourceValues];
		fourth = fn_number_key(file, NSURLFileSizeKey);

		check("rv-the-cache-is-part-of-the-contract",
		      first == 10 && second == 10 && third == 20,
		      [NSString stringWithFormat:@"read=%lld after-append=%lld after-remove=%lld",
			first, second, third]);
		check("rv-removing-everything-clears-everything", fourth == 25,
		      [NSString stringWithFormat:@"after-removeAll=%lld, disk says 25", fourth]);
	}

	/* ---- A TEMPORARY VALUE IS NOT ON DISK ------------------------------------------------------ */
	{
		NSURL *file = [NSURL fileURLWithPath:fn_path(@"inner/note.txt")];
		NSURL *other = [NSURL fileURLWithPath:fn_path(@"inner/note.txt")];
		id value = nil;

		[file setTemporaryResourceValue:@"not-a-size" forKey:NSURLFileSizeKey];
		[file getResourceValue:&value forKey:NSURLFileSizeKey error:NULL];
		{
			BOOL temporary = [value isEqual:@"not-a-size"];

			[file setTemporaryResourceValue:nil forKey:NSURLFileSizeKey];
			check("rv-a-temporary-value-is-not-on-disk",
			      temporary && fn_number_key(other, NSURLFileSizeKey) == 5 &&
			      fn_number_key(file, NSURLFileSizeKey) == 5,
			      [NSString stringWithFormat:@"temporary=%@ other=%lld after-nil=%lld",
				value, fn_number_key(other, NSURLFileSizeKey),
				fn_number_key(file, NSURLFileSizeKey)]);
		}
	}

	/* ---- TWO DOORS, ONE FACT ------------------------------------------------------------------- */
	{
		NSURL *file = [NSURL fileURLWithPath:fn_path(@"inner/note.txt")];
		id value = nil;
		NSDictionary *attributes = [manager attributesOfItemAtPath:fn_path(@"inner/note.txt") error:NULL];

		[file getResourceValue:&value forKey:NSURLContentModificationDateKey error:NULL];
		check("rv-two-doors-agree",
		      value != nil && [value isEqual:fn_lookup(attributes, NSFileModificationDate)],
		      [NSString stringWithFormat:@"url=%@ attributes=%@", value,
			fn_lookup(attributes, NSFileModificationDate)]);
	}

	/* ---- W8 SLICE 6b: THE WRITE SIDE, WHERE THE REFUSAL IS A NO-OP ----------------------------- */
	{
		NSURL *file = [NSURL fileURLWithPath:fn_path(@"inner/note.txt")];
		NSDate *when = [NSDate dateWithTimeIntervalSince1970:1234567890.0];
		NSDate *beforeWrite = nil;
		NSDate *afterWrite = nil;
		NSError *error = nil;
		BOOL wrote;

		/* READ FIRST, so the cache HOLDS a value before the write: that is what makes the
		 * forget-the-cache rule observable rather than assumed. */
		[file getResourceValue:&beforeWrite forKey:NSURLContentModificationDateKey error:NULL];
		wrote = [file setResourceValue:when forKey:NSURLContentModificationDateKey error:&error];
		[file getResourceValue:&afterWrite forKey:NSURLContentModificationDateKey error:NULL];
		check("rv-a-set-writes-and-the-cache-forgets",
		      wrote && error == nil && [afterWrite isEqual:when] &&
		      ![afterWrite isEqual:beforeWrite],
		      [NSString stringWithFormat:@"wrote=%d before=%@ after=%@ wanted=%@",
			(int)wrote, beforeWrite, afterWrite, when]);

		{
			/* THE SAME FACT THROUGH THE OTHER DOOR, which is the whole point of a resource value. */
			NSDictionary *attributes = [manager attributesOfItemAtPath:fn_path(@"inner/note.txt")
									     error:NULL];

			check("rv-a-set-is-visible-to-the-other-door",
			      [fn_lookup(attributes, NSFileModificationDate) isEqual:when],
			      [NSString stringWithFormat:@"nsfilemanager says %@",
				fn_lookup(attributes, NSFileModificationDate)]);
		}
	}

	{
		/* APPLE'S SENTENCE, IN THREE SHAPES: a READ-ONLY key, an UNKNOWN key and a URL that is not a
		 * FILE URL are all "ignored and are not considered errors" - so each answers YES with no error
		 * and writes nothing, which the file's unchanged size demonstrates. */
		NSURL *file = [NSURL fileURLWithPath:fn_path(@"inner/note.txt")];
		NSURL *fresh = [NSURL fileURLWithPath:fn_path(@"inner/note.txt")];
		NSURL *web = [NSURL URLWithString:@"https://example.invalid/thing"];
		NSError *readOnly = nil;
		NSError *unknown = nil;
		NSError *nonFile = nil;

		check("rv-a-set-of-a-read-only-key-is-a-no-op",
		      [file setResourceValue:@"1" forKey:NSURLFileSizeKey error:&readOnly] &&
		      readOnly == nil && fn_number_key(fresh, NSURLFileSizeKey) == 5,
		      [NSString stringWithFormat:@"error=%@ size=%lld",
			readOnly, fn_number_key(fresh, NSURLFileSizeKey)]);

		check("rv-a-set-of-an-unknown-key-is-a-no-op",
		      [file setResourceValue:@"1" forKey:@"NSURLNotAKeyWeAnswer" error:&unknown] &&
		      unknown == nil,
		      [NSString stringWithFormat:@"error=%@", unknown]);

		check("rv-a-set-on-a-non-file-url-is-a-no-op",
		      [web setResourceValue:@"1" forKey:NSURLNameKey error:&nonFile] && nonFile == nil,
		      [NSString stringWithFormat:@"error=%@", nonFile]);
	}

	{
		/* THE SET FORM: it applies what it can, ignores what it cannot, and reports only the writes
		 * that REACHED THE FILE SYSTEM AND FAILED. */
		NSURL *file = [NSURL fileURLWithPath:fn_path(@"inner/note.txt")];
		NSURL *fresh = [NSURL fileURLWithPath:fn_path(@"inner/note.txt")];
		NSDate *when = [NSDate dateWithTimeIntervalSince1970:1111111111.0];
		NSDictionary *values = [NSDictionary dictionaryWithObjectsAndKeys:
					when, NSURLContentModificationDateKey,
					[NSNumber numberWithInt:1], NSURLFileSizeKey, nil];
		NSError *error = nil;
		BOOL applied = [file setResourceValues:values error:&error];
		NSDate *readBack = nil;

		[file getResourceValue:&readBack forKey:NSURLContentModificationDateKey error:NULL];
		check("rv-a-set-of-many-applies-what-it-can",
		      applied && error == nil && [readBack isEqual:when] && fn_number_key(fresh, NSURLFileSizeKey) == 5,
		      [NSString stringWithFormat:@"applied=%d error=%@ date=%@ size=%lld",
			(int)applied, error, readBack, fn_number_key(fresh, NSURLFileSizeKey)]);

		{
			/* AND THE ONE ERROR SHAPE APPLE PUBLISHES, PRODUCED BY A WRITE THAT REALLY FAILED: the
			 * path does not exist, so the substrate's own write refuses, and the failure report
			 * carries the keys that were not set under the key's own name. */
			NSURL *missing = [NSURL fileURLWithPath:fn_path(@"inner/never-created")];
			NSDictionary *impossible = [NSDictionary dictionaryWithObject:when
									      forKey:NSURLContentModificationDateKey];
			NSError *failure = nil;
			BOOL refused = [missing setResourceValues:impossible error:&failure];
			NSArray *unset = fn_lookup([failure userInfo], NSURLKeysOfUnsetValuesKey);

			check("rv-set-many-reports-what-it-could-not-set",
			      !refused && failure != nil && unset != nil && [unset count] == 1 &&
			      [[unset objectAtIndex:0] isEqual:NSURLContentModificationDateKey],
			      [NSString stringWithFormat:@"refused=%d error=%@ unset=%@",
				(int)refused, failure, unset]);
		}
	}

	{
		/* AND A VALUE THE KEY CANNOT HOLD IS A CALLER ERROR - the one refusal this door makes for a
		 * reason of its own, because the key's type is published (a date) and a string is a mistake
		 * rather than a no-op. */
		NSURL *file = [NSURL fileURLWithPath:fn_path(@"inner/note.txt")];
		NSError *error = nil;
		BOOL refused = [file setResourceValue:@"not a date" forKey:NSURLContentModificationDateKey
						error:&error];

		check("rv-a-value-the-key-cannot-hold-is-refused",
		      !refused && error != nil,
		      [NSString stringWithFormat:@"refused=%d error=%@", (int)refused, error]);
	}

	/* ---- W8 SLICE 6c: THE VOLUME'S KEY, AND WHERE THE VALUE IS A CLAIM, THE PROOF ---------------- */
	{
		NSURL *file = [NSURL fileURLWithPath:fn_path(@"inner/note.txt")];
		NSDictionary *fs = [manager attributesOfFileSystemForPath:fn_path(@"inner") error:NULL];
		long long total = fn_number_key(file, NSURLVolumeTotalCapacityKey);
		long long available = fn_number_key(file, NSURLVolumeAvailableCapacityKey);

		/* TWO DOORS, ONE SUPERBLOCK: the capacity through the URL and through NSFileManager's own
		 * file-system attributes must be the same number. */
		check("vol-capacity-is-the-filesystems-and-both-doors-agree",
		      total > 0 && available >= 0 && available <= total &&
		      total == [fn_lookup(fs, NSFileSystemSize) longLongValue] &&
		      available == [fn_lookup(fs, NSFileSystemFreeSize) longLongValue],
		      [NSString stringWithFormat:@"total=%lld available=%lld nsfilemanager=%@/%lld",
			total, available, fn_lookup(fs, NSFileSystemSize),
			(long long)[fn_lookup(fs, NSFileSystemFreeSize) longLongValue]]);
	}

	{
		NSURL *file = [NSURL fileURLWithPath:fn_path(@"inner/note.txt")];
		int local = fn_bool_key(file, NSURLVolumeIsLocalKey);
		int readOnly = fn_bool_key(file, NSURLVolumeIsReadOnlyKey);

		check("vol-is-local-and-writable-here", local == 1 && readOnly == 0,
		      [NSString stringWithFormat:@"local=%d readOnly=%d (the temp tree lives on the root "
			@"volume, which is writable)", local, readOnly]);
	}

	{
		/* THREE CLAIMS ABOUT THE VOLUME, EACH ACCOMPANIED BY SOMETHING THE FIXTURE DOES THAT COULD
		 * ONLY WORK IF THE CLAIM HELD - because a volume's claimed capabilities are the worst place
		 * for a confident wrong answer. */
		NSURL *file = [NSURL fileURLWithPath:fn_path(@"inner/note.txt")];
		NSURL *again = [NSURL fileURLWithPath:fn_path(@"inner/note.txt")];
		NSURL *upper = [NSURL fileURLWithPath:fn_path(@"inner/MIXED")];
		NSURL *lower = [NSURL fileURLWithPath:fn_path(@"inner/mixed")];
		id firstId = nil, secondId = nil;

		/* CASE-SENSITIVE: the volume says so, and the fixture then keeps TWO NAMES THAT DIFFER ONLY
		 * IN CASE as two distinct files with two distinct inodes. */
		fn_write(fn_path(@"inner/MIXED"), "1");
		fn_write(fn_path(@"inner/mixed"), "2");
		{
			struct stat a, b;
			BOOL bothExist = lstat([[upper path] UTF8String], &a) == 0 &&
					 lstat([[lower path] UTF8String], &b) == 0;

			check("vol-supports-case-sensitive-names-and-the-proof",
			      fn_bool_key(file, NSURLVolumeSupportsCaseSensitiveNamesKey) == 1 &&
			      bothExist && a.st_ino != b.st_ino,
			      [NSString stringWithFormat:@"claim=%d both names exist=%d inodes=%llu/%llu",
				fn_bool_key(file, NSURLVolumeSupportsCaseSensitiveNamesKey), (int)bothExist,
				(unsigned long long)a.st_ino, (unsigned long long)b.st_ino]);
		}

		/* PERSISTENT IDS: the claim is that the identifier survives, and the proof is two URL objects
		 * built independently for the same path answering the SAME identifier. */
		[file getResourceValue:&firstId forKey:NSURLFileResourceIdentifierKey error:NULL];
		[again getResourceValue:&secondId forKey:NSURLFileResourceIdentifierKey error:NULL];
		check("vol-supports-persistent-ids-and-the-proof",
		      fn_bool_key(file, NSURLVolumeSupportsPersistentIDsKey) == 1 &&
		      firstId != nil && [firstId isEqual:secondId],
		      [NSString stringWithFormat:@"claim=%d first=%@ second=%@",
			fn_bool_key(file, NSURLVolumeSupportsPersistentIDsKey), firstId, secondId]);

		/* SYMBOLIC LINKS: the claim, plus the link the fixture made at the start resolving to the
		 * file it names - which is what "supports symbolic links" has to mean to be worth anything. */
		{
			char target[64];
			ssize_t got = readlink([fn_path(@"inner/link") UTF8String], target, sizeof(target) - 1);

			if (got > 0) {
				target[got] = '\0';
			}
			check("vol-supports-symbolic-links-and-the-proof",
			      fn_bool_key(file, NSURLVolumeSupportsSymbolicLinksKey) == 1 && got > 0 &&
			      strcmp(target, "note.txt") == 0,
			      [NSString stringWithFormat:@"claim=%d the fixture's link reads back as %s",
				fn_bool_key(file, NSURLVolumeSupportsSymbolicLinksKey), got > 0 ? target : "(nothing)"]);
		}
	}

	{
		NSError *cleanupError = nil;
		BOOL removed = [manager removeItemAtPath:root error:&cleanupError];

		check("probe-tree-removed", removed && ![manager fileExistsAtPath:root],
		      cleanupError != nil ? [cleanupError localizedDescription] : @"the tree is still there");
	}

	printf("FOUNDATION-URLRESOURCEVALUES RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-URLRESOURCEVALUES DONE\n");
	return failc == 0 ? 0 : 1;
}
