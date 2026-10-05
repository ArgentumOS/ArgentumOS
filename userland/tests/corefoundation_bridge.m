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


/*
 * A PROBE-LOCAL CLASS WHOSE DEALLOC IS OBSERVABLE, for the check that decides M4's first edit. NSArray
 * retains its items BY HAND today, while kCFTypeArrayCallBacks makes CF do the retaining - and those must
 * not BOTH happen, or every item is double-retained. Whether CF's callbacks actually reach an object built
 * by THIS library (rather than by Swift's, which is what upstream's CF was written against) is exactly the
 * kind of thing this session has twice found to be an assumption rather than a fact, so it is measured.
 */
static int bridge_probe_deallocs = 0;

@interface FNBridgeProbeObject : NSObject
@end

@implementation FNBridgeProbeObject
- (void)dealloc
{
	bridge_probe_deallocs++;
	[super dealloc];
}
@end

/* THIS LIBRARY'S OWN CF CALLBACKS, which is what the measurement above says the re-base needs: CF's
 * structure with Objective-C's lifetime policy, so a CFArray can hold this library's objects safely. */
static const void *fn_probe_retain(CFAllocatorRef allocator, const void *value)
{
	(void)allocator;
	return [(id)value retain];
}

static void fn_probe_release(CFAllocatorRef allocator, const void *value)
{
	(void)allocator;
	[(id)value release];
}

/* THE copyDescription CALLBACK IS NULL, and NOT because it is unimportant: `CFSTR("...")` compiles to a
 * reference to the CF CONSTANT STRING CLASS unless the translation unit is built with -fconstant-cfstrings,
 * and in swift-corelibs-foundation that class is Swift's -> the link asked for `$s10Foundation19_NSCFConstantStringCN`
 * and failed. A NULL copyDescription is legal in CFArrayCallBacks, and the real callbacks the re-base writes
 * will need either that flag on the translation unit or a description built without CFSTR. Recorded in the
 * plan, because Foundation's own compile flags did NOT carry the switch. */
static const CFArrayCallBacks fn_probe_array_callbacks = {
	0, fn_probe_retain, fn_probe_release, NULL, NULL
};

static const CFArrayCallBacks *fn_probe_callbacks(void)
{
	return &fn_probe_array_callbacks;
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

	/* A THIRD KIND OF DOOR: CF MUTATING AN OBJECT IN PLACE. CFStringUppercase dispatches `_cfUppercase:` to
	 * an NSMutableString, so the answer is not a return value but the object's own bytes afterwards - and a
	 * mutableCopy of a literal is a Foundation object whose class is neither the tiny nor the constant one. */
	NSMutableString *mutable = [@"bridge" mutableCopy];
	CFStringUppercase((CFMutableStringRef)mutable, NULL);

	check("a-cf-in-place-mutation-crosses-the-bridge",
	      [mutable isEqual:@"BRIDGE"],
	      "CFStringUppercase left the object unchanged through the bridge");

	/* THE FOUR OTHER IN-PLACE DOORS, EACH ASSERTING ITS OWN PROPERTY rather than merely that something
	 * changed - these were compiled and unrun until now, so their contracts rested on being read right. */
	NSMutableString *lower = [@"BRIDGE" mutableCopy];
	CFStringLowercase((CFMutableStringRef)lower, NULL);
	check("a-cf-lowercase-crosses-the-bridge", [lower isEqual:@"bridge"],
	      "CFStringLowercase did not lowercase through the bridge");

	/* CAPITALIZE ASSERTS A PROPERTY, NOT A SPELLING: what CF guarantees here is the first character of the
	 * string uppercased, and this library's word-boundary handling is its own, so asserting "Bridge Over"
	 * exactly would be asserting a coincidence. The length check is what stops "changed" from passing. */
	NSMutableString *capital =
		[@"bridge over" mutableCopy];
	NSUInteger capitalLengthBefore = [capital length];
	CFStringCapitalize((CFMutableStringRef)capital, NULL);
	check("a-cf-capitalize-crosses-the-bridge",
	      [capital length] == capitalLengthBefore && [[capital substringToIndex:1] isEqual:@"B"],
	      "CFStringCapitalize did not capitalise the first character in place");

	NSMutableString *ws = [@"  padded  " mutableCopy];
	CFStringTrimWhitespace((CFMutableStringRef)ws);
	check("a-cf-trim-whitespace-crosses-the-bridge", [ws isEqual:@"padded"],
	      "CFStringTrimWhitespace did not trim through the bridge");

	/* AND THE ONE THAT READS ITS ARGUMENT THROUGH CF: the trim string is a BRIDGED NSString, so this check
	 * exercises _cfTrim:'s own use of CFStringGetLength/CFStringGetCharacterAtIndex on a bridged string -
	 * the chain that a Foundation-side NSString message would have got wrong. */
	NSMutableString *trimmed = [@"xxbridgeyy" mutableCopy];
	CFStringTrim((CFMutableStringRef)trimmed, (CFStringRef)@"xy");
	check("a-cf-trim-characters-crosses-the-bridge", [trimmed isEqual:@"bridge"],
	      "CFStringTrim did not trim the given characters through the bridge");

	CFStringRef native = CFStringCreateWithCString(kCFAllocatorDefault, "Bridge", kCFStringEncodingUTF8);
	check("a-cf-native-string-still-takes-cfs-own-path",
	      native != NULL && CFStringGetLength(native) == 6,
	      "CF's own string did not answer through CF's own implementation");
	if (native != NULL) {
		CFRelease(native);
	}


	/* THE MEASUREMENT M4'S FIRST EDIT DEPENDS ON, AND ITS ANSWER CHANGED THE EDIT: CF's OWN array callbacks
	 * do NOT retain an object this library built - the object dies while the CFArray holds it. So an NSArray
	 * re-based on CFArray with kCFTypeArrayCallBacks would DROP EVERY ITEM. The second check is the design
	 * that works, and it is the one the edit will use: CF takes the callbacks FROM THE CALLER, so the array
	 * can be CF's STRUCTURE with this library's retain/release POLICY.
	 *
	 * THE PAIR IS DELIBERATE, and the first version of this measurement was one check pair with a flaw worth
	 * recording: when the object died early, the "released when the array goes" check passed VACUOUSLY - a
	 * freed object counts as deallocated. The two facts are now separate checks with separate objects, so
	 * neither can pass on the other's failure. */
	{
		CFMutableArrayRef cfCallbacks = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
		id victim = [[FNBridgeProbeObject alloc] init];

		bridge_probe_deallocs = 0;
		if (cfCallbacks != NULL) {
			CFArrayAppendValue(cfCallbacks, victim);
			[victim release];
			victim = nil;
			check("a-cf-array-with-cfs-own-callbacks-drops-this-librarys-object",
			      bridge_probe_deallocs == 1,
			      "the object SURVIVED - so CF's callbacks do retain this library's objects after all, and the "
			      "callbacks below are unnecessary");
			CFRelease(cfCallbacks);
		}
	}
	{
		CFMutableArrayRef ours = CFArrayCreateMutable(kCFAllocatorDefault, 0, fn_probe_callbacks());
		id held = [[FNBridgeProbeObject alloc] init];

		bridge_probe_deallocs = 0;
		if (ours != NULL) {
			CFArrayAppendValue(ours, held);
			[held release];
			held = nil;
			check("a-cf-array-with-this-librarys-callbacks-retains-its-item", bridge_probe_deallocs == 0,
			      "the object died though our own callbacks should have retained it");
			CFRelease(ours);
			check("a-cf-array-with-this-librarys-callbacks-releases-when-it-goes",
			      bridge_probe_deallocs == 1,
			      "the object outlived the array - our release callback did not run");
		}
	}
	printf("COREFOUNDATION-BRIDGE RESULT ok=%d fail=%d\n", ok_count, fail_count);
	printf("COREFOUNDATION-BRIDGE DONE\n");
	fflush(stdout);
	return fail_count == 0 ? 0 : 1;
}
