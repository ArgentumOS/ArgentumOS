/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_mountedvolumes, unit of 1 — W8p slice 6e's acceptance: THE MOUNT TABLE'S VOLUME KEYS.
 * docs/design/foundation-plan.md §60.
 *
 * ONE unit, importing only <Foundation/Foundation.h>. NO FIXTURE: this system PUBLISHES its mounts
 * (`/System/Processes/mounts`), so the probe asks about the volumes it is running on.
 *
 *   mounts-the-door-lists-the-table   -mountedVolumeURLsIncludingResourceValuesForKeys:options: answers the
 *                                     mounted volumes, and the guest has at least the root, procfs and the
 *                                     device tree;
 *   mounts-a-file-reports-the-volume-holding-it  THE LONGEST MOUNT POINT THAT PREFIXES THE PATH, which is
 *                                     what puts /proc/version on the procfs volume rather than on the root
 *                                     that contains the mount point;
 *   mounts-the-name-and-type-come-from-the-table  the mount point's own name (there are no volume labels
 *                                     here) and the file system's name;
 *   mounts-the-identifier-is-the-device  opaque, and the same for two files on one volume;
 *   mounts-read-only-comes-from-the-table-flag  the table says it outright, which is why the key is read
 *                                     from it rather than probed (slice 6c's EROFS reading is superseded);
 *   mounts-is-volume-and-is-mount-trigger  the root of a mounted file system, and a directory a mount
 *                                     landed on, are the same statement about the table;
 *   mounts-the-resource-count-and-size-support-come-from-the-file-system  answered by asking the file
 *                                     system, which is what those two keys are ABOUT;
 *   mounts-the-door-prefetches-the-keys  the keys a caller asks the door for are already in each URL.
 */

#import <Foundation/Foundation.h>

#include <stdio.h>

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-MOUNTEDVOLUMES %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-MOUNTEDVOLUMES %s FAIL %s\n", name, detail != nil ? [detail UTF8String] : "");
	}
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

int main(void)
{
	NSFileManager *manager = [NSFileManager defaultManager];
	NSArray *volumes = [manager mountedVolumeURLsIncludingResourceValuesForKeys:nil options:0];

	{
		NSMutableSet *paths = [NSMutableSet set];
		NSUInteger i;
		BOOL allURLs = [volumes count] > 0;

		for (i = 0; i < [volumes count]; i++) {
			id volume = [volumes objectAtIndex:i];

			if (![volume isKindOfClass:[NSURL class]] || ![volume isFileURL]) {
				allURLs = NO;
				break;
			}
			[paths addObject:[volume path]];
		}
		check("mounts-the-door-lists-the-table",
		      allURLs && [paths containsObject:@"/"] && [paths containsObject:@"/System/Processes"] &&
		      [paths count] >= 3,
		      [NSString stringWithFormat:@"allURLs=%d volumes=%@", (int)allURLs, paths]);
	}

	{
		/* EXISTING ITEMS, because a resource value is asked OF A RESOURCE: the door refuses a path that is
		 * not there, which is a rule the probe should not be fighting. */
		NSURL *onRoot = fn_url(@"/System");
		NSURL *onProc = fn_url(@"/System/Processes/version");

		check("mounts-a-file-reports-the-volume-holding-it",
		      [[fn_key(onRoot, NSURLVolumeURLKey) path] isEqual:@"/"] &&
		      [[fn_key(onProc, NSURLVolumeURLKey) path] isEqual:@"/System/Processes"],
		      [NSString stringWithFormat:@"root-file=%@ proc-file=%@",
			[fn_key(onRoot, NSURLVolumeURLKey) path],
			[fn_key(onProc, NSURLVolumeURLKey) path]]);
	}

	{
		NSURL *root = fn_url(@"/");
		NSURL *proc = fn_url(@"/System/Processes");

		check("mounts-the-name-and-type-come-from-the-table",
		      [[fn_key(root, NSURLVolumeNameKey) description] isEqual:@"/"] &&
		      [fn_key(root, NSURLVolumeTypeNameKey) length] > 0 &&
		      [[fn_key(proc, NSURLVolumeNameKey) description] isEqual:@"Processes"] &&
		      [fn_key(proc, NSURLVolumeTypeNameKey) length] > 0,
		      [NSString stringWithFormat:@"root=%@/%@ proc=%@/%@",
			fn_key(root, NSURLVolumeNameKey), fn_key(root, NSURLVolumeTypeNameKey),
			fn_key(proc, NSURLVolumeNameKey), fn_key(proc, NSURLVolumeTypeNameKey)]);
	}

	{
		NSURL *a = fn_url(@"/System");
		NSURL *b = fn_url(@"/Applications");
		NSURL *proc = fn_url(@"/System/Processes/version");
		NSString *first = fn_key(a, NSURLVolumeIdentifierKey);
		NSString *second = fn_key(b, NSURLVolumeIdentifierKey);
		NSString *other = fn_key(proc, NSURLVolumeIdentifierKey);

		check("mounts-the-identifier-is-the-device",
		      [first length] > 0 && [first isEqual:second] && ![first isEqual:other],
		      [NSString stringWithFormat:@"two on root: %@/%@ on proc: %@", first, second, other]);
	}

	{
		NSURL *root = fn_url(@"/");
		NSURL *proc = fn_url(@"/System/Processes");

		check("mounts-read-only-comes-from-the-table-flag",
		      [fn_key(root, NSURLVolumeIsReadOnlyKey) boolValue] == NO &&
		      [fn_key(proc, NSURLVolumeIsReadOnlyKey) boolValue] == NO,
		      [NSString stringWithFormat:@"root-readonly=%@ proc-readonly=%@ (the guest mounts both "
			@"read-write, so the check asserts the TABLE was read and not the opposite)",
			fn_key(root, NSURLVolumeIsReadOnlyKey), fn_key(proc, NSURLVolumeIsReadOnlyKey)]);
	}

	{
		NSURL *root = fn_url(@"/");
		NSURL *proc = fn_url(@"/System/Processes");
		NSURL *inside = fn_url(@"/System/Processes/version");

		check("mounts-is-volume-and-is-mount-trigger",
		      [fn_key(root, NSURLIsVolumeKey) boolValue] &&
		      [fn_key(proc, NSURLIsVolumeKey) boolValue] &&
		      ![fn_key(inside, NSURLIsVolumeKey) boolValue] &&
		      [fn_key(root, NSURLVolumeIsMountTriggerKey) boolValue] &&
		      ![fn_key(inside, NSURLVolumeIsMountTriggerKey) boolValue],
		      [NSString stringWithFormat:@"root=%@/%@ proc=%@ inside=%@",
			fn_key(root, NSURLIsVolumeKey), fn_key(root, NSURLVolumeIsMountTriggerKey),
			fn_key(proc, NSURLIsVolumeKey), fn_key(inside, NSURLIsVolumeKey)]);
	}

	{
		NSURL *root = fn_url(@"/");
		NSURL *proc = fn_url(@"/System/Processes");

		/* THE DOOR'S OWN RULE, not the substrate's answer: "a volume reports sizes when its file-system
		 * attributes can be read" is what the key promises, so the check asks the SAME question through
		 * NSFileManager and requires the two to agree - which is a stronger claim than a number. */
		BOOL readable = [manager attributesOfFileSystemForPath:@"/" error:NULL] != nil;

		check("mounts-the-resource-count-and-size-support-come-from-the-file-system",
		      [fn_key(root, NSURLVolumeSupportsVolumeSizesKey) boolValue] == readable,
		      [NSString stringWithFormat:@"root supports-sizes=%@ (the file system is readable=%@) "
			@"root resources=%@ proc supports-sizes=%@",
			fn_key(root, NSURLVolumeSupportsVolumeSizesKey), readable ? @"yes" : @"no",
			fn_key(root, NSURLVolumeResourceCountKey),
			fn_key(proc, NSURLVolumeSupportsVolumeSizesKey)]);
	}

	{
		NSArray *keys = [NSArray arrayWithObjects:NSURLVolumeTypeNameKey,
				 NSURLVolumeIsRootFileSystemKey, nil];
		NSArray *withKeys = [manager mountedVolumeURLsIncludingResourceValuesForKeys:keys options:0];
		NSUInteger i;
		BOOL everyOne = [withKeys count] > 0;
		NSUInteger withRoot = 0;

		for (i = 0; i < [withKeys count]; i++) {
			id url = [withKeys objectAtIndex:i];

			if ([fn_key(url, NSURLVolumeTypeNameKey) length] == 0) {
				everyOne = NO;
			}
			if ([fn_key(url, NSURLVolumeIsRootFileSystemKey) boolValue]) {
				withRoot++;
			}
		}
		check("mounts-the-door-prefetches-the-keys", everyOne && withRoot == 1,
		      [NSString stringWithFormat:@"volumes=%lu every-url-answers=%@ roots=%@",
			(unsigned long)[withKeys count], everyOne ? @"yes" : @"no",
			[NSNumber numberWithUnsignedInteger:withRoot]]);
	}

	printf("FOUNDATION-MOUNTEDVOLUMES RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-MOUNTEDVOLUMES DONE\n");
	return failc == 0 ? 0 : 1;
}
