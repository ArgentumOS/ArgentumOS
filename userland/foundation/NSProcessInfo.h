/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSProcessInfo — the running program describing itself. F13.13, docs/design/foundation-plan.md §10.
 *
 * WHERE EACH ANSWER COMES FROM, because a service's honesty is its sources:
 *   -processIdentifier  getpid(2);
 *   -arguments          /proc/self/cmdline, NUL-separated — the kernel's copy of argv, which a
 *                       libc does not have to keep for us;
 *   -processName        /proc/self/comm, the name the KERNEL knows this process by, unless a caller
 *                       has set one — Cocoa's `-setProcessName:` is a stored override too;
 *   -environment        the `environ` the C library maintains;
 *   -hostName           gethostname(2), which is the same name init sets at boot from the network
 *                       configuration;
 *   -processorCount/-activeProcessorCount/-physicalMemory  sysconf(3);
 *   -systemUptime       CLOCK_MONOTONIC, which is the uptime on a system with no suspend.
 *
 * TWO NAMES THAT ARE OURS AND NOT COCOA'S. `-operatingSystemName` answers
 * `NSArgentumOperatingSystem`: Cocoa's constants name Mach, Windows NT and their relatives, and
 * returning one of those from this system would be the kind of lie a version string is cheap to
 * avoid. `-operatingSystemVersionString` is then the name and the version, and
 * `-isOperatingSystemAtLeastVersion:` compares against THIS system's version rather than a
 * simulated one.
 *
 * AN ENVIRONMENT ENTRY THAT IS NOT UTF-8 IS SKIPPED rather than answered as a nil inside a
 * dictionary, which is the one place a caller could not tell "absent" from "unrepresentable".
 */

#ifndef FOUNDATION_NSPROCESSINFO_H
#define FOUNDATION_NSPROCESSINFO_H

#import <foundation/NSObject.h>
/* NSTimeInterval IS DECLARED IN NSDate.h, and this header names it as a return type — so the import
 * is a dependency rather than a convenience, exactly as -valueForKeyPath: made KVC one for the
 * expression file. */
#import <foundation/NSDate.h>

@class NSArray;
@class NSDictionary;
@class NSString;

NS_ASSUME_NONNULL_BEGIN

/* Cocoa's shape, including the field names. */
typedef struct {
	NSInteger majorVersion;
	NSInteger minorVersion;
	NSInteger patchVersion;
} NSOperatingSystemVersion;

@interface NSProcessInfo : NSObject

+ (NSProcessInfo *)processInfo;

- (NSArray *)arguments;
- (NSDictionary *)environment;
- (int)processIdentifier;
- (NSString *)processName;
- (void)setProcessName:(NSString *)name;
- (NSString *)globallyUniqueString;
- (NSString *)hostName;
- (NSString *)operatingSystemName;
- (NSString *)operatingSystemVersionString;
- (NSOperatingSystemVersion)operatingSystemVersion;
- (BOOL)isOperatingSystemAtLeastVersion:(NSOperatingSystemVersion)version;
- (NSUInteger)processorCount;
- (NSUInteger)activeProcessorCount;
- (unsigned long long)physicalMemory;
- (NSTimeInterval)systemUptime;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSPROCESSINFO_H */
