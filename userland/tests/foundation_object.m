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

/* A NOTE IS NOT A CHECK: it prints a NAME and a VALUE so a failure can be read rather than guessed at,
 * which is the only way the two-question check above could be told apart. */
static void note_value(const char *what, unsigned long value) {
	printf("FOUNDATION-OBJECT note %s = 0x%lx\n", what, value);
	fflush(stdout);
}

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

	/* THE DIRECT INSTRUMENT FOR THE ONE QUESTION THE TWO CHECKS ABOVE LEAVE OPEN, and it comes BEFORE the
	 * container check because the container cannot answer it: "the object died" and "the container dropped
	 * it" are both consistent with TWO different faults — an arm that does not fire, and an object shaped
	 * the way the arm's test does not expect. These two facts separate them, and they are separate checks
	 * on purpose: the first word must BE the class (that is what CF_IS_OBJC compares), and CFRetain must
	 * move the count the class owns. First holds and second does not = the arm is not firing. First fails =
	 * the object is not shaped as assumed. */
	{
		FNProbeObject *who = [[FNProbeObject alloc] init];
		unsigned long before = (unsigned long)[who retainCount];
		unsigned long first = *(unsigned long *)who;

		check("the-objects-first-word-is-its-class",
		      first != 0 && first == (unsigned long)object_getClass(who),
		      "the object's first word is not its class, which is what CF_IS_OBJC compares");
		CFRetain((CFTypeRef)who);
		check("cf-retain-moves-the-count-the-class-owns",
		      (unsigned long)[who retainCount] == before + 1,
		      "CFRetain did not move the count the class owns - the ownership arm is not firing");
		[who release];
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

	/* FREE CASTING, WHICH IS THE POINT OF THE WHOLE EXERCISE. A CF-NATIVE string is not merely usable by
	 * CF: it is a messageable OBJECT, and the same pointer goes both ways without a conversion step. The
	 * checks are in that order on purpose — an isa FIRST (nothing can be messaged without one), then the
	 * message, then the cast back to the C API. */
	{
		CFStringRef native = CFStringCreateWithCString(kCFAllocatorDefault, "native", kCFStringEncodingUTF8);
		Class registered = objc_getClass("NSString");	/* the class the CF package declares */

		/* THE EVIDENCE, PRINTED RATHER THAN COMPARED. The previous version of this check asked two
		 * questions as one — "is the isa NSString" AND "does the class even exist" — so a missing class
		 * and a wrong isa produced the same message, and one of them is a bug in the probe rather than in
		 * the library. These notes separate them, and they also settle whether the registration is what
		 * fails: the door is called HERE, after the first string, and a second string is then made. If the
		 * two isas differ, the door works and the load-time registration did not run. */
		{
			extern unsigned long CFNXBridgeClassToType(Class cls, CFTypeID typeID);
			CFStringRef after = CFStringCreateWithCString(kCFAllocatorDefault, "native2", kCFStringEncodingUTF8);

			note_value("the class the probe looks for", (unsigned long)registered);
			note_value("the first string's first word", native ? *(unsigned long *)native : 0);
			CFNXBridgeClassToType(registered, CFStringGetTypeID());
			note_value("and what the door read back", (unsigned long)CFNXBridgeClassToType(registered, CFStringGetTypeID()));
			note_value("and after calling the door, a new string's", after ? *(unsigned long *)after : 0);
			/* THE TWO IDS, WHICH IS THE QUESTION THE WHOLE FAILURE MAY TURN ON. CF creates a string with the
			 * BUILT-IN constant _kCFRuntimeIDCFString; CFStringGetTypeID() answers the RUNTIME-REGISTERED id;
			 * and the registration is keyed by the latter. If those two numbers differ, the class was
			 * registered on an index that CF's own C code never asks about — and __CFISAForTypeID returns 0
			 * by design, with the object's isa zero and the class table looking perfectly correct. The object
			 * carries its type id in the info word (CFRuntime.c writes typeID << 8), so this is read, not
			 * guessed: the second word's id field should equal CFStringGetTypeID() if they are the same id. */
			note_value("CFStringGetTypeID()", (unsigned long)CFStringGetTypeID());
			if (native != NULL) {
				unsigned long info = ((unsigned long *)native)[1];

				note_value("the object's info word", info);
				note_value("   and its type-id field", (info >> 8) & 0xFFFFFF);
			}
			if (after) {
				CFRelease(after);
			}
		}

		int hasClass = (native != NULL && registered != NULL &&
		                *(unsigned long *)native == (unsigned long)registered);

		check("a-cf-native-string-has-a-class", hasClass,
		      "a CF-native string's first word is not NSString - CF's string creation does not consult the "
		      "bridging registration (CFString.c never sets _cfisa; the immutable funnel bypasses "
		      "_CFRuntimeCreateInstance)");
		if (hasClass) {
			check("and-is-messageable-as-an-ns-string", [(id)native length] == 6,
			      "[(id)cfStr length] did not answer 6 - a CF object cannot be messaged");
			check("and-casts-back-to-cf", CFStringGetLength(native) == 6,
			      "CFStringGetLength on the same pointer did not answer 6 - the cast back does not work");
		} else {
			/* A FAILED PRECONDITION MUST NOT TAKE THE PROCESS WITH IT: messaging an object that has no class
			 * is a CRASH, not a check - and the crash hides the tally and every check after it, which is
			 * exactly what happened on this block's first run. Both dependent checks are therefore REPORTED,
			 * with the reason, rather than skipped: an unrun check that reads as absent is how a gate lies. */
			check("and-is-messageable-as-an-ns-string", 0,
			      "not run: the CF-native object has no class, so there is nothing to message");
			check("and-casts-back-to-cf", 0,
			      "not run: the CF-native object has no class, so there is nothing to cast");
		}
		if (native != NULL) {
			CFRelease(native);
		}
	}

	/* THE DESCRIPTION DOOR IS CF'S OWN STRING, so CF's own code path can read it. */
	{
		id described = [[NSObject alloc] init];
		CFStringRef text = (CFStringRef)[described description];

		check("the-description-door-builds-a-cf-string", text != NULL && CFStringGetLength(text) > 0,
		      "description did not build a non-empty CoreFoundation string");
		/* THE SEARCH STRING IS BUILT AT RUNTIME for the reason NSObject.m records: this library has no
		 * constant-string class yet, so a CFSTR here would be a reference to one that does not exist
		 * (swift-corelibs-foundation's is Swift's, hence the $s10..._NSCFConstantStringCN the linker asks
		 * for). The class, and @"...", are the NSString unit's first order of business. */
		CFStringRef wanted = CFStringCreateWithCString(kCFAllocatorDefault, "NSObject", kCFStringEncodingUTF8);

		check("and-that-string-names-the-class",
		      text != NULL && wanted != NULL && CFStringFind(text, wanted, 0).location != kCFNotFound,
		      "the description does not name the class");
		if (wanted != NULL) {
			CFRelease(wanted);
		}
		CFRelease(text);
		[described release];
	}

	printf("FOUNDATION-OBJECT RESULT ok=%d fail=%d\n", ok_count, fail_count);
	printf("FOUNDATION-OBJECT DONE\n");
	fflush(stdout);
	return (fail_count == 0) ? 0 : 1;
}
