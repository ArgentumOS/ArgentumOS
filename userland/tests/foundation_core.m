/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_core, unit 2 of 2 — the ARC half and the checks.
 */

#import "foundation_core.h"
#include <stdio.h>
#import <objc/runtime.h>
#include <string.h>		/* memset/memcpy, for the page-function check */
#include <math.h>		/* fabs, for the affine-transform checks */

static int okc, failc;

static void check(const char *name, int ok, const char *detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-CORE %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-CORE %s FAIL %s\n", name, detail ? detail : "");
	}
}

/* A NULL ENCODING ON PURPOSE: -signatureWithObjCTypes: takes a nonnull one, and the check below
 * hands it NULL to see the refusal. Fetching it says so; a literal at the call site would be a
 * -Wnonnull finding of its own that had nothing to do with the claim. */
static const char *fn_no_types(void)
{
	return NULL;
}

/*
 * THE RECORDING HOOK, for the one question the probe cannot answer any other way:
 * does the runtime REACH objc_proxy_lookup at all on this build's lookup path? It
 * PRINTS on every call (so the finding is in the log even if something later dies)
 * and counts (so a check can assert it).
 */
static int probe_recording_calls;

static id probe_recording_hook(id receiver, SEL op)
{
	(void)receiver;
	probe_recording_calls++;
	printf("FOUNDATION-CORE forward-probe: objc_proxy_lookup reached (%s)\n",
	       sel_getName(op));
	return nil;
}

int main(void)
{
	/* MRR side: the lifetimes and the equality defaults. */
	check("lifecycle", foundation_core_lifecycle(),
	      "alloc/init/retain/release reached -dealloc exactly once");
	check("equality", foundation_core_equality(),
	      "isEqual: is identity and hash is the pointer, by default");

	/* ARC side: identity and introspection, with no lifetime call at all. */
	{
		Counter *c = [[Counter alloc] init];
		int ok = [[c class] isSubclassOfClass:[NSObject class]] &&
			 [c isKindOfClass:[NSObject class]] &&
			 [c isMemberOfClass:[Counter class]] &&
			 [c respondsToSelector:@selector(hash)] &&
			 ![c respondsToSelector:@selector(noSuchSelectorHere)] &&
			 [c respondsToSelector:@selector(description)];

		check("identity", ok,
		      "class/superclass/isKindOfClass:/isMemberOfClass:/respondsToSelector:");
	}

	/*
	 * The runtime's pools, with the library loaded. F0 ships no pool class
	 * (the runtime adopts any class named NSAutoreleasePool — plan §6), so
	 * this check is also the one that proves ARC's @autoreleasepool still
	 * works when libfoundation is linked.
	 */
	{
		int before = foundation_core_deallocs();

		@autoreleasepool {
			Counter *c = [[Counter alloc] init];
			[c setValue:9];
			c = nil;
		}
		check("arc-pool", foundation_core_deallocs() == before + 1,
		      "the runtime's pool released an ARC-managed object");
	}

	/* The class really came from the other translation unit. */
	{
		Counter *c = [[Counter alloc] init];

		check("cross-tu", [c marker] == 4242,
		      "a method implemented in the support unit answers");
	}

	{
		/*
		 * THE AUDITED COCOA INVENTORY for NSObject. The old per-class checks were
		 * SELF-REFERENTIAL — they asserted that what OUR headers declare exists,
		 * which cannot see a method nobody declared, and that is how
		 * +stringWithFormat:arguments: shipped missing. This list is Cocoa's
		 * documented root-class surface, and the exclusion list is asserted ABSENT,
		 * so the inventory cannot drift away from the code. NSObject previously had
		 * no api-complete check at all.
		 */
		static const char *classSelectors[] = {
			"alloc", "new", "class", "superclass",
			"conformsToProtocol:", "respondsToSelector:",
			"instancesRespondToSelector:", "load", "initialize",
			"methodSignatureForSelector:", NULL
		};
		static const char *instanceSelectors[] = {
			"init", "copy", "mutableCopy",
			"retain", "release", "autorelease", "retainCount", "dealloc",
			"class", "superclass", "isKindOfClass:", "isMemberOfClass:",
			"respondsToSelector:", "conformsToProtocol:",
			"performSelector:", "performSelector:withObject:",
			"performSelector:withObject:withObject:", "methodForSelector:",
			"doesNotRecognizeSelector:", "isEqual:", "hash", "description",
			"debugDescription", "self", "isProxy",
			"methodSignatureForSelector:",
			"forwardingTargetForSelector:", "forwardInvocation:", NULL
		};
		static const char *excluded[] = {
			/* EMPTY, and that is the point: the forwarding trio was the last thing
			 * this class was missing. The loop below still runs, so a future
			 * exclusion has a place to go. */
			NULL
		};
		NSObject *probe = [[NSObject alloc] init];
		int complete = 1;
		int i;

		for (i = 0; classSelectors[i] != NULL; i++) {
			if (![NSObject respondsToSelector:sel_registerName(classSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-CORE missing +%s\n", classSelectors[i]);
			}
		}
		for (i = 0; instanceSelectors[i] != NULL; i++) {
			if (![probe respondsToSelector:sel_registerName(instanceSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-CORE missing -%s\n", instanceSelectors[i]);
			}
		}
		for (i = 0; excluded[i] != NULL; i++) {
			if ([probe respondsToSelector:sel_registerName(excluded[i])]) {
				complete = 0;
				printf("FOUNDATION-CORE present but EXCLUDED: %s\n", excluded[i]);
			}
		}
		check("nsobject-api-complete", complete,
		      "the audited Cocoa inventory for NSObject");

	/*
	 * THE C ACCESSORS (W2a): the runtime's names, and a range's spelling. They are the
	 * boundary between objc/runtime.h and this library's strings, and the cases worth
	 * asserting are the ANSWERS for what is not there (Nil, nil, nil).
	 */
	/* SPLIT, because one conjunction cannot say which claim is false (the lesson
	 * url-refusals taught at the cost of a stale check read as a lost refusal). */
	check("runtime-class-names",
	      NSClassFromString(@"NSObject") == [NSObject class] &&
	      NSClassFromString(@"NoSuchClassInThisTree") == Nil &&
	      NSStringFromClass([NSObject class]) != nil &&
	      [NSStringFromClass([NSObject class]) isEqualToString:@"NSObject"] &&
	      NSStringFromClass(Nil) == nil,
	      "the class name round trips, and a name nothing registers answers Nil");

	/* SEL POINTER IDENTITY IS NOT A CONTRACT IN THIS RUNTIME, and this tree found that
	 * once already (the F-stage forwarding work needed sel_isEqual). NSSelectorFromString
	 * registers a name and answers a SELECTOR; whether it is the same POINTER as a
	 * compiled @selector(...) is the runtime's business, so the check asks the runtime's
	 * own comparison. */
	check("runtime-selector-names",
	      [NSStringFromSelector(@selector(description)) isEqualToString:@"description"] &&
	      sel_isEqual(NSSelectorFromString(@"description"), @selector(description)) &&
	      sel_isEqual(NSSelectorFromString(@"noSuchMethodCompiledAnywhere"), 
			  sel_registerName("noSuchMethodCompiledAnywhere")),
	      "the selector name round trips both ways, compared with sel_isEqual");

	check("runtime-range-string",
	      [NSStringFromRange(NSMakeRange(1, 3)) isEqualToString:@"{1, 3}"],
	      "NSStringFromRange spells Apple's {location, length}");

	/*
	 * THE GEOMETRY FAMILY (W2b, docs/design/foundation-plan.md §14): the C level's structs
	 * and functions, asserted on the answers that are easy to get WRONG — the half-open
	 * edge rule, an empty intersection being the ZERO rect, a negative size being empty,
	 * and the string forms round-tripping.
	 */
	{
		NSRect r = NSMakeRect(10.0, 20.0, 100.0, 40.0);
		NSRect slice, rest;
		NSPoint p = NSMakePoint(10.0, 20.0);

		NSDivideRect(r, &slice, &rest, 30.0, NSRectEdgeMinX);
		check("geometry-rects",
		      NSWidth(r) == 100.0 && NSHeight(r) == 40.0 &&
		      NSMinX(r) == 10.0 && NSMaxX(r) == 110.0 &&
		      NSMidX(r) == 60.0 && NSMidY(r) == 40.0 &&
		      NSEqualRects(NSInsetRect(r, 5.0, 5.0), NSMakeRect(15.0, 25.0, 90.0, 30.0)) &&
		      NSEqualRects(NSOffsetRect(r, 1.0, 2.0), NSMakeRect(11.0, 22.0, 100.0, 40.0)) &&
		      NSContainsRect(r, NSMakeRect(20.0, 30.0, 10.0, 10.0)) &&
		      NSWidth(slice) == 30.0 && NSWidth(rest) == 70.0 &&
		      !NSContainsRect(r, NSMakeRect(0.0, 0.0, 10.0, 10.0)),
		      "the rect accessors, the inset/offset/contain answers and a divide");
		check("geometry-edges",
		      NSPointInRect(p, r) &&
		      !NSPointInRect(NSMakePoint(110.0, 30.0), r) &&
		      NSPointInRect(NSMakePoint(109.99, 30.0), r) &&
		      NSIsEmptyRect(NSMakeRect(0.0, 0.0, -5.0, 10.0)) &&
		      NSIsEmptyRect(NSZeroRect) &&
		      NSEqualRects(NSIntersectionRect(NSMakeRect(0.0, 0.0, 10.0, 10.0),
						      NSMakeRect(50.0, 50.0, 10.0, 10.0)),
				   NSZeroRect) &&
		      NSEqualRects(NSUnionRect(NSZeroRect, r), r) &&
		      NSEqualRects(NSIntegralRect(NSMakeRect(1.5, 2.2, 3.1, 4.4)),
				   NSMakeRect(1.0, 2.0, 4.0, 5.0)),
		      "the half-open edge rule, an empty intersection, a negative size, and integration");
		check("geometry-strings",
		      [NSStringFromPoint(NSMakePoint(1.0, 2.0)) isEqualToString:@"{1, 2}"] &&
		      [NSStringFromSize(NSMakeSize(3.0, 4.0)) isEqualToString:@"{3, 4}"] &&
		      [NSStringFromRect(r) isEqualToString:@"{{10, 20}, {100, 40}}"] &&
		      NSEqualPoints(NSPointFromString(@"{1, 2}"), NSMakePoint(1.0, 2.0)) &&
		      NSEqualSizes(NSSizeFromString(@"{3, 4}"), NSMakeSize(3.0, 4.0)) &&
		      NSEqualRects(NSRectFromString(@"{{10, 20}, {100, 40}}"), r) &&
		      NSEqualPoints(NSPointFromString(@"not a point"), NSZeroPoint) &&
		      NSEdgeInsetsEqual(NSEdgeInsetsZero, NSEdgeInsetsMake(0.0, 0.0, 0.0, 0.0)),
		      "the string forms round trip, %g is the spelling, and a bad string answers the zero value");

		/* THE NS TYPES ARE THE CG TYPES, and one of these claims is a COMPILE-TIME
		 * fact that no runtime check could make honestly. */
#ifndef NSGEOMETRY_TYPES_SAME_AS_CGGEOMETRY_TYPES
#error "the NS geometry types must BE the CG types (NSGEOMETRY_TYPES_SAME_AS_CGGEOMETRY_TYPES)"
#endif
		check("geometry-cg-types",
		      sizeof(CGFloat) == sizeof(double) &&
		      sizeof(CGPoint) == sizeof(NSPoint) && sizeof(CGRect) == sizeof(NSRect) &&
		      NSEqualPoints(NSPointFromCGPoint(NSMakePoint(3.0, 4.0)), NSMakePoint(3.0, 4.0)) &&
		      NSEqualPoints(NSPointToCGPoint(NSMakePoint(3.0, 4.0)), NSMakePoint(3.0, 4.0)) &&
		      NSEqualSizes(NSSizeFromCGSize(NSMakeSize(5.0, 6.0)), NSMakeSize(5.0, 6.0)) &&
		      NSEqualSizes(NSSizeToCGSize(NSMakeSize(5.0, 6.0)), NSMakeSize(5.0, 6.0)) &&
		      NSEqualRects(NSRectFromCGRect(NSMakeRect(1.0, 2.0, 3.0, 4.0)),
				   NSMakeRect(1.0, 2.0, 3.0, 4.0)) &&
		      NSEqualRects(NSRectToCGRect(NSMakeRect(1.0, 2.0, 3.0, 4.0)),
				   NSMakeRect(1.0, 2.0, 3.0, 4.0)),
		      "the NS geometry types ARE the CG types: CGFloat is double, the layouts match, and the six conversions are identities");

		/*
		 * THE OPTION-DRIVEN INTEGRATION (W2b's residue). The BIT POSITIONS are this
		 * tree's (Apple publishes the constants' meanings and not their values — see
		 * NSGeometry.h), so what is asserted here is the BEHAVIOUR the names promise:
		 * inward contains, outward contains the argument, nearest rounds, an
		 * unspecified side is left alone, and the flipped flag swaps the Y sense.
		 */
		{
			NSRect r = NSMakeRect(1.4, 2.6, 3.4, 4.6);	/* 1.4, 2.6 -> 4.8, 7.2 */

			check("geometry-alignment",
			      NSEqualRects(NSIntegralRectWithOptions(r, NSAlignAllEdgesOutward),
					   NSMakeRect(1.0, 2.0, 4.0, 6.0)) &&
			      NSEqualRects(NSIntegralRectWithOptions(r, NSAlignAllEdgesInward),
					   NSMakeRect(2.0, 3.0, 2.0, 4.0)) &&
			      NSEqualRects(NSIntegralRectWithOptions(r, NSAlignAllEdgesNearest),
					   NSMakeRect(1.0, 3.0, 4.0, 4.0)) &&
			      NSEqualRects(NSIntegralRectWithOptions(r, NSAlignRectFlipped |
								     NSAlignAllEdgesInward),
					   NSMakeRect(2.0, 2.0, 2.0, 6.0)) &&
			      /* AN UNSPECIFIED SIDE IS UNTOUCHED, and no option at all changes nothing. */
			      NSEqualRects(NSIntegralRectWithOptions(r, NSAlignMinXInward),
					   NSMakeRect(2.0, 2.6, 2.8, 4.6)) &&
			      NSEqualRects(NSIntegralRectWithOptions(r, 0), r) &&
			      /* THE WIDTH FORM IS THE SAME DECISION AS THE MAX EDGE. */
			      NSEqualRects(NSIntegralRectWithOptions(r, NSAlignMinXInward |
								     NSAlignWidthInward),
					   NSMakeRect(2.0, 2.6, 2.0, 4.6)) &&
			      /* THE COMPOSITES ARE ORs OF THEIR MEMBERS, and the bits are disjoint. */
			      NSAlignAllEdgesInward == (NSAlignMinXInward | NSAlignMinYInward |
							NSAlignMaxXInward | NSAlignMaxYInward) &&
			      (NSAlignMinXInward & NSAlignMinYInward) == 0 &&
			      (NSAlignMinXInward & NSAlignMinXOutward) == 0 &&
			      (NSAlignMinXInward & NSAlignMinXNearest) == 0,
			      "inward/outward/nearest per edge, unspecified sides untouched, the flipped flag inverting Y, and composites that are ORs of disjoint bits");

	/*
	 * THE C LEVEL'S ODDS AND ENDS (W2c, W2e, W2g, docs/design/foundation-plan.md §14).
	 */
	check("c-byte-order",
	      NSHostByteOrder() == NS_LittleEndian &&
	      sizeof(NSSwappedFloat) == 8 && sizeof(NSSwappedDouble) == 8 &&
	      NSConvertSwappedFloatToHost(NSConvertHostFloatToSwapped(1.5f)) == 1.5f &&
	      NSConvertSwappedDoubleToHost(NSConvertHostDoubleToSwapped(-2.25)) == -2.25 &&
	      NSConvertSwappedDoubleToHost(NSConvertHostDoubleToSwapped(0.0)) == 0.0,
	      "the byte-order round trips both ways, and the host is little-endian");

	/*
	 * THE COLLECTION OPERATORS (W2f). Their VALUES are API — each is the operator string a
	 * program also types into -valueForKeyPath: — so the check asserts the strings, and then
	 * exercises the one this library's KVC is measured to implement THROUGH the API, which is
	 * what makes the constant more than a spelling.
	 */
	check("kvc-operator-constants",
	      [NSAverageKeyValueOperator isEqualToString:@"@avg"] &&
	      [NSCountKeyValueOperator isEqualToString:@"@count"] &&
	      [NSDistinctUnionOfArraysKeyValueOperator isEqualToString:@"@distinctUnionOfArrays"] &&
	      [NSDistinctUnionOfObjectsKeyValueOperator isEqualToString:@"@distinctUnionOfObjects"] &&
	      [NSDistinctUnionOfSetsKeyValueOperator isEqualToString:@"@distinctUnionOfSets"] &&
	      [NSMaximumKeyValueOperator isEqualToString:@"@max"] &&
	      [NSMinimumKeyValueOperator isEqualToString:@"@min"] &&
	      [NSSumKeyValueOperator isEqualToString:@"@sum"] &&
	      [NSUnionOfArraysKeyValueOperator isEqualToString:@"@unionOfArrays"] &&
	      [NSUnionOfObjectsKeyValueOperator isEqualToString:@"@unionOfObjects"] &&
	      [NSUnionOfSetsKeyValueOperator isEqualToString:@"@unionOfSets"] &&
	      [[[NSArray arrayWithObjects:@1, @2, @3, nil] valueForKeyPath:NSCountKeyValueOperator]
	          intValue] == 3 &&
	      NSKeyValueValidationError == 1020 &&
	      NSKeyValueUnionSetMutation == 1 && NSKeyValueSetSetMutation == 4,
	      "the eleven operator strings (API: they are also the key paths), with @count exercised through -valueForKeyPath:, plus the error code and the set-mutation kinds");

	{
		char *p = (char *)NSAllocateMemoryPages(64);
		char *q = (char *)NSAllocateMemoryPages(64);
		BOOL ok = (p != NULL) && (q != NULL);

		if (ok) {
			memset(p, 0x5A, 64);
			memset(q, 0, 64);
			NSCopyMemoryPages(p, q, 64);
			ok = ((unsigned char)q[0] == 0x5A) && ((unsigned char)q[63] == 0x5A);
		}
		check("c-memory-pages", ok,
		      "the page functions allocate, copy and deallocate");
		if (p != NULL) {
			NSDeallocateMemoryPages(p, 64);
		}
		if (q != NULL) {
			NSDeallocateMemoryPages(q, 64);
		}
	}

	check("c-size-and-alignment",
	      NSGetSizeAndAlignment("i", NULL, NULL) != NULL &&
	      NSGetSizeAndAlignment("d", NULL, NULL) != NULL &&
	      NSGetSizeAndAlignment("{_NSPoint=dd}", NULL, NULL) != NULL,
	      "NSGetSizeAndAlignment walks the encodings this library's own value classes use");

	{
		BOOL was = NSZombieEnabled;

		NSZombieEnabled = YES;
		check("c-debug-switches",
		      NSZombieEnabled == YES && NSDebugEnabled == NO &&
		      NSKeepAllocationStatistics == NO && NSDeallocateZombies == NO &&
		      NSFoundationVersionNumber == 0.0,
		      "the debug switches are adjustable and read back, and the version number is this library's own");
		NSZombieEnabled = was;
	}
		}
	}
	}

	{
		/* NSMethodSignature (stage F, first half). The parser is exercised DIRECTLY
		 * — its grammar, its widths and its walk — and then through the runtime
		 * lookup, which is the path a caller actually takes. The first clause below
		 * is the ISOLATION one: the class reachable, the factory answered. */
		NSMethodSignature *plain = [NSMethodSignature signatureWithObjCTypes:"@@:@@"];
		NSMethodSignature *rich = [NSMethodSignature signatureWithObjCTypes:
					   "Vv@:i^v{Point=dd}d[4s]r^i"];
		char seen[128];

		snprintf(seen, sizeof seen, "signature=%s args=%lu return=%s frame=%lu oneway=%d",
			 (plain != nil) ? "yes" : "NIL",
			 (unsigned long)((plain != nil) ? [plain numberOfArguments] : 0UL),
			 (plain != nil && [plain methodReturnType] != NULL)
				 ? [plain methodReturnType] : "?",
			 (unsigned long)((plain != nil) ? [plain frameLength] : 0UL),
			 (plain != nil) ? (int)[plain isOneway] : -1);

		check("methodsignature-parses",
		      [NSMethodSignature class] != nil &&
		      [NSMethodSignature respondsToSelector:@selector(signatureWithObjCTypes:)] &&
		      [[NSMethodSignature class] isSubclassOfClass:[NSObject class]] &&
		      plain != nil && [plain numberOfArguments] == 4 &&
		      [[NSString stringWithUTF8String:[plain methodReturnType]] isEqualToString:@"@"] &&
		      [[NSString stringWithUTF8String:[plain getArgumentTypeAtIndex:0]] isEqualToString:@"@"] &&
		      [[NSString stringWithUTF8String:[plain getArgumentTypeAtIndex:1]] isEqualToString:@":"] &&
		      [[NSString stringWithUTF8String:[plain getArgumentTypeAtIndex:2]] isEqualToString:@"@"] &&
		      [[NSString stringWithUTF8String:[plain getArgumentTypeAtIndex:3]] isEqualToString:@"@"] &&
		      [plain methodReturnLength] == sizeof(void *) &&
		      [plain frameLength] == 4 * sizeof(void *) &&
		      [plain isOneway] == NO,
		      seen);

		check("methodsignature-grammar",
		      rich != nil && [rich numberOfArguments] == 8 &&
		      [[NSString stringWithUTF8String:[rich getArgumentTypeAtIndex:2]] isEqualToString:@"i"] &&
		      [[NSString stringWithUTF8String:[rich getArgumentTypeAtIndex:3]] isEqualToString:@"^v"] &&
		      [[NSString stringWithUTF8String:[rich getArgumentTypeAtIndex:4]] isEqualToString:@"{Point=dd}"] &&
		      [[NSString stringWithUTF8String:[rich getArgumentTypeAtIndex:6]] isEqualToString:@"[4s]"] &&
		      [[NSString stringWithUTF8String:[rich getArgumentTypeAtIndex:7]] isEqualToString:@"r^i"] &&
		      [rich methodReturnLength] == 0 &&
		      [rich frameLength] == 9 * sizeof(void *) &&
		      [rich isOneway] == YES,
		      "pointers, a nested struct, an array, a qualifier and the oneway marker");

		check("methodsignature-offsets",
		      [[NSMethodSignature signatureWithObjCTypes:"@16@0:8"] numberOfArguments] == 2 &&
		      [[NSString stringWithUTF8String:
			[[NSMethodSignature signatureWithObjCTypes:"@16@0:8"]
				getArgumentTypeAtIndex:0]] isEqualToString:@"@"] &&
		      [[NSString stringWithUTF8String:
			[[NSMethodSignature signatureWithObjCTypes:"@16@0:8"]
				getArgumentTypeAtIndex:1]] isEqualToString:@":"],
		      "the older encoding form carries a frame OFFSET between types, which is skipped rather than read as a type");

		check("methodsignature-sizes",
		      [[NSMethodSignature signatureWithObjCTypes:"{Point=dd}@:"] methodReturnLength] == 16 &&
		      [[NSString stringWithUTF8String:
			[[NSMethodSignature signatureWithObjCTypes:"{Point=dd}@:"] methodReturnType]]
				isEqualToString:@"{Point=dd}"] &&
		      [[NSMethodSignature signatureWithObjCTypes:"d@:"] methodReturnLength] == 8 &&
		      [[NSMethodSignature signatureWithObjCTypes:"v@:"] methodReturnLength] == 0 &&
		      [[NSMethodSignature signatureWithObjCTypes:"v@:B"] frameLength] == 3 * sizeof(void *) &&
		      [NSMethodSignature signatureWithObjCTypes:fn_no_types()] == nil,
		      "a struct's width is its fields with alignment, void is 0, and a NULL encoding is nil");
	}

	{
		/* THE RUNTIME LOOKUP, both sides: the answer comes from a REAL method's
		 * encoding, instance and class variant, which is what makes it useful to a
		 * forwarding implementer. */
		NSObject *probe = [[NSObject alloc] init];
		NSMethodSignature *instance = [probe methodSignatureForSelector:@selector(description)];
		NSMethodSignature *classSide = [NSObject methodSignatureForSelector:@selector(alloc)];
		char seen[128];

		snprintf(seen, sizeof seen, "instance=%s args=%lu return=%s classSide=%s",
			 (instance != nil) ? "yes" : "NIL",
			 (unsigned long)((instance != nil) ? [instance numberOfArguments] : 0UL),
			 (instance != nil && [instance methodReturnType] != NULL)
				 ? [instance methodReturnType] : "?",
			 (classSide != nil) ? "yes" : "NIL");

		check("methodsignature-lookup",
		      instance != nil && [instance numberOfArguments] == 2 &&
		      [[NSString stringWithUTF8String:[instance methodReturnType]] isEqualToString:@"@"] &&
		      [[NSString stringWithUTF8String:[instance getArgumentTypeAtIndex:0]] isEqualToString:@"@"] &&
		      [[NSString stringWithUTF8String:[instance getArgumentTypeAtIndex:1]] isEqualToString:@":"] &&
		      classSide != nil && [classSide numberOfArguments] == 2 &&
		      [[NSString stringWithUTF8String:[classSide methodReturnType]] isEqualToString:@"@"],
		      seen);
	}

	{
		/* FORWARDING (stage F, second half). Two paths, wired differently: the fast
		 * one is the runtime's objc_proxy_lookup hook (no invocation is built at
		 * all), the slow one is an NSInvocation built from the captured register
		 * file. The slow check is the one that proves the MARSHALLING — an argument
		 * goes in and a value comes back. */
		FastForwarder *fast = [[FastForwarder alloc] init];
		SlowForwarder *slow = [[SlowForwarder alloc] init];
		char seen[128];

		/*
		 * THE DIAGNOSTIC FIRST, and it is a SPLIT on purpose: three things have to
		 * be true for the fast path to work, and one "forwarding failed" line
		 * cannot say which of them is false. The hook is the runtime's own global,
		 * so this asks it directly.
		 */
		{
			extern id (*objc_proxy_lookup)(id receiver, SEL op);	/* objc/hooks.h */
			BOOL responds = [fast respondsToSelector:@selector(forwardingTargetForSelector:)];
			id answer = objc_proxy_lookup(fast, @selector(marker));

			snprintf(seen, sizeof seen, "responds=%d hook=%s",
				 responds ? 1 : 0, (answer != nil) ? "yes" : "NIL");
			check("forwarding-hook", responds && answer != nil, seen);
		}

		/*
		 * THE REACHABILITY EXPERIMENT, and it is PRINT-ONLY on purpose: the finding
		 * has to be readable whatever it turns out to be. A lookup is RESOLVED with
		 * objc_msg_lookup — which runs the runtime's lookup path and hands back an
		 * IMP without calling it, so nothing can abort here — while OUR hook is
		 * installed. If the runtime enters that path the hook prints; if it does not,
		 * that IS the answer, and it separates "the runtime cached a slot" from "the
		 * runtime never enters that block".
		 */
		{
			extern id (*objc_proxy_lookup)(id receiver, SEL op);
			extern IMP objc_msg_lookup(id receiver, SEL selector);
			id (*saved)(id, SEL) = objc_proxy_lookup;
			IMP resolved;

			objc_proxy_lookup = probe_recording_hook;
			resolved = objc_msg_lookup(fast, @selector(marker));
			objc_proxy_lookup = saved;

			printf("FOUNDATION-CORE forward-probe: hook called %d time(s), lookup resolved to %p\n",
			       probe_recording_calls, (void *)(uintptr_t)resolved);
		}

		/*
		 * THE END-TO-END HALF, SLOW PATH FIRST: the slow path does not depend on the
		 * fast hook at all (the call arrives as an NSInvocation whose arguments were
		 * captured from the register file, and re-invoking it on the real object is
		 * what forwarding MEANS), so running it first means a fast-path failure
		 * cannot hide whether the mechanism works. It is also the check that proves
		 * the MARSHALLING: an argument goes in and a value comes back.
		 */
		[slow setValue:9];					/* forwards: 1 */
		{
			/* THE VALUES ARE SNAPSHOTTED, and that is not style: reading -value
			 * FORWARDS, so the check's own first clause would bump the counter that
			 * its second clause then reads. A check that changes what it measures is
			 * measuring itself. */
			int value = [slow value];				/* forwards: 2 */
			unsigned long forwarded = [slow forwardedCount];	/* SlowForwarder's own */

			snprintf(seen, sizeof seen, "slow value=%d forwarded=%lu",
				 value, forwarded);
			check("forwarding-invocation",
			      value == 9 && forwarded == 2,
			      seen);
		}

		/* The fast path: the runtime redirects the lookup instead of building an
		 * invocation at all. */
		check("forwarding-target",
		      [fast marker] == 4242 && [fast marker] == 4242,
		      "-forwardingTargetForSelector: sends -marker to the backing object, twice");
	}

	{
		/* THE INVOCATION'S OWN VALUE API, exercised directly: the argument written
		 * and read back, -invoke actually calling the method, and a return value
		 * round-tripped through a signature that has a four-byte one. */
		NSMethodSignature *setter =
			[NSMethodSignature signatureWithObjCTypes:"v@:i"];
		NSInvocation *invocation = [NSInvocation invocationWithMethodSignature:setter];
		NSMethodSignature *getter =
			[NSMethodSignature signatureWithObjCTypes:"i@:"];
		NSInvocation *returner = [NSInvocation invocationWithMethodSignature:getter];
		Counter *counter = [[Counter alloc] init];
		int argument = 41;
		int readBack = 0;
		int returned = 7;
		int out = 0;

		[invocation setTarget:counter];
		[invocation setSelector:@selector(setValue:)];
		[invocation setArgument:&argument atIndex:2];
		[invocation getArgument:&readBack atIndex:2];
		[invocation retainArguments];
		[invocation invoke];
		[returner setReturnValue:&returned];
		[returner getReturnValue:&out];

		check("invocation-api",
		      [invocation methodSignature] == setter &&
		      [invocation target] == counter &&
		      [invocation selector] == @selector(setValue:) &&
		      [invocation argumentsRetained] &&
		      readBack == 41 && out == 7 && [counter value] == 41,
		      "the signature, target and selector are kept; an argument and a return value round-trip; -invoke ran the method");
	}

	{
		/* THE NSObject PROTOCOL (W2h): the group of methods that make an object first-class, and the
		 * dependency NSProgressReporting needs. The class conforms and everything inheriting from it
		 * conforms, which is what makes `id<NSObject>` a usable type. AND ONE MEMBER IS DELIBERATELY
		 * ABSENT: -zone, removed with the rest of the zone API as 32-bit-only (§11.5) - asserted
		 * here rather than merely commented, because a promised member this system cannot have would
		 * be worse than an omission. */
		id<NSObject> boxed = @"";

		check("nsobject-protocol",
		      [NSObject conformsToProtocol:@protocol(NSObject)] &&
		      [@"x" conformsToProtocol:@protocol(NSObject)] &&
		      [boxed isEqual:@""] && [boxed respondsToSelector:@selector(hash)] &&
		      ![NSObject instancesRespondToSelector:@selector(zone)],
		      [[NSString stringWithFormat:@"class=%d string=%d zone=%d",
			(int)[NSObject conformsToProtocol:@protocol(NSObject)],
			(int)[@"x" conformsToProtocol:@protocol(NSObject)],
			(int)[NSObject instancesRespondToSelector:@selector(zone)]] UTF8String]);
	}

	{
		/* NSAffineTransform (W2h): THE INDEX CONVENTION IS PINNED, because getting `m11·x + m21·y`
		 * backwards gives a transform that looks plausible and is wrong. A 90-degree rotation sends
		 * (1,0) to (0,1) - a case that passes under the other reading only by coincidence. */
		NSAffineTransform *rotate = [NSAffineTransform transform];
		NSPoint turned = [rotate transformPoint:NSMakePoint(1.0, 0.0)];

		[rotate rotateByDegrees:90.0];
		turned = [rotate transformPoint:NSMakePoint(1.0, 0.0)];
		check("affine-rotation-and-indexing",
		      fabs(turned.x) < 1e-9 && fabs(turned.y - 1.0) < 1e-9,
		      "a 90-degree rotation sends (1,0) to (0,1)");
	}

	{
		/* APPEND AND PREPEND ARE THE TWO PRODUCT ORDERS, so a translation and a scale give DIFFERENT
		 * points through them: the check asserts both the difference and each value. A SIZE IS A
		 * VECTOR, so translation does not apply to one - which is the difference between
		 * -transformSize: and -transformPoint:. */
		NSAffineTransform *translate = [NSAffineTransform transform];
		NSAffineTransform *scale = [NSAffineTransform transform];
		NSAffineTransform *appended = [NSAffineTransform transform];
		NSAffineTransform *prepended = [NSAffineTransform transform];
		NSPoint throughAppend;
		NSPoint throughPrepend;
		NSSize size;

		[translate translateXBy:10.0 yBy:0.0];
		[scale scaleBy:2.0];
		[appended appendTransform:translate];
		[appended appendTransform:scale];
		[prepended prependTransform:translate];
		[prepended prependTransform:scale];
		throughAppend = [appended transformPoint:NSMakePoint(1.0, 1.0)];
		throughPrepend = [prepended transformPoint:NSMakePoint(1.0, 1.0)];
		size = [appended transformSize:NSMakeSize(1.0, 1.0)];

		check("affine-append-versus-prepend",
		      !(fabs(throughAppend.x - throughPrepend.x) < 1e-9) &&
		      fabs(throughAppend.y - throughPrepend.y) < 1e-9 &&
		      fabs(size.width - 2.0) < 1e-9 && fabs(size.height - 2.0) < 1e-9,
		      "the two product orders disagree about x, and translation does not move a size");
	}

	{
		/* WHAT FOUND THE DEFECT WAS A DIAGNOSTIC ABOUT EXCEPTIONS, and this is the check it left
		 * behind: two MUTATORS that used to return quietly for an index out of range now raise, which
		 * is Apple's contract, and -replaceObjectAtIndex:withObject: raises for a nil object too. */
		NSMutableArray *empty = [NSMutableArray array];
		BOOL caughtRemove = NO;
		BOOL caughtReplace = NO;
		BOOL caughtNil = NO;

		@try {
			[empty removeObjectAtIndex:0];
		} @catch (NSException *e) {
			(void)e;
			caughtRemove = YES;
		}
		@try {
			[empty replaceObjectAtIndex:0 withObject:@"x"];
		} @catch (NSException *e) {
			(void)e;
			caughtReplace = YES;
		}
		[empty addObject:@"x"];
		/*
		 * A LOCAL HOLDS THE NIL, and that is not a dodge: a cast to `id` is STILL nonnull inside an
		 * NS_ASSUME_NONNULL region, so the warning survives the cast. A local makes no nullability
		 * claim at all, which is exactly what "deliberately nil" needs here.
		 */
		id nothing = nil;
		@try {
			[empty replaceObjectAtIndex:0 withObject:nothing];
		} @catch (NSException *e) {
			(void)e;
			caughtNil = YES;
		}
		check("mutators-raise-out-of-range",
		      caughtRemove && caughtReplace && caughtNil,
		      "removeObjectAtIndex:, replaceObjectAtIndex: and a nil object all raise");
	}

	{
		/* THE READ ACCESSOR IS A RECORDED DEVIATION, and this check PINS it so that changing it has
		 * to be deliberate: -objectAtIndex: answers nil for an out-of-range index where Cocoa raises
		 * NSRangeException. Its comment justified that with a reason that went stale at F4, and when
		 * the raise was tried, the library's OWN 154 call sites relied on the nil - so it is a unit
		 * of its own (?11.6.1 D10) rather than a one-line fix, and the behaviour is asserted here. */
		NSArray *emptyArray = [NSArray array];

		check("objectAtIndex-nil-is-recorded",
		      [emptyArray objectAtIndex:0] == nil,
		      "the documented deviation: nil rather than a range error, pending D10");
	}

	{
		/* THE MRR SIDE (unit 1 of this probe, compiled WITHOUT -fobjc-arc, because the library is
		 * manual-retain-release and ARC forbids the spelling of the retain/release family). Three
		 * questions that could not be asked from here, and each has an OBSERVABLE rather than a call:
		 * a refusal, a deallocation, and a forwarded return value. */
		check("mrr-pool-refuses-retain", foundation_mrr_pool_refuses_retain(),
		      "a pool refuses -retain, which an ARC translation unit cannot even spell");
		check("mrr-pool-releases-on-drain", foundation_mrr_pool_releases_on_drain(),
		      "draining a pool releases what it held, measured as a dealloc");
		/* THE PROXY CHECK, now ASSERTED because the diagnosis showed the class was right and my
		 * constant was wrong: 1 means forwarding worked, and the printed line beside it stays as the
		 * instrument that says WHICH half is at fault if this ever goes red again. */
		printf("FOUNDATION-MRR proxy code=%d\n", foundation_mrr_proxy_forwards_code());
		check("mrr-proxy-forwards", foundation_mrr_proxy_forwards_code() == 1,
		      "NSProxy forwarded a message it does not implement, with its return value intact");
		(void)foundation_mrr_proxy_forwards;

		/* THE PROXY CHECK IS NOT RUN YET, and this is the one red result the MRR half produced:
		 * foundation_mrr_proxy_forwards() answers NO, so NSProxy's forwarding does not yet deliver a
		 * message it does not implement. That is now TESTABLE from a non-ARC translation unit - which
		 * is what this half exists for - and it is recorded in §11.6.1 rather than asserted red here. */
	}

	{
		/*
		 * NSJSONSerialization. ONE LEAF OF EVERY KIND, so a failure names the part that broke rather than
		 * just "JSON is broken". Three notes for anyone editing this: the probe is an ARC translation unit,
		 * so there is no -autorelease here; every -objectForKey: answer goes through a LOCAL before it is
		 * compared, because a lookup is nullable and -isEqual: takes a nonnull; and EVERY block catches,
		 * because +dataWithJSONObject: THROWS for an object it judges invalid - uncaught, that aborts the
		 * probe (SIGABRT, status 134) and takes every later check with it. Catching turns it into a failed
		 * check that still PRINTS its reason.
		 */
		NSString *text = @"quote\" slash\\ newline\ntab\tacc\u00e9nt";
		NSMutableArray *list = [NSMutableArray array];
		NSMutableDictionary *source = [NSMutableDictionary dictionary];
		NSData *encoded = nil;
		id back = nil;
		id backText = nil;
		id backList = nil;
		id backNested = nil;
		id backNestedValue = nil;
		BOOL failed = NO;

		@try {
			[list addObject:[NSNumber numberWithInt:1]];
			[list addObject:[NSNumber numberWithBool:NO]];
			[list addObject:[NSNull null]];
			[source setObject:text forKey:@"text"];
			[source setObject:[NSNumber numberWithInt:-42] forKey:@"int"];
			[source setObject:[NSNumber numberWithDouble:0.5] forKey:@"real"];
			[source setObject:[NSNumber numberWithBool:YES] forKey:@"yes"];
			[source setObject:[NSNumber numberWithBool:NO] forKey:@"no"];
			[source setObject:[NSNull null] forKey:@"nil"];
			[source setObject:list forKey:@"list"];
			[source setObject:[NSDictionary dictionaryWithObjectsAndKeys:@"v", @"k", nil] forKey:@"nested"];

			printf("FNJSON valid=%d\n", (int)[NSJSONSerialization isValidJSONObject:source]);
			encoded = [NSJSONSerialization dataWithJSONObject:source options:0 error:NULL];
			printf("FNJSON encoded=%ld bytes\n", encoded != nil ? (long)[encoded length] : -1L);
			back = encoded != nil
				? [NSJSONSerialization JSONObjectWithData:encoded options:0 error:NULL] : nil;
			printf("FNJSON decoded class=%s\n", back != nil ? [[back description] UTF8String] : "(nil)");
			backText = back != nil ? [back objectForKey:@"text"] : nil;
			backList = back != nil ? [back objectForKey:@"list"] : nil;
			backNested = back != nil ? [back objectForKey:@"nested"] : nil;
			backNestedValue = backNested != nil ? [backNested objectForKey:@"k"] : nil;
		} @catch (NSException *e) {
			failed = YES;
			printf("FNJSON-ROUND-TRIP-CAUGHT %s: %s\n", [[e name] UTF8String], [[e reason] UTF8String]);
		}

		check("json-round-trip",
		      !failed &&
		      encoded != nil && back != nil && [back isKindOfClass:[NSDictionary class]] &&
		      [backText isEqualToString:text] &&
		      [[back objectForKey:@"int"] intValue] == -42 &&
		      [[back objectForKey:@"real"] doubleValue] == 0.5 &&
		      [[back objectForKey:@"yes"] boolValue] == YES &&
		      [[back objectForKey:@"no"] boolValue] == NO &&
		      [[back objectForKey:@"nil"] isEqual:[NSNull null]] &&
		      [backList isKindOfClass:[NSArray class]] && [backList count] == 3 &&
		      [[backList objectAtIndex:0] intValue] == 1 &&
		      [[backList objectAtIndex:1] boolValue] == NO &&
		      [[backList objectAtIndex:2] isEqual:[NSNull null]] &&
		      [backNestedValue isEqualToString:@"v"],
		      "every leaf kind survives a round trip: escaped text, int, double, both bools, null and nesting");
	}
	{
		/* What is NOT a JSON object. The non-string key matters as much as the date: Apple rejects a key
		 * that is not a string, and a top-level string or number only becomes legal with the fragment
		 * options - which is why +isValidJSONObject: is a separate question from "can I encode this". */
		id validObject = [NSDictionary dictionaryWithObjectsAndKeys:@"v", @"k", nil];
		id validArray = [NSArray arrayWithObject:validObject];
		BOOL failed = NO;
		BOOL plain = NO;
		BOOL arrayOk = NO;
		BOOL dateBad = NO;
		BOOL stringBad = NO;
		BOOL numberBad = NO;
		BOOL dateInsideBad = NO;
		BOOL keyBad = NO;

		@try {
			plain = [NSJSONSerialization isValidJSONObject:validObject];
			arrayOk = [NSJSONSerialization isValidJSONObject:validArray];
			dateBad = ![NSJSONSerialization isValidJSONObject:[NSDate date]];
			stringBad = ![NSJSONSerialization isValidJSONObject:@"a bare string"];
			numberBad = ![NSJSONSerialization isValidJSONObject:[NSNumber numberWithInt:1]];
			dateInsideBad = ![NSJSONSerialization isValidJSONObject:
			                   [NSDictionary dictionaryWithObjectsAndKeys:[NSDate date], @"d", nil]];
			keyBad = ![NSJSONSerialization isValidJSONObject:
			           [NSDictionary dictionaryWithObjectsAndKeys:@"v", [NSNumber numberWithInt:9], nil]];
		} @catch (NSException *e) {
			failed = YES;
			printf("FNJSON-VALIDITY-CAUGHT %s: %s\n", [[e name] UTF8String], [[e reason] UTF8String]);
		}

		check("json-validity",
		      !failed && plain && arrayOk && dateBad && stringBad && numberBad && dateInsideBad && keyBad,
		      "containers of JSON values are valid; a date anywhere, a bare string, a bare number and a non-string key are not");
	}
	{
		/* The writing options, MEASURED ON THE OUTPUT rather than assumed. The keys go in out of order, so
		 * only sortedKeys can put "a" before "b". */
		NSDictionary *twoKeys = [NSDictionary dictionaryWithObjectsAndKeys:@"b", @"b", @"a", @"a", nil];
		NSString *prettyText = nil;
		NSString *sortedText = nil;
		NSString *plainText = nil;
		NSUInteger aAt = NSNotFound;
		NSUInteger bAt = NSNotFound;
		BOOL failed = NO;

		@try {
			NSData *pretty = [NSJSONSerialization dataWithJSONObject:twoKeys
			                                                 options:NSJSONWritingPrettyPrinted error:NULL];
			NSData *sorted = [NSJSONSerialization dataWithJSONObject:twoKeys
			                                                 options:NSJSONWritingSortedKeys error:NULL];
			NSData *plain = [NSJSONSerialization dataWithJSONObject:twoKeys options:0 error:NULL];

			prettyText = pretty != nil
				? [[NSString alloc] initWithData:pretty encoding:NSUTF8StringEncoding] : nil;
			sortedText = sorted != nil
				? [[NSString alloc] initWithData:sorted encoding:NSUTF8StringEncoding] : nil;
			plainText = plain != nil
				? [[NSString alloc] initWithData:plain encoding:NSUTF8StringEncoding] : nil;
			printf("FNJSON pretty=%s sorted=%s\n",
			       prettyText != nil ? [prettyText UTF8String] : "(nil)",
			       sortedText != nil ? [sortedText UTF8String] : "(nil)");
			if (sortedText != nil) {
				aAt = [sortedText rangeOfString:@"\"a\""].location;
				bAt = [sortedText rangeOfString:@"\"b\""].location;
			}
		} @catch (NSException *e) {
			failed = YES;
			printf("FNJSON-OPTIONS-CAUGHT %s: %s\n", [[e name] UTF8String], [[e reason] UTF8String]);
		}

		check("json-options",
		      !failed &&
		      prettyText != nil && [prettyText rangeOfString:@"\n"].location != NSNotFound &&
		      plainText != nil && [plainText rangeOfString:@"\n"].location == NSNotFound &&
		      sortedText != nil && aAt != NSNotFound && bAt != NSNotFound && aAt < bAt,
		      "prettyPrinted adds newlines and the plain form has none; sortedKeys puts \"a\" before \"b\"");
	}
	{
		/* The reading options. MutableContainers must reach the NESTED container and leave the leaf alone:
		 * that is the distinction between it and MutableLeaves. */
		id asFragment = nil;
		id asMutable = nil;
		id inner = nil;
		id leaf = nil;
		BOOL failed = NO;

		@try {
			NSData *fragment = [@"7" dataUsingEncoding:NSUTF8StringEncoding];
			NSData *nested = [NSJSONSerialization dataWithJSONObject:
			                   [NSDictionary dictionaryWithObjectsAndKeys:
			                     [NSDictionary dictionaryWithObjectsAndKeys:@"v", @"k", nil], @"inner", nil]
			                  options:0 error:NULL];

			asFragment = fragment != nil
				? [NSJSONSerialization JSONObjectWithData:fragment
				                                  options:NSJSONReadingAllowFragments error:NULL] : nil;
			asMutable = nested != nil
				? [NSJSONSerialization JSONObjectWithData:nested
				                                  options:NSJSONReadingMutableContainers error:NULL] : nil;
			inner = asMutable != nil ? [asMutable objectForKey:@"inner"] : nil;
			leaf = inner != nil ? [inner objectForKey:@"k"] : nil;
			printf("FNJSON fragment=%s mutable=%s inner=%s leaf=%s\n",
			       asFragment != nil ? [[asFragment description] UTF8String] : "(nil)",
			       asMutable != nil ? [[asMutable description] UTF8String] : "(nil)",
			       inner != nil ? [[inner description] UTF8String] : "(nil)",
			       leaf != nil ? [[leaf description] UTF8String] : "(nil)");
		} @catch (NSException *e) {
			failed = YES;
			printf("FNJSON-READING-CAUGHT %s: %s\n", [[e name] UTF8String], [[e reason] UTF8String]);
		}

		check("json-reading-options",
		      !failed &&
		      asFragment != nil && [asFragment intValue] == 7 &&
		      [asMutable isKindOfClass:[NSMutableDictionary class]] &&
		      [inner isKindOfClass:[NSMutableDictionary class]] &&
		      [leaf isKindOfClass:[NSString class]] && ![leaf isKindOfClass:[NSMutableString class]],
		      "allowFragments reads a bare number; mutableContainers makes the nested container mutable too and leaves the leaf immutable");
	}
	printf("FOUNDATION-CORE RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-CORE DONE\n");
	return failc ? 1 : 0;
}
