/*
 * corefoundation_bridge — M2's first acceptance: CF dispatching into Objective-C.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * WHAT THIS PROVES, IN ONE SENTENCE: an NSString created by THIS TREE's Foundation, handed to
 * CoreFoundation as a `CFStringRef`, is answered by CF running THAT CLASS'S code — not CF's own C
 * implementation. That is the bridge (docs/design/foundation-cf-core-plan.md D3: "a bridged object IS an
 * ObjC object"), and until this probe exists the mechanism was only compiled, never exercised.
 *
 * HOW THE MECHANISM REACHES IT, since the probe's whole value is that it can fail informatively:
 * `CF_IS_OBJC(typeID, obj)` is an ISA COMPARISON (modification 5 in CFInternal.h, derived from upstream's
 * own comment on __CFISAForTypeID) - an object is Objective-C when its first word is NOT the CF runtime
 * class registered for the type it claims. For an NSString that first word is its real class, so the
 * comparison is true and the dispatch macros send it a message; for a CF-native string the first word is
 * `__CFISAForTypeID` itself (0 at present, since the class table is still empty), so CF keeps its C path.
 * BOTH HALVES ARE CHECKED HERE, because a bridge that reached every object equally would be no bridge.
 *
 * AND ONE CHECK IS DELIBERATELY ABSENT. CFStringGetCString's dispatch site asks for
 * `-getCString:maxLength:encoding:`, and if this tree's Foundation does not implement that selector the
 * probe would die of an unrecognized selector rather than report a check - so it is NOT called here. The
 * FIRST thing to look at when the M2 re-base starts is exactly that list of selectors CF expects and
 * Foundation does not yet answer; a probe that crashed on the first one would hide the rest.
 */
#import <Foundation/Foundation.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <string.h>

static int ok_count = 0;
static int fail_count = 0;

static void check(const char *name, int ok, const char *detail) {
	if (ok) {
		ok_count++;
		printf("COREFOUNDATION-BRIDGE %s ok\n", name);
	} else {
		fail_count++;
		printf("COREFOUNDATION-BRIDGE %s FAIL %s\n", name, detail ? detail : "");
	}
	fflush(stdout);
}

int main(void) {
	/* THE LITERAL FIRST, because it is the one CF string that arrives with an isa the COMPILER chose
	 * (-fconstant-string-class=NSConstantString), which is a different path from a factory method's. */
	CFStringRef literal = (CFStringRef)@"Bridge";
	CFIndex literalLength = CFStringGetLength(literal);

	check("a-literal-string-dispatches-to-its-class", literalLength == 6,
	      "CFStringGetLength on @\"Bridge\" did not answer 6");

	/* A FACTORY-BUILT STRING, so the check does not rest on the constant-string path alone. */
	NSString *made = [NSString stringWithUTF8String:"Bridge"];
	CFStringRef bridged = (CFStringRef)made;
	CFIndex bridgedLength = CFStringGetLength(bridged);

	check("a-factory-string-dispatches-to-its-class", made != nil && bridgedLength == 6,
	      "CFStringGetLength on +stringWithUTF8String: did not answer 6");

	/* THE SECOND ARGUMENT, so the dispatch is exercised with a parameter and not only with a receiver:
	 * this site asks for -characterAtIndex: and casts the answer to UniChar. */
	check("a-parameterised-site-crosses-the-bridge",
	      CFStringGetCharacterAtIndex(bridged, 0) == (UniChar)'B',
	      "CFStringGetCharacterAtIndex did not answer 'B' through the bridge");

	/* AND THE OTHER HALF OF THE COIN: a string CF made ITSELF must still take CF's OWN path. If the
	 * bridge's check were wrong - true for everything - the two halves would be indistinguishable, and
	 * this is the check that says so. */
	/* A SECOND BRIDGED TYPE, because one door proved is one door. CFArray's dispatch was compiled from the
	 * first day of this work and had never RUN until this check: an NSArray literal handed to CF as a
	 * CFArrayRef, answered by -count. The comment is the point - the array is built by the COMPILER
	 * (@[...]), so its class is the literal array class, a different path again from a factory result. */
	NSArray *array = @[@"Bridge", @"x"];
	CFArrayRef bridgedArray = (CFArrayRef)array;
	check("an-nsarray-dispatches-to-its-class",
	      array != nil && CFArrayGetCount(bridgedArray) == 2,
	      "CFArrayGetCount did not answer 2 through the bridge");

	/* THE DOOR THE FIRST UNIT DELIBERATELY AVOIDED, NOW OPEN. CFStringGetCString's dispatch site asks the
	 * object for `-_getCString:maxLength:encoding:` - CF's PRIVATE protocol, and one that takes a
	 * CFStringEncoding rather than an NSStringEncoding - so before the compatibility surface existed this
	 * check would have died of an unrecognized selector instead of reporting. FNCoreFoundationBridge.m
	 * answers it, and this is that answer measured: the bytes CF asked for, in the buffer CF provided. */
	char cbuf[32];
	Boolean gotCString = CFStringGetCString(bridged, cbuf, (CFIndex)sizeof(cbuf), kCFStringEncodingUTF8);

	check("a-cf-door-needing-private-foundation-crosses-the-bridge",
	      gotCString && strcmp(cbuf, "Bridge") == 0,
	      "CFStringGetCString did not answer through the bridge");

	CFStringRef native = CFStringCreateWithCString(kCFAllocatorDefault, "Bridge", kCFStringEncodingUTF8);
	check("a-cf-native-string-still-takes-cfs-own-path",
	      native != NULL && CFStringGetLength(native) == 6,
	      "CF's own string did not answer through CF's own implementation");
	if (native != NULL) {
		CFRelease(native);
	}

	printf("COREFOUNDATION-BRIDGE RESULT ok=%d fail=%d\n", ok_count, fail_count);
	printf("COREFOUNDATION-BRIDGE DONE\n");
	fflush(stdout);
	return fail_count == 0 ? 0 : 1;
}
