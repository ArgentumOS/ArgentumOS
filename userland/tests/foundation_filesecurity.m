/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_filesecurity, unit of 1 — W8 slice 5's acceptance for NSFileSecurity.
 * docs/design/foundation-plan.md §60 and §11.6.1 D13.
 *
 * ONE unit, importing only <Foundation/Foundation.h>. NO FIXTURE: this class touches no file system, and
 * the interesting half of its surface is what is NOT on it.
 *
 * WHAT IT MEASURES, and every one is a rule from the class's own page:
 *   fs-is-a-container              Apple's Overview: "A stub class that encapsulates security information
 *                                  about a file" — an instance exists and is this class's;
 *   fs-conforms-as-apple-lists     the page's "Conforms To" list is NSCoding, NSCopying and
 *                                  NSSecureCoding, and the object answers `-conformsToProtocol:` to all
 *                                  three (the protocol question is one a program can ask on any day);
 *   fs-is-secure-codeable          the secure half answers, which is this tree's standing ruling
 *                                  (NSCoding.h) and not this class's private choice;
 *   fs-a-copy-is-its-own-object    the type's facts are SETTABLE through the bridge Apple documents, so
 *                                  `-copy` must hand back its own object rather than a second reference
 *                                  to one;
 *   fs-codes-and-comes-back        so an archive that names this class comes back AS this class;
 *   fs-refuses-a-foreign-archive   and bytes that are not an archive are refused - BY RAISING, which is
 *                                  Apple's own sentence for +unarchiveObjectWithData: - while a nil coder
 *                                  is refused with nil;
 *   fs-the-bridged-accessors-are-absent  THE BOUNDARY, ASSERTED RATHER THAN WRITTEN DOWN: Apple's own
 *                                  Overview says the class "contains no methods of its own" and "is
 *                                  transparently bridged to CFFileSecurity", whose C functions carry the
 *                                  owner, the group, the mode, the access control list and the two UUIDs
 *                                  - and this tree HAS NO COREFOUNDATION, so those facts are not
 *                                  reachable through this class here. The probe asks for each of them BY
 *                                  NAME and requires NO, so §11.6.1 D13 cannot rot into a silence.
 */

#import <Foundation/Foundation.h>

#include <stdio.h>

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-FILESECURITY %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-FILESECURITY %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

/* THE FACTS CFFileSecurity PUBLISHES, AS SELECTORS: a class that claims to carry them would answer
 * `-respondsToSelector:` for each. The names are the CF accessors' own (Owner/Group/Mode/
 * AccessControlList/OwnerUUID/GroupUUID, get and set), which is where Apple publishes them. */
static const char *kFactSelectors[] = {
	"owner", "setOwner:",
	"group", "setGroup:",
	"mode", "setMode:",
	"accessControlList", "setAccessControlList:",
	"ownerUUID", "setOwnerUUID:",
	"groupUUID", "setGroupUUID:",
};

int main(void)
{
	NSFileSecurity *subject = [[NSFileSecurity alloc] init];

	/* ---- THE PUBLISHED SURFACE ---------------------------------------------------------------- */
	check("fs-is-a-container",
	      subject != nil && [subject isKindOfClass:[NSFileSecurity class]] &&
	      [subject respondsToSelector:NSSelectorFromString(@"copy")],
	      [NSString stringWithFormat:@"the object is a %@", [subject class]]);

	{
		BOOL copying = [subject conformsToProtocol:@protocol(NSCopying)];
		BOOL coding = [subject conformsToProtocol:@protocol(NSCoding)];
		BOOL secure = [subject conformsToProtocol:@protocol(NSSecureCoding)];
		BOOL object = [subject conformsToProtocol:@protocol(NSObject)];

		check("fs-conforms-as-apple-lists",
		      copying && coding && secure && object,
		      [NSString stringWithFormat:@"copying=%d coding=%d secure=%d object=%d",
			(int)copying, (int)coding, (int)secure, (int)object]);
	}

	check("fs-is-secure-codeable", [NSFileSecurity supportsSecureCoding],
	      @"+supportsSecureCoding answers NO, where the conforming type answers YES");

	{
		NSFileSecurity *copy = [subject copy];

		check("fs-a-copy-is-its-own-object",
		      copy != nil && copy != subject && [copy isKindOfClass:[NSFileSecurity class]],
		      [NSString stringWithFormat:@"copy=%p subject=%p", (void *)copy, (void *)subject]);
	}

	{
		id archive = [NSKeyedArchiver archivedDataWithRootObject:subject];
		id decoded = archive != nil ? [NSKeyedUnarchiver unarchiveObjectWithData:archive] : nil;

		check("fs-codes-and-comes-back",
		      archive != nil && decoded != nil && [decoded isKindOfClass:[NSFileSecurity class]],
		      [NSString stringWithFormat:@"archive=%lu bytes decoded=%@",
			(unsigned long)(archive != nil ? [archive length] : 0),
			decoded != nil ? [decoded class] : (id)@"nil"]);
	}

	{
		/* THE REFUSAL IS A RAISE, BECAUSE APPLE'S OWN PAGE SAYS SO: "+unarchiveObjectWithData:" "raises
		 * an NSInvalidArgumentException if data is not a valid archive". My first version of this leg
		 * compared the answer to nil and the probe ABORTED (status 134, "Aborted" in the guest log) -
		 * the instrument was wrong, not the library. The leg therefore CATCHES, and asserts Apple's
		 * raise, while the nil answer is asserted where it belongs: the coder door. */
		NSData *garbage = [@"not an archive at all" dataUsingEncoding:NSUTF8StringEncoding];
		BOOL raised = NO;
		NSString *why = @"nothing was raised - the bytes were accepted";
		id decoded = nil;

		@try {
			decoded = [NSKeyedUnarchiver unarchiveObjectWithData:garbage];
		}
		@catch (NSException *exception) {
			raised = YES;
			why = [exception name];
		}
		check("fs-refuses-a-foreign-archive",
		      raised && decoded == nil && [[NSFileSecurity alloc] initWithCoder:nil] == nil,
		      [NSString stringWithFormat:@"raised=%@ (%@), decoded=%@, nil-coder=%@",
			raised ? @"yes" : @"no", why,
			decoded == nil ? @"nil" : @"an object",
			[[NSFileSecurity alloc] initWithCoder:nil] == nil ? @"nil" : @"an object"]);
	}

	{
		NSMutableArray *present = [NSMutableArray array];
		size_t i;

		for (i = 0; i < sizeof(kFactSelectors) / sizeof(kFactSelectors[0]); i++) {
			NSString *name = [NSString stringWithUTF8String:kFactSelectors[i]];

			if ([subject respondsToSelector:NSSelectorFromString(name)]) {
				[present addObject:name];
			}
		}
		check("fs-the-bridged-accessors-are-absent",
		      [present count] == 0,
		      [NSString stringWithFormat:@"this class answers for %lu of the %lu facts CFFileSecurity "
			@"publishes, so the D13 boundary has moved and the row must be updated: %@",
			(unsigned long)[present count],
			(unsigned long)(sizeof(kFactSelectors) / sizeof(kFactSelectors[0])),
			[present componentsJoinedByString:@", "]]);
	}

	printf("FOUNDATION-FILESECURITY RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-FILESECURITY DONE\n");
	return failc == 0 ? 0 : 1;
}
