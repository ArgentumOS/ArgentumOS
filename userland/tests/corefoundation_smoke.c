/*
 * corefoundation_smoke — M1's acceptance for the vendored CoreFoundation.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * WHAT THIS JUDGES, AND WHAT IT DELIBERATELY DOES NOT. It is the FIRST thing in this tree that links
 * the adopted CoreFoundation, so its job is the narrowest honest one: that the library LOADS, that the
 * allocator/runtime beneath a CF object works, and that a CFString and a CFArray carry their bytes and
 * their count. That is M1's whole claim — "vendor and build CF for FNX, with libcorefoundation.so.1 in
 * the image" (docs/design/foundation-cf-core-plan.md §7).
 *
 * IT IS C AND NOT OBJECTIVE-C, ON PURPOSE. The plan's bridge (D3/D8) makes CF types and Objective-C
 * objects the same memory; this probe must therefore NOT depend on that, or a failure here would be
 * indistinguishable from a failure of the bridge. Nothing in this file sends a message or casts to id,
 * so it can pass while the bridge does not exist yet — which is exactly right for M1, and why M2's
 * acceptance is the existing Foundation probes rather than this one.
 *
 * CFRelease IS REACHED FOR EVERY OBJECT, because a smoke test that leaks proves the create path and
 * nothing else: the deallocate path is where the runtime's allocator bookkeeping actually runs.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <string.h>

static int ok_count = 0;
static int fail_count = 0;

static void check(const char *name, int ok, const char *detail) {
	if (ok) {
		ok_count++;
		printf("COREFOUNDATION-SMOKE %s ok\n", name);
	} else {
		fail_count++;
		printf("COREFOUNDATION-SMOKE %s FAIL %s\n", name, detail ? detail : "");
	}
	fflush(stdout);
}

int main(void) {
	/* THE STRING, both directions: created from UTF-8 bytes, its length read, its bytes read back out
	 * into a buffer this file owns. Comparing the round trip against the input is the point — a length
	 * that agreed with a corrupt read-back would pass a one-way check. */
	const char *source = "CoreFoundation on FNX";
	CFStringRef string = CFStringCreateWithCString(kCFAllocatorDefault, source, kCFStringEncodingUTF8);
	char buffer[64];
	Boolean gotBytes = false;

	check("cfstring-create-succeeds", string != NULL, "CFStringCreateWithCString returned NULL");
	if (string != NULL) {
		check("cfstring-length-is-the-character-count",
		      (long)CFStringGetLength(string) == (long)strlen(source),
		      "CFStringGetLength and strlen disagree");
		memset(buffer, 0, sizeof(buffer));
		gotBytes = CFStringGetCString(string, buffer, (CFIndex)sizeof(buffer), kCFStringEncodingUTF8);
		check("cfstring-round-trips-its-bytes",
		      gotBytes && strcmp(buffer, source) == 0,
		      gotBytes ? "the bytes read back differ from the bytes written"
		               : "CFStringGetCString refused the buffer");
	}

	/* THE ARRAY: built from the string above, so the two types are exercised together — which is what
	 * catches a runtime whose type IDs collide. */
	if (string != NULL) {
		const void *values[1] = { (const void *)string };
		CFArrayRef array = CFArrayCreate(kCFAllocatorDefault, values, 1, &kCFTypeArrayCallBacks);

		check("cfarray-create-succeeds", array != NULL, "CFArrayCreate returned NULL");
		if (array != NULL) {
			check("cfarray-counts-and-indexes",
			      CFArrayGetCount(array) == 1 &&
			      CFArrayGetValueAtIndex(array, 0) == string,
			      "the array's count or its first element is wrong");
			check("cfarray-and-cfstring-have-different-type-ids",
			      CFGetTypeID(array) != CFGetTypeID(string),
			      "CFArray and CFString report the same CFTypeID");
			CFRelease(array);
		}
	}

	/* The string outlives the array that borrowed it (kCFTypeArrayCallBacks retains), and releasing it
	 * last is what makes the check above meaningful. */
	if (string != NULL) {
		CFRelease(string);
	}

	printf("COREFOUNDATION-SMOKE RESULT ok=%d fail=%d\n", ok_count, fail_count);
	printf("COREFOUNDATION-SMOKE DONE\n");
	fflush(stdout);
	return fail_count == 0 ? 0 : 1;
}
