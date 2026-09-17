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
			"methodSignatureForSelector:", NULL
		};
		static const char *excluded[] = {
			/* Needs NSInvocation — and with it -forwardInvocation:, the half of the
			 * forwarding trio that must MARSHAL the arguments. Stage F's second half;
			 * until then an unimplemented message still reaches
			 * -doesNotRecognizeSelector: and aborts loudly. */
			"forwardInvocation:", "forwardingTargetForSelector:", NULL
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
		      [NSMethodSignature signatureWithObjCTypes:NULL] == nil,
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

	printf("FOUNDATION-CORE RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-CORE DONE\n");
	return failc ? 1 : 0;
}
