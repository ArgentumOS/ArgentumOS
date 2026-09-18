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
			"alloc", "allocWithZone:", "new", "class", "superclass",
			"conformsToProtocol:", "respondsToSelector:",
			"instancesRespondToSelector:", "load", "initialize",
			"methodSignatureForSelector:", NULL
		};
		static const char *instanceSelectors[] = {
			"init", "copy", "mutableCopy", "copyWithZone:", "mutableCopyWithZone:",
			"retain", "release", "autorelease", "retainCount", "dealloc",
			"class", "superclass", "isKindOfClass:", "isMemberOfClass:",
			"respondsToSelector:", "conformsToProtocol:",
			"performSelector:", "performSelector:withObject:",
			"performSelector:withObject:withObject:", "methodForSelector:",
			"doesNotRecognizeSelector:", "isEqual:", "hash", "description",
			"debugDescription", "self", "zone", "isProxy",
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

	printf("FOUNDATION-CORE RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-CORE DONE\n");
	return failc ? 1 : 0;
}
