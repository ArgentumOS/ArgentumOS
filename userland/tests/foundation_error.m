/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_error, unit 2 of 2 — the checks (ARC).
 *
 *   error-api-complete     every public NSError selector exists
 *   exception-api-complete every public NSException selector exists
 *   error-value            domain/code/userInfo, the localised description from
 *                          the KEY and the fallback without it, -description's
 *                          shape, value equality, and that a MUTABLE userInfo is
 *                          copied rather than referenced
 *   exception-raise        @try/@catch end to end: -raise unwinds through the
 *                          runtime, and the caught object is the one thrown
 *   exception-format       +raise:format: builds the reason and throws
 *   error-cross-tu         values from the support unit compare equal to local ones
 */

#import "foundation_error.h"
#include <stdio.h>
#include <string.h>

static int okc, failc;

static void check(const char *name, int ok, const char *detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-ERROR %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-ERROR %s FAIL %s\n", name, detail ? detail : "");
	}
}

int main(void)
{
	{
		static const char *classSelectors[] = {
			"errorWithDomain:code:userInfo:", NULL
		};
		static const char *instanceSelectors[] = {
			"initWithDomain:code:userInfo:", "domain", "code", "userInfo",
			"localizedDescription", "localizedFailureReason",
			"isEqual:", "hash", "description", "copy", "mutableCopy", NULL
		};
		NSError *probe = [NSError errorWithDomain:@"D" code:1 userInfo:nil];
		int complete = 1;
		int i;

		for (i = 0; classSelectors[i] != NULL; i++) {
			if (![NSError respondsToSelector:sel_registerName(classSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-ERROR missing +%s\n", classSelectors[i]);
			}
		}
		for (i = 0; instanceSelectors[i] != NULL; i++) {
			if (![probe respondsToSelector:sel_registerName(instanceSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-ERROR missing -%s\n", instanceSelectors[i]);
			}
		}
		check("error-api-complete", complete,
		      "every public NSError selector exists (the hard rule)");
	}

	{
		static const char *classSelectors[] = {
			"exceptionWithName:reason:userInfo:", "raise:format:",
			"raise:format:arguments:", NULL
		};
		static const char *instanceSelectors[] = {
			"initWithName:reason:userInfo:", "name", "reason", "userInfo",
			"raise", "isEqual:", "hash", "description", "copy", "mutableCopy", NULL
		};
		NSException *probe = [NSException exceptionWithName:@"N" reason:@"r" userInfo:nil];
		int complete = 1;
		int i;

		for (i = 0; classSelectors[i] != NULL; i++) {
			if (![NSException respondsToSelector:sel_registerName(classSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-ERROR missing +%s\n", classSelectors[i]);
			}
		}
		for (i = 0; instanceSelectors[i] != NULL; i++) {
			if (![probe respondsToSelector:sel_registerName(instanceSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-ERROR missing -%s\n", instanceSelectors[i]);
			}
		}
		check("exception-api-complete", complete,
		      "every public NSException selector exists (the hard rule)");
	}

	{
		NSMutableDictionary *editable = [[NSMutableDictionary alloc] init];
		NSError *plain;
		NSError *described;
		NSError *same;
		NSError *differentCode;

		[editable setObject:@"the reason" forKey:NSLocalizedFailureReasonKey];
		plain = [NSError errorWithDomain:@"FNDomain" code:42 userInfo:editable];
		[editable setObject:@"changed afterwards" forKey:NSLocalizedFailureReasonKey];
		described = [NSError errorWithDomain:@"FNDomain" code:42
					     userInfo:[NSDictionary dictionaryWithObject:@"described"
										  forKey:NSLocalizedDescriptionKey]];
		same = [NSError errorWithDomain:@"FNDomain" code:42
				       userInfo:[NSDictionary dictionaryWithObject:@"the reason"
									    forKey:NSLocalizedFailureReasonKey]];
		differentCode = [NSError errorWithDomain:@"FNDomain" code:43 userInfo:nil];
		/* F6: built through the same nullable constructor, bound here so the check
		 * below can guard it. */
		NSError *secondPlain = [NSError errorWithDomain:@"FNDomain" code:42 userInfo:nil];

		check("error-value",
		      [[plain domain] isEqualToString:@"FNDomain"] && [plain code] == 42 &&
		      [[plain localizedFailureReason] isEqualToString:@"the reason"] &&
		      [[described localizedDescription] isEqualToString:@"described"] &&
		      [[plain localizedDescription] length] > 0 &&
		      [plain isEqual:same] &&
		      secondPlain != nil && ![plain isEqual:secondPlain] &&
		      ![plain isEqual:differentCode] &&
		      ![plain isEqual:@"not an error"] &&
		      [plain hash] == [same hash] &&
		      [plain hash] != [differentCode hash] &&
		      [plain copy] == plain &&
		      strstr([[plain description] UTF8String], "Error Domain=FNDomain Code=42") != NULL,
		      "the value, the key-driven and fallback descriptions, equality, and -description's shape");
	}

	{
		int caught = 0;
		NSString *name = nil;
		NSString *reason = nil;
		int finallyRan = 0;

		printf("FOUNDATION-ERROR step throw and catch\n");
		@try {
			[NSException raise:NSRangeException format:@"index %d is out of range", 7];
			printf("FOUNDATION-ERROR step after raise (unreachable)\n");
		} @catch (NSException *caughtException) {
			caught = 1;
			name = [caughtException name];
			reason = [caughtException reason];
			printf("FOUNDATION-ERROR caught name=%s reason=%s\n",
			       [name UTF8String], [reason UTF8String]);
		} @finally {
			finallyRan = 1;
		}

		check("exception-raise",
		      caught && finallyRan &&
		      [name isEqualToString:NSRangeException] &&
		      [reason isEqualToString:@"index 7 is out of range"] &&
		      [NSGenericException isEqualToString:@"NSGenericException"] &&
		      [NSInvalidArgumentException isEqualToString:@"NSInvalidArgumentException"] &&
		      [NSInternalInconsistencyException isEqualToString:@"NSInternalInconsistencyException"],
		      "@try/@catch/@finally unwind a raised NSException through the runtime, and the reason came from the format");
	}

	{
		int caught = 0;
		NSString *name = nil;

		printf("FOUNDATION-ERROR step throw a built exception\n");
		@try {
			@throw [NSException exceptionWithName:NSInvalidArgumentException
						       reason:@"thrown directly"
						     userInfo:nil];
		} @catch (NSException *caughtException) {
			caught = 1;
			name = [caughtException name];
		} @catch (id anything) {
			caught = 0;
		}

		check("exception-throw", caught && [name isEqualToString:NSInvalidArgumentException],
		      "@throw of a built exception lands in the matching @catch");
	}

	{
		NSError *fromSupport = foundation_error_built();
		NSException *exception = foundation_error_exception();
		int caught = 0;

		@try {
			[exception raise];
		} @catch (NSException *caughtException) {
			caught = [caughtException reason] != nil;
		}

		check("error-cross-tu",
		      [[fromSupport domain] isEqualToString:@"FNXSupportDomain"] &&
		      [fromSupport code] == 7 &&
		      [[fromSupport localizedFailureReason] isEqualToString:@"from the support unit"] &&
		      caught,
		      "values built in the support unit behave here");
	}

	printf("FOUNDATION-ERROR RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-ERROR DONE\n");
	return failc ? 1 : 0;
}
