/*
 * foundation_object — the first acceptance of the NEW Foundation: one object, one lifetime, both worlds.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * WHAT THIS PROVES, AND WHY THESE ARE THE CHECKS AND NOT OTHERS. The new library's day-one claim is that
 * a Foundation object is an Objective-C object CoreFoundation accepts as its own. The old library could
 * only ever prove HALF of that, and this session measured which half: CF's type-specific doors dispatched
 * into its classes correctly, while CFRetain/CFRelease did NOT reach them — an object the old Foundation
 * built was DROPPED by a CFArray created with kCFTypeArrayCallBacks. That is the failure a container's
 * contents disappearing into, so the checks below are built around it rather than around what is easy:
 *
 *   * cf-retain-keeps-an-object-of-this-library-alive — CF's OWN retain, on our object, keeps it alive
 *     while we let go of it;
 *   * cf-release-lets-it-go — and CF's release is what ends it;
 *   * a-cf-array-holds-an-object-of-this-library — a CF container built with CF's OWN callbacks retains
 *     what it is given, and hands the same object back;
 *   * a-cf-array-releases-it-when-the-array-goes — and the container is what ends it.
 *
 * THE SUBCLASS WITH THE COUNTER IS THE INSTRUMENT, and it exists because the base class cannot report its
 * own death: -dealloc frees through the runtime. A dealloc counter in a subclass is the smallest thing
 * that can tell "still alive" from "quietly gone".
 *
 * AND TWO CHECKS ARE DELIBERATELY HERE RATHER THAN LATER, because they are what makes the two worlds agree
 * rather than merely coexist: the class the object reports, and the description door building Core
 * Foundation's own string. A CFStringRef is a CF object, so CFStringGetLength on it is CF's own code path
 * running over a string this library built.
 */

#import <Foundation/NSObject.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <string.h>

static int ok_count = 0;
static int fail_count = 0;

static void check(const char *name, int ok, const char *detail) {
	if (ok) {
		ok_count++;
		printf("FOUNDATION-OBJECT %s ok\n", name);
	} else {
		fail_count++;
		printf("FOUNDATION-OBJECT %s FAIL %s\n", name, detail ? detail : "");
	}
	fflush(stdout);
}

static int probe_deallocs = 0;

@interface FNProbeObject : NSObject
@end

@implementation FNProbeObject
- (void)dealloc
{
	probe_deallocs++;
	[super dealloc];
}
@end

int main(void) {
	id plain = [[NSObject alloc] init];

	check("an-object-of-this-library-is-allocated", plain != nil,
	      "[[NSObject alloc] init] answered nothing");
	check("its-class-is-the-class-that-was-asked-for", object_getClass(plain) == [NSObject class],
	      "the object's class is not NSObject");
	check("a-subclass-is-a-kind-of-its-superclass", [[[FNProbeObject alloc] init] isKindOfClass:[NSObject class]],
	      "isKindOfClass: did not walk up to NSObject");
	[plain release];

	/* CF'S OWN RETAIN, ON OUR OBJECT — the check the old library could not pass. */
	probe_deallocs = 0;
	{
		FNProbeObject *o = [[FNProbeObject alloc] init];

		CFRetain((CFTypeRef)o);
		[o release];
		check("cf-retain-keeps-an-object-of-this-library-alive", probe_deallocs == 0,
		      "the object died while CF held a reference to it - CFRetain does not reach this library");
		CFRelease((CFTypeRef)o);
		check("cf-release-lets-it-go", probe_deallocs == 1,
		      "the object outlived CF's release - CFRelease does not reach this library");
	}

	/* A CF CONTAINER, WITH CF'S OWN CALLBACKS — the failure this design exists to prevent. */
	probe_deallocs = 0;
	{
		CFMutableArrayRef array = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
		FNProbeObject *held = [[FNProbeObject alloc] init];

		CFArrayAppendValue(array, (const void *)held);
		[held release];
		check("a-cf-array-holds-an-object-of-this-library", probe_deallocs == 0,
		      "the CF array let the object die - a CF container built with CF's own callbacks cannot hold this library's objects");
		check("and-hands-the-same-object-back", CFArrayGetValueAtIndex(array, 0) == (const void *)held,
		      "the array did not return the object it was given");
		CFRelease(array);
		check("a-cf-array-releases-it-when-the-array-goes", probe_deallocs == 1,
		      "the object outlived the CF array that held it");
	}

	/* THE DESCRIPTION DOOR IS CF'S OWN STRING, so CF's own code path can read it. */
	{
		id described = [[NSObject alloc] init];
		CFStringRef text = (CFStringRef)[described description];

		check("the-description-door-builds-a-cf-string", text != NULL && CFStringGetLength(text) > 0,
		      "description did not build a non-empty CoreFoundation string");
		check("and-that-string-names-the-class", text != NULL && CFStringFind(text, CFSTR("NSObject"), 0).location != kCFNotFound,
		      "the description does not name the class");
		CFRelease(text);
		[described release];
	}

	printf("FOUNDATION-OBJECT RESULT ok=%d fail=%d\n", ok_count, fail_count);
	printf("FOUNDATION-OBJECT DONE\n");
	fflush(stdout);
	return (fail_count == 0) ? 0 : 1;
}
