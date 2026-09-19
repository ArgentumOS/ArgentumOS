/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * sterlingc_k1 — the guest half of Sterling K1 (docs/design/sterling-plan.md §4).
 *
 * K1's host legs prove the emitted TEXT: `sterlingc.sh --golden` diffs it against
 * the document byte-for-byte, `--corpus` / `--reject` prove the parser reads the
 * surface, and `sterlingc-compile.sh` proves it *compiles* under -fobjc-arc
 * against libobjc2. None of them runs anything. This unit is the other half — it
 * is linked WITH the compiler's own output and executed on a guest boot, so the
 * whole chain is exercised:
 *
 *     MyClass.ag -> sterlingc -> MyClass.h/.m -> clang -> libobjc2 -> Foundation
 *
 * WHY THIS DRIVER IS HAND-WRITTEN OBJC, NOT STERLING. §1's specimen is a class
 * and nothing else — no top-level statements — so something has to instantiate
 * it and call it, and the emitter cannot yet emit calls or statement bodies
 * (that is K2's widening; the plan says so, and
 * tools/sterlingc/tests/probe/sterlingc_k1_probe.ag is the Sterling version of
 * this file, kept as the target for when it can). The
 * point of K1 is the *chain*, not the coverage: everything below the `#import`
 * is ordinary ObjC reaching into a class that a `.ag` file produced.
 *
 * `MyClass.h`/`MyClass.m` are generated at build time and are NOT in the tree —
 * this file is compiled with `-I` pointing at the directory sterlingc wrote.
 */
#import "MyClass.h"
#import <objc/runtime.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

/*
 * §2 emits `callSomeFunc(arg1);` into MyClass.m's `+baz:arg2:`, with a local
 * `extern void callSomeFunc(BOOL arg);` in the same unit. The spec's "labeled
 * call to a C function" drops the label at the boundary, so the linker needs a
 * definition here — and recording the argument is how the check proves the
 * label was dropped onto the *right position* rather than merely that some call
 * happened.
 */
static int cfunc_called;
static int cfunc_arg;

void callSomeFunc(BOOL arg)
{
	cfunc_called = 1;
	cfunc_arg = arg ? 1 : 0;
}

static int okc, failc;

static void check(const char *name, int ok, const char *detail)
{
	if (ok) {
		okc++;
		printf("STERLING %s ok\n", name);
	} else {
		failc++;
		printf("STERLING %s FAIL %s\n", name, detail ? detail : "");
	}
}

int main(void)
{
	MyClass *m = [[MyClass alloc] init];
	MyClass *fresh = [[MyClass alloc] init];
	const char *cls;

	check("init", m != nil, "[[MyClass alloc] init] returned nil");

	if (m == nil) {
		printf("STERLING RESULT ok=%d fail=%d\n", okc, failc);
		printf("STERLING DONE\n");
		return 1;
	}

	/* The emitted class IS the Sterling class, by name — not a substitute. */
	cls = object_getClassName(m);
	check("class-name", cls != NULL && strcmp(cls, "MyClass") == 0,
	      cls ? cls : "(nil)");

	/*
	 * `class MyClass: Object` emits `: NSObject`, so this is what proves the
	 * Foundation is linked and that MyClass really descends from it. A driver
	 * that only made instances would pass against a stub root class.
	 */
	check("foundation-superclass", [m isKindOfClass:[NSObject class]] == YES,
	      "MyClass is not a kind of NSObject");

	/* `property myValue: Int64` stores: @property + auto-synthesis. */
	check("stored-default", fresh.myValue == 0,
	      "an unset stored property is not its zero value");

	m.myValue = 42;
	check("stored-property", m.myValue == 42, "the stored property lost its value");

	/*
	 * `readonly property dynamicValue: Float32 { return 0.1; }` declares no
	 * storage and emits the accessor from the block (§2). Compared with a
	 * tolerance because 0.1 is not exact in binary — but the emitted literal is
	 * `0.1f` and this is `0.1f`, so exact equality would also hold; the epsilon
	 * documents the intent rather than papering over a rounding bug.
	 */
	check("readonly-property", fabsf(m.dynamicValue - 0.1f) < 1e-6f,
	      "the readonly getter did not return 0.1");

	/* `class method baz(arg1:arg2:)` — a class method that calls C. */
	cfunc_called = 0;
	cfunc_arg = -1;
	[MyClass baz:YES arg2:@"unused-in-this-check"];
	check("class-method-called-c", cfunc_called == 1 && cfunc_arg == 1,
	      "the class method did not call callSomeFunc with its first argument");

	/*
	 * A second call with NO proves the argument is *forwarded* rather than a
	 * constant baked into the emitted call site — the check above would pass
	 * against `callSomeFunc(YES)`.
	 */
	cfunc_called = 0;
	cfunc_arg = -1;
	[MyClass baz:NO arg2:@"x"];
	check("class-method-forwards-arg", cfunc_called == 1 && cfunc_arg == 0,
	      "the emitted C call site does not forward its argument");

	/* `method foobar(argname:arg2:) -> Bool` — an instance method. */
	check("instance-method", [m foobar:7 arg2:NO] == NO,
	      "the instance method did not return false");

	printf("STERLING RESULT ok=%d fail=%d\n", okc, failc);
	printf("STERLING DONE\n");
	return failc ? 1 : 0;
}
