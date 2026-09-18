/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * nsprocessinfo.m — the running program describing itself (F13.13). MANUAL OWNERSHIP.
 *
 * WHERE THE PROCESS FILES ARE, and this is the whole of what this file gets wrong the first time:
 * FNX mounts procfs at /System/Processes — the FSH spelling — not at /proc, which is where a
 * Linux-shaped mount would put it. The kernel's own procfs has a "self" symlink and per-pid "comm"
 * and "cmdline" entries, so this file asks in that order and falls back to the Linux paths, because
 * a library that only works when the mount point matches its author's memory is a library that
 * breaks on the next machine.
 *
 * THE OTHER SOURCES: getpid(2); gethostname(2), the name init sets at boot from the network
 * configuration; sysconf(3) for the processor counts; sysinfo(2) for physical memory, with sysconf
 * behind it, because the syscall is the one this kernel implements; and CLOCK_MONOTONIC for uptime,
 * which is the uptime on a system with no suspend.
 *
 * TWO NAMES THAT ARE OURS AND NOT COCOA'S. -operatingSystemName answers
 * NSArgentumOperatingSystem: Cocoa's constants name Mach, Windows NT and their relatives, and
 * returning one of those from this system would be the kind of lie a version string is cheap to
 * avoid. -operatingSystemVersionString is then the name and the version, and
 * -isOperatingSystemAtLeastVersion: compares against THIS system's version.
 *
 * AN ENVIRONMENT ENTRY THAT IS NOT UTF-8 IS SKIPPED rather than answered as a nil inside a
 * dictionary, which is the one place a caller could not tell "absent" from "unrepresentable".
 */

#import <foundation/NSProcessInfo.h>
#import <foundation/NSArray.h>
#import <foundation/NSDictionary.h>
#import <foundation/NSString.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* THIS SYSTEM'S VERSION, in one place, so the string and the comparison cannot disagree. */
#define FN_OS_MAJOR 1
#define FN_OS_MINOR 0
#define FN_OS_PATCH 0

extern char **environ;

static NSProcessInfo *fn_shared_process_info = nil;

/*
 * ONE ENTRY OF THE PROCESS DIRECTORY, from the first path that HAS it.
 *
 * `self` FIRST because the kernel resolves it, so the library does not have to spell the pid; the
 * pid form second because a procfs without the symlink still has the directory; and the Linux paths
 * last, so a machine that mounts /proc is served too. Reading is all-or-nothing per path: an empty
 * file is treated as "not this one" and the search continues.
 */
static size_t fn_read_process_file(const char *leaf, char *buffer, size_t size)
{
	static const char *forms[] = {
		"/System/Processes/self/%s",
		"/System/Processes/%d/%s",
		"/proc/self/%s",
		"/proc/%d/%s"
	};
	char path[256];
	size_t i;

	buffer[0] = '\0';
	for (i = 0; i < sizeof(forms) / sizeof(forms[0]); i++) {
		FILE *file;
		size_t n;

		if (i % 2 == 0) {
			snprintf(path, sizeof(path), forms[i], leaf);
		} else {
			snprintf(path, sizeof(path), forms[i], (int)getpid(), leaf);
		}
		file = fopen(path, "rb");
		if (file == NULL) {
			continue;
		}
		n = fread(buffer, 1, size - 1, file);
		fclose(file);
		buffer[n] = '\0';
		if (n > 0) {
			return n;
		}
	}
	buffer[0] = '\0';
	return 0;
}

/* A SMALL TEXT FILE'S CONTENTS, trimmed: the kernel terminates `comm` with a newline. */
static NSString *fn_trimmed_process_string(const char *leaf)
{
	char buffer[256];
	size_t n = fn_read_process_file(leaf, buffer, sizeof(buffer));
	NSUInteger end = n;

	while (end > 0 && (buffer[end - 1] == '\n' || buffer[end - 1] == '\r' ||
			   buffer[end - 1] == ' ' || buffer[end - 1] == '\t')) {
		end--;
	}
	buffer[end] = '\0';
	return end > 0 ? [NSString stringWithUTF8String:buffer] : nil;
}

@implementation NSProcessInfo

+ (NSProcessInfo *)processInfo
{
	if (fn_shared_process_info == nil) {
		fn_shared_process_info = [[NSProcessInfo alloc] init];
	}
	return fn_shared_process_info;
}

- (int)processIdentifier
{
	return (int)getpid();
}

- (NSString *)processName
{
	NSString *stored = [self fnStoredName];

	if (stored != nil) {
		return stored;
	}
	{
		NSString *fromKernel = fn_trimmed_process_string("comm");

		return fromKernel != nil ? fromKernel : @"";
	}
}

- (void)setProcessName:(NSString *)name
{
	[self fnSetStoredName:name];
}

- (NSArray *)arguments
{
	NSMutableArray *arguments = [NSMutableArray array];
	char buffer[4096];
	size_t n = fn_read_process_file("cmdline", buffer, sizeof(buffer));

	/* NUL-SEPARATED, and that is the kernel's format rather than a choice: an argument may hold any
	 * byte but NUL, so NUL is the only separator that cannot be confused with data. */
	{
		size_t start = 0;
		size_t i;

		for (i = 0; i < n; i++) {
			if (buffer[i] == '\0') {
				if (i > start) {
					NSString *argument = [NSString stringWithUTF8String:buffer + start];

					if (argument != nil) {
						[arguments addObject:argument];
					}
				}
				start = i + 1;
			}
		}
	}
	return arguments;
}

- (NSDictionary *)environment
{
	NSMutableDictionary *environment = [NSMutableDictionary dictionary];
	char **entry;

	if (environ == NULL) {
		return environment;
	}
	for (entry = environ; *entry != NULL; entry++) {
		const char *equals = strchr(*entry, '=');

		if (equals == NULL || equals == *entry) {
			continue;		/* not a NAME=VALUE line, or an empty name */
		}
		{
			size_t nameLength = (size_t)(equals - *entry);
			char *name = malloc(nameLength + 1);
			NSString *key;
			NSString *value;

			if (name == NULL) {
				continue;
			}
			memcpy(name, *entry, nameLength);
			name[nameLength] = '\0';
			key = [NSString stringWithUTF8String:name];
			value = [NSString stringWithUTF8String:equals + 1];
			free(name);
			/* A NAME OR VALUE THAT IS NOT UTF-8 IS SKIPPED, which the header says. */
			if (key != nil && value != nil) {
				[environment setObject:value forKey:key];
			}
		}
	}
	return environment;
}

- (NSString *)hostName
{
	char buffer[256];

	if (gethostname(buffer, sizeof(buffer) - 1) != 0) {
		return @"localhost";
	}
	buffer[sizeof(buffer) - 1] = '\0';
	{
		NSString *name = [NSString stringWithUTF8String:buffer];

		return name != nil ? name : @"localhost";
	}
}

- (NSString *)globallyUniqueString
{
	static unsigned long counter = 0;
	struct timeval now;
	NSString *name = [self processName];

	counter++;
	gettimeofday(&now, NULL);
	/* HOST, PROCESS, TIME AND A COUNTER: any one of them can repeat, and together they do not within
	 * a process's life and across two processes on one host at the same instant. */
	return [NSString stringWithFormat:@"%@-%d-%ld-%lu-%lu", name, [self processIdentifier],
				 (long)now.tv_sec, (unsigned long)now.tv_usec, counter];
}

- (NSString *)operatingSystemName
{
	return @"NSArgentumOperatingSystem";
}

- (NSOperatingSystemVersion)operatingSystemVersion
{
	NSOperatingSystemVersion version;

	version.majorVersion = FN_OS_MAJOR;
	version.minorVersion = FN_OS_MINOR;
	version.patchVersion = FN_OS_PATCH;
	return version;
}

- (NSString *)operatingSystemVersionString
{
	return [NSString stringWithFormat:@"%@ %d.%d.%d", [self operatingSystemName],
				  (int)FN_OS_MAJOR, (int)FN_OS_MINOR, (int)FN_OS_PATCH];
}

- (BOOL)isOperatingSystemAtLeastVersion:(NSOperatingSystemVersion)version
{
	NSOperatingSystemVersion ours = [self operatingSystemVersion];

	if (ours.majorVersion != version.majorVersion) {
		return ours.majorVersion > version.majorVersion;
	}
	if (ours.minorVersion != version.minorVersion) {
		return ours.minorVersion > version.minorVersion;
	}
	return ours.patchVersion >= version.patchVersion;
}

- (NSUInteger)processorCount
{
	long count = sysconf(_SC_NPROCESSORS_ONLN);

	return count > 0 ? (NSUInteger)count : 1;
}

- (NSUInteger)activeProcessorCount
{
	return [self processorCount];
}

- (unsigned long long)physicalMemory
{
	struct sysinfo info;

	/* THE SYSCALL FIRST, because it is the one THIS kernel implements; sysconf is the portable
	 * fallback for a libc whose sysconf knows the answer without it. A `mem_unit` OF ZERO MEANS
	 * BYTES: this kernel reports totals in bytes and used to leave the unit field at 0, so a reader
	 * that multiplied blindly got zero — and musl's sysconf, which divides by it, did not do better.
	 * The kernel sets it now; this stays for a kernel that has not been rebuilt. */
	if (sysinfo(&info) == 0 && info.totalram > 0) {
		unsigned long long unit = info.mem_unit > 0 ? (unsigned long long)info.mem_unit : 1;

		return (unsigned long long)info.totalram * unit;
	}
	{
		long pages = sysconf(_SC_PHYS_PAGES);
		long pageSize = sysconf(_SC_PAGESIZE);

		if (pages <= 0 || pageSize <= 0) {
			return 0;
		}
		return (unsigned long long)pages * (unsigned long long)pageSize;
	}
}

- (NSTimeInterval)systemUptime
{
	struct timespec now;

	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
		return 0;
	}
	return (NSTimeInterval)now.tv_sec + ((NSTimeInterval)now.tv_nsec / 1000000000.0);
}

/* THE STORED NAME, in one file-static: NSProcessInfo is a singleton, so that IS its whole state. */
static NSString *fn_stored_name = nil;

- (nullable NSString *)fnStoredName
{
	return fn_stored_name;
}

- (void)fnSetStoredName:(nullable NSString *)name
{
	fn_stored_name = name;
}

@end
