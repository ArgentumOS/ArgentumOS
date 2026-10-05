/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSProcessInfo.m — the running program describing itself (F13.13). MANUAL OWNERSHIP.
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

#import <Foundation/NSProcessInfo.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSNumber.h>	/* -unsignedIntegerValue: the termination counters' type (without it clang reads 'assigning to NSUInteger from id') */
#import <Foundation/NSString.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pwd.h>		/* getpwuid(3): the account NAMES, the same source NSFileManager's NSUserName() reads */

/* THIS SYSTEM'S VERSION, in one place, so the string and the comparison cannot disagree. */
#define FN_OS_MAJOR 1
#define FN_OS_MINOR 0
#define FN_OS_PATCH 0

extern char **environ;

static NSProcessInfo *fn_shared_process_info = nil;

/* THE ACTIVITY AND TERMINATION STATE, static for the same reason the shared instance is: this class is a
 * singleton over process-wide facts, and what follows describes THE PROCESS rather than an object. */
static NSMutableDictionary *fn_termination_reasons = nil;	/* reason -> count */
static NSUInteger fn_sudden_termination_disabled = 0;		/* a count: enables balance disables */
static BOOL fn_automatic_termination_support_enabled = NO;

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

/* THE ACTIVITY TOKEN, private: what a caller gets back is opaque by contract, and the only thing this
 * library does with it is recognise it in -endActivity:. */
@interface FNActivity : NSObject
{
@public
	NSActivityOptions options;
	NSString *reason;
}
@end

@implementation FNActivity
- (NSString *)description
{
	return [NSString stringWithFormat:@"<NSActivity: %lu %@>", (unsigned long)options, reason];
}
@end

@implementation NSProcessInfo
- (id<NSObject>)beginActivityWithOptions:(NSActivityOptions)options reason:(NSString *)reason
{
	FNActivity *activity = [[FNActivity alloc] init];

	activity->options = options;
	activity->reason = [reason copy];
	return [activity autorelease];
}

- (void)endActivity:(id<NSObject>)activity
{
	/* A TOKEN THIS LIBRARY DID NOT HAND OUT IS IGNORED rather than fatal: the contract is that a caller
	 * returns what it was given, and there is no state here whose bookkeeping a foreign object could break. */
	if (![activity isKindOfClass:[FNActivity class]]) {
		return;
	}
	[(FNActivity *)activity release];
}

- (void)performActivityWithOptions:(NSActivityOptions)options
			    reason:(NSString *)reason
			usingBlock:(void (^)(void))block
{
	id<NSObject> activity = [self beginActivityWithOptions:options reason:reason];

	if (block != NULL) {
		block();
	}
	[self endActivity:activity];
}

- (void)performExpiringActivityWithReason:(NSString *)reason usingBlock:(void (^)(BOOL expired))block
{
	id<NSObject> activity = [self beginActivityWithOptions:0 reason:reason];

	if (block != NULL) {
		block(NO);	/* nothing is trying to suspend this process, so nothing has expired */
	}
	[self endActivity:activity];
}

- (void)disableAutomaticTermination:(NSString *)reason
{
	NSUInteger count;

	if (reason == nil) {
		return;
	}
	if (fn_termination_reasons == nil) {
		fn_termination_reasons = [[NSMutableDictionary alloc] init];
	}
	count = [[fn_termination_reasons objectForKey:reason] unsignedIntegerValue];
	[fn_termination_reasons setObject:[NSNumber numberWithUnsignedInteger:count + 1] forKey:reason];
}

- (void)enableAutomaticTermination:(NSString *)reason
{
	NSUInteger count;

	if (reason == nil) {
		return;
	}
	count = [[fn_termination_reasons objectForKey:reason] unsignedIntegerValue];
	if (count <= 1) {
		[fn_termination_reasons removeObjectForKey:reason];
	} else {
		[fn_termination_reasons setObject:[NSNumber numberWithUnsignedInteger:count - 1] forKey:reason];
	}
}

- (void)disableSuddenTermination
{
	fn_sudden_termination_disabled++;
}

- (void)enableSuddenTermination
{
	if (fn_sudden_termination_disabled > 0) {
		fn_sudden_termination_disabled--;
	}
}

- (BOOL)automaticTerminationSupportEnabled
{
	return fn_automatic_termination_support_enabled;
}

- (void)setAutomaticTerminationSupportEnabled:(BOOL)flag
{
	fn_automatic_termination_support_enabled = flag;
}

- (BOOL)hasPerformanceProfile:(NSString *)profile
{
	(void)profile;
	return NO;	/* a performance profile is an Apple-silicon mode; nothing here offers one */
}

- (BOOL)isDeviceCertifiedFor:(NSString *)certificationType
{
	(void)certificationType;
	return NO;	/* certification is Apple's, for named uses of a named device */
}

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

/* THE ACCOUNT DATABASE ANSWERS, through the SAME getpwuid(3) NSFileManager's NSUserName()/NSFullUserName()
 * already read - so the two doors cannot disagree. A uid with no entry, or an entry with no full name,
 * answers the EMPTY string rather than nil: the header promises a nonnull string, and "" is how "unknown"
 * is spelled there. */
- (NSString *)userName
{
	struct passwd *pw = getpwuid(getuid());

	if (pw != NULL && pw->pw_name != NULL) {
		NSString *name = [NSString stringWithUTF8String:pw->pw_name];

		if (name != nil) {
			return name;
		}
	}
	return @"";
}

- (NSString *)fullUserName
{
	struct passwd *pw = getpwuid(getuid());

	if (pw != NULL && pw->pw_gecos != NULL && pw->pw_gecos[0] != '\0') {
		NSString *name = [NSString stringWithUTF8String:pw->pw_gecos];

		if (name != nil) {
			return name;
		}
	}
	return @"";
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

/* THE SEVEN NAMES OF THE DOOR'S OWN VOCABULARY (§62.98). Each one's value is the string its name is, which is
 * what that door answers on the system the name describes; NONE of them is this system's answer, and the door
 * below says so by returning a name that is not in this list. */
NSString * const NSMACHOperatingSystem = @"NSMACHOperatingSystem";
NSString * const NSWindowsNTOperatingSystem = @"NSWindowsNTOperatingSystem";
NSString * const NSWindows95OperatingSystem = @"NSWindows95OperatingSystem";
NSString * const NSHPUXOperatingSystem = @"NSHPUXOperatingSystem";
NSString * const NSSolarisOperatingSystem = @"NSSolarisOperatingSystem";
NSString * const NSSunOSOperatingSystem = @"NSSunOSOperatingSystem";
NSString * const NSOSF1OperatingSystem = @"NSOSF1OperatingSystem";

- (NSString *)operatingSystemName
{
	return @"NSArgentumOperatingSystem";
}

- (NSProcessInfoThermalState)thermalState
{
	/* NOTHING HERE MEASURES TEMPERATURE, and answering a changing number would be the lie this class already
	 * refuses to tell about its own name. Nominal is what a system with no thermal management is, and the
	 * notification's own comment records that nothing posts it. */
	return NSProcessInfoThermalStateNominal;
}

/* THE PLATFORM-COMPATIBILITY FLAGS, and each one answers NO because none of the modes EXISTS here - this is a
 * native Argentum process, not a Mac Catalyst app and not an iOS app running on a Mac or a Vision device, and
 * this system has no low-power mode to be in. NO is not a placeholder: it is the true answer to "is this
 * process that kind of app" and "is low-power mode on" on a system where the answer must be no, the same way
 * -thermalState answers Nominal for a system with no thermal management. */
- (BOOL)lowPowerModeEnabled
{
	return NO;
}

- (BOOL)macCatalystApp
{
	return NO;
}

- (BOOL)iOSAppOnMac
{
	return NO;
}

- (BOOL)iOSAppOnVision
{
	return NO;
}

- (NSOperatingSystemVersion)operatingSystemVersion
{
	NSOperatingSystemVersion version;

	version.majorVersion = FN_OS_MAJOR;
	version.minorVersion = FN_OS_MINOR;
	version.patchVersion = FN_OS_PATCH;
	return version;
}

/* THE THREE FIELDS OF THE VERSION, each taken from the ONE struct so the parts and the whole cannot drift. */
- (NSInteger)majorVersion
{
	return [self operatingSystemVersion].majorVersion;
}

- (NSInteger)minorVersion
{
	return [self operatingSystemVersion].minorVersion;
}

- (NSInteger)patchVersion
{
	return [self operatingSystemVersion].patchVersion;
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
