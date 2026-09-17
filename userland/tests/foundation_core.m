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
			"instancesRespondToSelector:", "load", "initialize", NULL
		};
		static const char *instanceSelectors[] = {
			"init", "copy", "mutableCopy", "copyWithZone:", "mutableCopyWithZone:",
			"retain", "release", "autorelease", "retainCount", "dealloc",
			"class", "superclass", "isKindOfClass:", "isMemberOfClass:",
			"respondsToSelector:", "conformsToProtocol:",
			"performSelector:", "performSelector:withObject:",
			"performSelector:withObject:withObject:", "methodForSelector:",
			"doesNotRecognizeSelector:", "isEqual:", "hash", "description",
			"debugDescription", "self", "zone", "isProxy", NULL
		};
		static const char *excluded[] = {
			/* Needs NSInvocation/NSMethodSignature, which this Foundation does not
			 * ship; an unimplemented message reaches -doesNotRecognizeSelector: and
			 * aborts loudly instead. */
			"forwardInvocation:", "methodSignatureForSelector:",
			"forwardingTargetForSelector:", NULL
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

	printf("FOUNDATION-CORE RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-CORE DONE\n");
	return failc ? 1 : 0;
}
