/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * objc_smoke, unit 2 of 2 - the CATEGORY on the other unit's class, the ARC
 * side, and the checks.
 *
 * Compiled with -fobjc-arc (the unit above is not), which is the normal mix: ARC
 * is a per-file choice, and the root class cannot be ARC.
 */

#include "objc_smoke.h"
#include <stdio.h>
#include <string.h>

@implementation SmokeGreeter (SmokeExtra)
- (int)twice { return self.count * 2; }
@end

static int okc, failc;

static void check(const char *name, int ok, const char *detail)
{
	if (ok) {
		okc++;
		printf("OBJC-SMOKE %s ok\n", name);
	} else {
		failc++;
		printf("OBJC-SMOKE %s FAIL %s\n", name, detail ? detail : "");
	}
}

int main(void)
{
	SmokeGreeter *g = [[SmokeGreeter alloc] init];	/* ARC + cross-TU class */
	const char *cls;
	int locked = 0, pooled = 0, caught = 0;

	g.count = 21;

	cls = object_getClassName(g);
	check("class", cls != NULL && strcmp(cls, "SmokeGreeter") == 0,
	      cls ? cls : "(nil)");
	check("greet", [g greet] != NULL &&
	      strcmp([g greet], "hello from an ObjC method") == 0,
	      [g greet] ? [g greet] : "(nil)");
	check("origin", [g origin] != NULL &&
	      strcmp([g origin], "support-tu") == 0,
	      [g origin] ? [g origin] : "(nil)");
	check("category.twice", [g twice] == 42, "a category implemented in THIS unit");
	check("protocol", class_conformsToProtocol(object_getClass(g),
	      @protocol(SmokeGreeting)) ? 1 : 0, "the class declares <SmokeGreeting>");
	check("property", g.count == 21, "@property/@synthesize and ivar offsets");
	check("block", blockSum(5) == 15, "stack block + Block_copy to the heap");
	{
		int (^blk)(int) = ^(int x) { return x * 3; };
		check("arc.block", blk(4) == 12, "ARC retains/releases a block");
	}
	@try {
		@throw g;
	} @catch (id e) {
		caught = (object_getClassName(e) != NULL &&
			  strcmp(object_getClassName(e), "SmokeGreeter") == 0);
	}
	check("catch", caught, "@throw/@catch through the ObjC personality");
	@synchronized (g) { locked = 1; }
	check("synchronized", locked == 1, "objc_sync_enter/exit");
	@autoreleasepool {
		SmokeGreeter *tmp = [[SmokeGreeter alloc] init];
		tmp.count = 7;
		pooled = tmp.count;
	}
	check("pool", pooled == 7, "@autoreleasepool");

	printf("OBJC-SMOKE RESULT ok=%d fail=%d\n", okc, failc);
	printf("OBJC-SMOKE DONE\n");
	return failc ? 1 : 0;
}
