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
 * AND APPLE'S SEVEN NAMES ARE DECLARED ANYWAY (§62.98), because they are the VOCABULARY OF THAT DOOR rather
 * than claims about this system: a program that compares `-operatingSystemName` against
 * `NSMACHOperatingSystem` or `NSWindowsNTOperatingSystem` has to be able to spell them, and each one's value is
 * the string its own name is — which is what the door answers on the systems they name. NONE of them is ever
 * this system's answer, and the probe asserts both halves: the seven values, and that this system answers a
 * name outside the seven.
 *
 * AN ENVIRONMENT ENTRY THAT IS NOT UTF-8 IS SKIPPED rather than answered as a nil inside a
 * dictionary, which is the one place a caller could not tell "absent" from "unrepresentable".
 */

#ifndef FOUNDATION_NSPROCESSINFO_H
#define FOUNDATION_NSPROCESSINFO_H

#import <Foundation/NSObject.h>
/* NSTimeInterval IS DECLARED IN NSDate.h, and this header names it as a return type — so the import
 * is a dependency rather than a convenience, exactly as -valueForKeyPath: made KVC one for the
 * expression file. */
#import <Foundation/NSDate.h>

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

/* THE THERMAL STATE BELONGS BEFORE THE CLASS, and it MOVED here in §62.98 for a reason worth keeping: the enum
 * used to sit below the interface with no door that could name it, so `-thermalState` was unwritable — an enum
 * whose only reader would have been a forward declaration. Apple declares it before the class too. */
typedef enum {
	NSProcessInfoThermalStateNominal = 0,
	NSProcessInfoThermalStateFair = 1,
	NSProcessInfoThermalStateSerious = 2,
	NSProcessInfoThermalStateCritical = 3
} NSProcessInfoThermalState;

/* How much a long-running activity is allowed to disturb the machine, and how hot the
 * process is (2026-09-20). NSActivityOptions is a bit set — thirteen bits of "do not
 * sleep", "do not terminate", and what kind of work this is. Names from Apple's
 * documentation index; values are ours (§11.6.1 D2, see NSFileManager.h). */
typedef enum {
	NSActivityIdleDisplaySleepDisabled = 1 << 0,
	NSActivityIdleSystemSleepDisabled = 1 << 1,
	NSActivitySuddenTerminationDisabled = 1 << 2,
	NSActivityAutomaticTerminationDisabled = 1 << 3,
	NSActivityUserInitiated = 1 << 4,
	NSActivityUserInteractive = 1 << 5,
	NSActivityUserInitiatedAllowingIdleSystemSleep = 1 << 6,
	NSActivityBackground = 1 << 7,
	NSActivityLatencyCritical = 1 << 8,
	NSActivityAnimationTrackingEnabled = 1 << 9,
	NSActivityTrackingEnabled = 1 << 10
} NSActivityOptions;

@interface NSProcessInfo : NSObject

+ (NSProcessInfo *)processInfo;

- (NSArray *)arguments;
- (NSDictionary *)environment;
- (int)processIdentifier;
- (NSString *)processName;
- (void)setProcessName:(NSString *)name;
/* THE USER THE PROCESS RUNS AS: the account database's own answers, which NSFileManager's NSUserName() and
 * NSFullUserName() already read through getpwuid(3). The SAME entries, so the two doors cannot disagree. A
 * uid the database does not know, or an entry with no full name, leaves the name empty rather than nil —
 * the nonnull shape this header promises for a string property. */
- (NSString *)userName;
- (NSString *)fullUserName;
- (NSString *)globallyUniqueString;
- (NSString *)hostName;
- (NSString *)operatingSystemName;
/* THE STATE THE ENUM ABOVE DESCRIBES HAD NO DOOR UNTIL §62.98, which is how the enum sat here orphaned.
 * ANSWERED AS Nominal, and the boundary is stated rather than implied: no thermal sensor is plumbed into this
 * library, so the state never changes and the notification that pairs with it is never posted by anything here.
 * A caller that reads the property or observes the name compiles and gets a truthful answer - "nominal" is what
 * a system with no thermal management is. */
- (NSProcessInfoThermalState)thermalState;
/* THE PLATFORM-COMPATIBILITY FLAGS, and NONE of them names a mode this OS HAS: the process is a native
 * Argentum process, not a Mac Catalyst app and not an iOS app running on a Mac or a Vision device, and this
 * system has no low-power mode to be in. So each answers its own NO — the honest state of a system that never
 * enters the mode, the same choice -thermalState makes for a system with no thermal management. Apple spells
 * the accessors with an `is` prefix (isLowPowerModeEnabled, isMacCatalystApp, isiOSAppOnMac, isiOSAppOnVision);
 * each door here is named by its PROPERTY name, which is the selector the ledger records. */
- (BOOL)lowPowerModeEnabled;
- (BOOL)macCatalystApp;
- (BOOL)iOSAppOnMac;
- (BOOL)iOSAppOnVision;
- (NSString *)operatingSystemVersionString;
- (NSOperatingSystemVersion)operatingSystemVersion;
/* THE VERSION COMPONENTS, one field each, so a caller need not take the struct apart. They are the SAME three
 * numbers -operatingSystemVersion answers, read from the one place this file keeps them. */
- (NSInteger)majorVersion;
- (NSInteger)minorVersion;
- (NSInteger)patchVersion;
- (BOOL)isOperatingSystemAtLeastVersion:(NSOperatingSystemVersion)version;
- (NSUInteger)processorCount;
- (NSUInteger)activeProcessorCount;
- (unsigned long long)physicalMemory;
- (NSTimeInterval)systemUptime;

/* ACTIVITIES (§63.181). An activity is a PROMISE to the system that work is in progress — do not
 * idle-sleep the machine, do not terminate the app — and what is handed back is an OPAQUE TOKEN whose only
 * job is to be given to -endActivity:. The promise is RECORDED here, and there is nothing on this system to
 * keep it with: no idle sleep to prevent and no app-napping to defer. Recorded rather than pretended,
 * because a caller can still measure the SHAPE — an activity begins, the block runs, the activity ends —
 * and -endActivity: with a token this library did not hand out is ignored rather than fatal. */
- (id<NSObject>)beginActivityWithOptions:(NSActivityOptions)options reason:(NSString *)reason;
- (void)endActivity:(id<NSObject>)activity;
- (void)performActivityWithOptions:(NSActivityOptions)options
			    reason:(NSString *)reason
			usingBlock:(void (^)(void))block;
- (void)performExpiringActivityWithReason:(NSString *)reason
			       usingBlock:(void (^)(BOOL expired))block;

/* AUTOMATIC AND SUDDEN TERMINATION. The "enable" doors BALANCE the "disable" ones, so what is kept is a
 * COUNT — per reason for automatic termination, one counter for sudden termination. The support flag is the
 * one part of this group with a getter, which is why it can be asserted rather than hoped for. */
- (void)disableAutomaticTermination:(NSString *)reason;
- (void)enableAutomaticTermination:(NSString *)reason;
- (void)disableSuddenTermination;
- (void)enableSuddenTermination;
- (BOOL)automaticTerminationSupportEnabled;
- (void)setAutomaticTerminationSupportEnabled:(BOOL)flag;

/* THE TWO HARDWARE QUESTIONS, and both ask about a Mac: a performance profile is an Apple-silicon
 * performance mode, and a certified device is one Apple has certified for a named use. Neither exists on
 * this system — and NO is the answer rather than a raised door, because the question IS answerable. */
- (BOOL)hasPerformanceProfile:(NSString *)profile;
- (BOOL)isDeviceCertifiedFor:(NSString *)certificationType;

@end



/* THE SEVEN NAMES OF `-operatingSystemName`'s VOCABULARY, in Apple's spelling and with the value each one has on
 * the system its name describes. This system answers its own name (never one of these) — the note at the top of
 * this header says why — so a program that knows about macOS, Windows or the other relatives can still write the
 * comparison it was written to write. */
extern NSString * const NSMACHOperatingSystem;
extern NSString * const NSWindowsNTOperatingSystem;
extern NSString * const NSWindows95OperatingSystem;
extern NSString * const NSHPUXOperatingSystem;
extern NSString * const NSSolarisOperatingSystem;
extern NSString * const NSSunOSOperatingSystem;
extern NSString * const NSOSF1OperatingSystem;

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSPROCESSINFO_H */
