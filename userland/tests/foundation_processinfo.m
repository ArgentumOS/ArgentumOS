/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_processinfo, unit of 1 — F13.13's acceptance.
 * docs/design/foundation-plan.md §10.
 *
 * ONE unit, importing only <foundation/Foundation.h>, plus <unistd.h> for the CROSS-CHECK that
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
 */

#import <foundation/Foundation.h>

#include <stdio.h>
#include <unistd.h>

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-PROCESSINFO %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-PROCESSINFO %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

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

	printf("FOUNDATION-PROCESSINFO RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-PROCESSINFO DONE\n");
	return failc ? 1 : 0;
}
