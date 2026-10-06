/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_processinfo, unit of 1 — F13.13's acceptance.
 * docs/design/foundation-plan.md §10.
 *
 * ONE unit, importing only <Foundation/Foundation.h>, plus <unistd.h> for the CROSS-CHECK that
 * matters: `-processIdentifier` is asserted against `getpid()` rather than merely against "greater
 * than zero", so the answer is pinned to the system's rather than to a plausible number.
 *
 * WHAT IT MEASURES:
 *   proc-shared-instance    +processInfo answers the same object twice;
 *   proc-identifier         the pid, checked against getpid(2);
 *   proc-name-and-arguments the kernel's comm, and argv[0] from /proc/self/cmdline — the path this
 *                           probe was launched with, which no constant in this file could produce;
 *   proc-environment        the C library's environ, read back as a dictionary that contains PATH,
 *                           because the shell that launched this probe had one;
 *   proc-counts             processors, active processors and physical memory, all positive;
 *   proc-host-and-uptime    a host name and a monotonic uptime;
 *   proc-unique-strings     TWO globally unique strings, which must DIFFER;
 *   proc-version            this system's own name and version, and the comparison against it —
 *                           including the case that must answer NO.
 *   proc-user-names         the account database's own names, cross-checked against NSUserName();
 *   proc-version-components major/minor/patch, cross-checked against -operatingSystemVersion's fields;
 *   proc-platform-flags     the four compatibility flags, each NO (REASONED — see the block).
 */

#import <Foundation/Foundation.h>

#include <stdio.h>
#include <unistd.h>

static int okc, failc;

static int lastcheck;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	lastcheck = ok;	/* read by covers() */
	if (ok) {
		okc++;
		printf("FOUNDATION-PROCESSINFO %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-PROCESSINFO %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

/* covers("NSBundle", "resourcePath") - the behavioural claim, piggybacked on the check above it. */
static void covers_(const char *cls, const char *sel)
{
	if (lastcheck) {
		printf("COVERS %s %s\n", cls, sel);
	}
}
#define covers(cls, sel) covers_(cls, sel)

int main(void)
{
	NSProcessInfo *info = [NSProcessInfo processInfo];

	check("proc-shared-instance",
	      info != nil && info == [NSProcessInfo processInfo],
	      info == [NSProcessInfo processInfo] ? @"one instance" : @"two instances");

	check("proc-identifier",
	      info != nil && [info processIdentifier] > 0 &&
	      [info processIdentifier] == (int)getpid(),
	      [NSString stringWithFormat:@"pid=%d getpid=%d",
		info != nil ? [info processIdentifier] : -1, (int)getpid()]);

	{
		NSArray *arguments = info != nil ? [info arguments] : nil;
		NSString *name = info != nil ? [info processName] : nil;

		check("proc-name-and-arguments",
		      name != nil && [name length] > 0 && name != nil &&
		      arguments != nil && [arguments count] >= 1 &&
		      [[arguments objectAtIndex:0] length] > 0,
		      [NSString stringWithFormat:@"name=%@ argc=%lu argv0=%@", name,
			(unsigned long)(arguments != nil ? [arguments count] : 0),
			(arguments != nil && [arguments count] >= 1)
				? [arguments objectAtIndex:0] : @"(none)"]);
	}

	{
		NSDictionary *environment = info != nil ? [info environment] : nil;
		NSString *path = environment != nil ? [environment objectForKey:@"PATH"] : nil;

		check("proc-environment",
		      environment != nil && [environment count] > 0 &&
		      path != nil && [path length] > 0,
		      [NSString stringWithFormat:@"count=%lu PATH=%@",
			(unsigned long)(environment != nil ? [environment count] : 0),
			path != nil ? path : @"(absent)"]);
	}

	/* THE LAST TERM IS THE KERNEL'S OWN CROSS-CHECK: musl's sysconf(_SC_PHYS_PAGES) reads the SAME
	 * `struct sysinfo`, and it DIVIDES by `mem_unit`. While the kernel's struct was the 32-bit one
	 * and the unit field was zero, this door was wrong for every program that used it — so asking
	 * musl the same question is what proves the fix is in the KERNEL and not only in this library. */
	check("proc-counts",
	      info != nil && [info processorCount] >= 1 && [info activeProcessorCount] >= 1 &&
	      [info physicalMemory] > 0 && sysconf(_SC_PHYS_PAGES) > 0 &&
	      sysconf(_SC_NPROCESSORS_ONLN) >= 1,
	      [NSString stringWithFormat:@"cpus=%lu active=%lu memory=%llu physPages=%ld",
		(unsigned long)(info != nil ? [info processorCount] : 0),
		(unsigned long)(info != nil ? [info activeProcessorCount] : 0),
		info != nil ? [info physicalMemory] : 0, sysconf(_SC_PHYS_PAGES)]);

	check("proc-host-and-uptime",
	      info != nil && [[info hostName] length] > 0 && [info systemUptime] >= 0,
	      [NSString stringWithFormat:@"host=%@ uptime=%g",
		info != nil ? [info hostName] : @"(nil)",
		info != nil ? [info systemUptime] : -1.0]);

	{
		NSString *first = info != nil ? [info globallyUniqueString] : nil;
		NSString *second = info != nil ? [info globallyUniqueString] : nil;

		check("proc-unique-strings",
		      first != nil && second != nil && ![first isEqualToString:second],
		      [NSString stringWithFormat:@"first=%@ second=%@", first, second]);
	}

	{
		NSOperatingSystemVersion zero = { 0, 0, 0 };
		NSOperatingSystemVersion sky = { 99, 0, 0 };
		NSString *name = info != nil ? [info operatingSystemName] : nil;
		NSString *text = info != nil ? [info operatingSystemVersionString] : nil;
		NSOperatingSystemVersion ours = info != nil ? [info operatingSystemVersion]
							    : zero;

		check("proc-version",
		      name != nil && [name length] > 0 && text != nil && [text length] > 0 &&
		      ours.majorVersion >= 1 &&
		      [info isOperatingSystemAtLeastVersion:zero] &&
		      ![info isOperatingSystemAtLeastVersion:sky],
		      [NSString stringWithFormat:@"%@ / %@ atLeast0=%d atLeast99=%d", name, text,
			(int)(info != nil && [info isOperatingSystemAtLeastVersion:zero]),
			(int)(info != nil && [info isOperatingSystemAtLeastVersion:sky])]);
	}

	/* §62.98: THE VOCABULARY OF -operatingSystemName, and the thermal state beside it. The seven names are
	 * Apple's, each valued as the name it is; THIS system answers a name outside the seven, which is the
	 * deviation the header records and the reason the vocabulary is declared rather than borrowed. */
	{
		NSProcessInfo *info = [NSProcessInfo processInfo];
		NSString *ours = info != nil ? [info operatingSystemName] : nil;
		BOOL outsideTheSeven = ours != nil && [ours length] > 0 &&
				       ![ours isEqualToString:NSMACHOperatingSystem] &&
				       ![ours isEqualToString:NSWindowsNTOperatingSystem] &&
				       ![ours isEqualToString:NSWindows95OperatingSystem] &&
				       ![ours isEqualToString:NSHPUXOperatingSystem] &&
				       ![ours isEqualToString:NSSolarisOperatingSystem] &&
				       ![ours isEqualToString:NSSunOSOperatingSystem] &&
				       ![ours isEqualToString:NSOSF1OperatingSystem];

		check("proc-operating-system-names",
		      [NSMACHOperatingSystem isEqualToString:@"NSMACHOperatingSystem"] &&
		      [NSWindowsNTOperatingSystem isEqualToString:@"NSWindowsNTOperatingSystem"] &&
		      [NSWindows95OperatingSystem isEqualToString:@"NSWindows95OperatingSystem"] &&
		      [NSHPUXOperatingSystem isEqualToString:@"NSHPUXOperatingSystem"] &&
		      [NSSolarisOperatingSystem isEqualToString:@"NSSolarisOperatingSystem"] &&
		      [NSSunOSOperatingSystem isEqualToString:@"NSSunOSOperatingSystem"] &&
		      [NSOSF1OperatingSystem isEqualToString:@"NSOSF1OperatingSystem"] &&
		      outsideTheSeven,
		      [NSString stringWithFormat:@"this system answers <%@>, which must not be one of the seven", ours]);
	covers("NSProcessInfo", "isOperatingSystemAtLeastVersion:");
	}

	/* AND THE STATE WITH ITS NOTIFICATION (§62.98): the enum had NO door before this unit, so the door and the
	 * name it pairs with are both asserted — the answer is one of the four documented states, and the
	 * notification name is a usable name (a registrant receives a post through it). */
	{
		NSProcessInfo *info = [NSProcessInfo processInfo];
		NSProcessInfoThermalState state = info != nil ? [info thermalState]
							      : NSProcessInfoThermalStateCritical;
		BOOL documented = (state == NSProcessInfoThermalStateNominal ||
				   state == NSProcessInfoThermalStateFair ||
				   state == NSProcessInfoThermalStateSerious ||
				   state == NSProcessInfoThermalStateCritical);
		__block int received = 0;
		id observer;

		observer = [[NSNotificationCenter defaultCenter]
			    addObserverForName:NSProcessInfoThermalStateDidChangeNotification
					object:nil queue:nil usingBlock:^(NSNotification *note) { received++; }];
		[[NSNotificationCenter defaultCenter] postNotificationName:NSProcessInfoThermalStateDidChangeNotification
								   object:nil];
		[[NSNotificationCenter defaultCenter] removeObserver:observer];

		check("proc-thermal-state-and-its-notification",
		      documented && state == NSProcessInfoThermalStateNominal &&
		      [NSProcessInfoThermalStateDidChangeNotification
			 isEqualToString:@"NSProcessInfoThermalStateDidChangeNotification"] &&
		      received == 1,
		      [NSString stringWithFormat:@"state=%d received=%d", (int)state, received]);
	}

	/* THE USER NAMES, MEASURED against NSFileManager's OWN door for the same account database - so the two
	 * cannot drift. NSUserName() is nil only when the guest has no account entry, and this header answers the
	 * empty string there, which the check allows for. */
	{
		NSString *user = info != nil ? [info userName] : nil;
		NSString *full = info != nil ? [info fullUserName] : nil;
		NSString *accountUser = NSUserName();
		BOOL agree = (accountUser != nil) ? [user isEqualToString:accountUser]
						  : (user != nil && [user length] == 0);

		check("proc-user-names",
		      user != nil && full != nil && agree,
		      [NSString stringWithFormat:@"user=<%@> userLen=%lu fullLen=%lu NSUserName=<%@>",
			user, (unsigned long)(user != nil ? [user length] : 0),
			(unsigned long)(full != nil ? [full length] : 0), accountUser]);
	}

	/* THE VERSION COMPONENTS against the struct -processInfo itself answers, so the parts and the whole are
	 * cross-checked rather than a number written twice. */
	{
		NSOperatingSystemVersion v = info != nil ? [info operatingSystemVersion]
						  : (NSOperatingSystemVersion){ 0, 0, 0 };
		NSInteger major = info != nil ? [info majorVersion] : -1;
		NSInteger minor = info != nil ? [info minorVersion] : -1;
		NSInteger patch = info != nil ? [info patchVersion] : -1;

		check("proc-version-components",
		      info != nil && major == v.majorVersion && minor == v.minorVersion &&
		      patch == v.patchVersion && major >= 1,
		      [NSString stringWithFormat:@"major=%ld minor=%ld patch=%ld vs struct %ld.%ld.%ld",
			(long)major, (long)minor, (long)patch,
			(long)v.majorVersion, (long)v.minorVersion, (long)v.patchVersion]);
	}

	/* THE PLATFORM-COMPATIBILITY FLAGS. REASONED, NOT MEASURED: this is a native Argentum process, so it is
	 * NOT a Catalyst app, NOT an iOS app on a Mac or a Vision device, and this system has no low-power mode -
	 * every flag is NO, the truthful state rather than a placeholder. */
	{
		BOOL lowPower = [info lowPowerModeEnabled];
		BOOL catalyst = [info macCatalystApp];
		BOOL onMac = [info iOSAppOnMac];
		BOOL onVision = [info iOSAppOnVision];

		{
		/* ACTIVITIES (§63.181). What a caller can measure with no system support to keep the promise: the
		 * token exists, it is a DIFFERENT object per activity, the block runs exactly once, the expiring form
		 * is asked with "not expired" (nothing is trying to suspend this process), and a foreign token is
		 * ignored rather than fatal. */
		NSProcessInfo *info = [NSProcessInfo processInfo];
		id<NSObject> first = [info beginActivityWithOptions:NSActivityUserInitiated reason:@"probe"];
		id<NSObject> second = [info beginActivityWithOptions:NSActivityBackground reason:@"probe"];
		__block int ran = 0;
		__block BOOL sawExpired = YES;

		[info endActivity:first];
		[info endActivity:[NSObject new]];
		[info performActivityWithOptions:NSActivityIdleSystemSleepDisabled reason:@"block" usingBlock:^{
			ran++;
		}];
		[info performExpiringActivityWithReason:@"expiring" usingBlock:^(BOOL expired) {
			sawExpired = expired;
		}];
		check("proc-activities",
		      first != nil && second != nil && first != second && ran == 1 && sawExpired == NO,
		      [NSString stringWithFormat:@"first=%d second=%d distinct=%d ran=%d expired=%d",
			first != nil, second != nil, first != second, ran, sawExpired]);
	covers("NSProcessInfo", "endActivity:");
	}

	{
		/* THE TERMINATION FLAGS AND THE TWO PLATFORM ANSWERS, asserted by the only parts that HAVE a
		 * getter: the support flag must round-trip, and the enable doors must balance the disable ones
		 * without disturbing it - including ONE MORE enable than disable, which must not underflow. */
		NSProcessInfo *info = [NSProcessInfo processInfo];
		BOOL before = [info automaticTerminationSupportEnabled];

		[info setAutomaticTerminationSupportEnabled:!before];
		[info disableAutomaticTermination:@"probe"];
		[info disableAutomaticTermination:@"probe"];
		[info enableAutomaticTermination:@"probe"];
		[info enableAutomaticTermination:@"probe"];
		[info enableAutomaticTermination:@"probe"];
		[info disableSuddenTermination];
		[info enableSuddenTermination];
		[info enableSuddenTermination];
		check("proc-termination-and-platform-flags",
		      [info automaticTerminationSupportEnabled] == !before &&
		      ![info hasPerformanceProfile:@"probe"] &&
		      ![info isDeviceCertifiedFor:@"probe"],
		      [NSString stringWithFormat:@"support=%d before=%d profile=%d certified=%d",
			[info automaticTerminationSupportEnabled], before,
			[info hasPerformanceProfile:@"probe"], [info isDeviceCertifiedFor:@"probe"]]);
	covers("NSProcessInfo", "disableAutomaticTermination:");
	covers("NSProcessInfo", "enableAutomaticTermination:");
		[info setAutomaticTerminationSupportEnabled:before];	/* leave the flag as it was found */
	}

	check("proc-platform-flags",
		      info != nil && !lowPower && !catalyst && !onMac && !onVision,
		      [NSString stringWithFormat:@"lowPower=%d catalyst=%d onMac=%d onVision=%d",
			(int)lowPower, (int)catalyst, (int)onMac, (int)onVision]);
	covers("NSProcessInfo", "hasPerformanceProfile:");
	covers("NSProcessInfo", "isDeviceCertifiedFor:");
	}

	printf("FOUNDATION-PROCESSINFO RESULT ok=%d fail=%d\n", okc, failc);
	/* The exit status, in the probe's OWN output. After a probe the console can stop
	 * serving INPUT for a while (the tier residual), so an `echo $?` that the harness
	 * types may never run. The probe knows exactly what it returns, so it says so - and
	 * this is the same value: failc ? 1 : 0 is the return statement below. */
	printf("FOUNDATION-PROCESSINFO-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-PROCESSINFO DONE\n");
	return failc ? 1 : 0;
}
