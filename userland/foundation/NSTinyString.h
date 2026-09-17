/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSTinyString — the class for clang's TAGGED constant strings.
 * docs/design/foundation-plan.md, F1.
 *
 * A `@"..."` literal of FEWER THAN 9 ASCII CHARACTERS is not an object at all.
 * clang packs the characters into the high 56 bits of the pointer, a 4-bit
 * length in bits 3-6, and the tag 4 in bits 0-2 (clang's own code:
 * CGObjCGNU.cpp:1005, "Tiny strings are only used on 64-bit platforms"). The
 * runtime then dispatches any pointer whose low three bits are non-zero through
 * `SmallObjectClasses[tag]` (class.h + objc_msgSend.x86_64.S) — so unless the
 * Foundation REGISTERS a class at tag 4, every short literal is dispatched to a
 * null class and faults. That is precisely how F1's first attempts died.
 *
 * Our runtime ships the API (`objc/runtime.h`: `objc_registerSmallObjectClass_np`)
 * and its own test harness shows the pattern (`Test/Test.m`): a subclass of the
 * constant-string class whose `+load` registers it at 4. This is that class.
 *
 * NOT IN THE UMBRELLA, deliberately: a consumer never names this class — writing
 * `@"..."` is how you make one — so it only has to be IN the library, where its
 * `+load` runs.
 *
 * THE PAYLOAD IS THE POINTER. There are no ivars to read and no memory to touch,
 * so the accessors decode the bits. The runtime already treats small objects as
 * persistent before it looks at the isa (`arc.mm:isPersistentObject`), so ARC's
 * retains and releases never dereference one; the no-op lifetime overrides below
 * are belt and braces, and match the runtime's own test.
 */

#ifndef FOUNDATION_NSTINYSTRING_H
#define FOUNDATION_NSTINYSTRING_H

#import <foundation/NSString.h>

@interface NSTinyString : NSConstantString
@end

#endif /* FOUNDATION_NSTINYSTRING_H */
