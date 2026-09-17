/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * objc_smoke - the Objective-C acceptance probe (docs/design/objc-toolchain-plan.md).
 *
 * TWO TRANSLATION UNITS ON PURPOSE. The class under test and its root class are
 * implemented in objc_smoke_support.m; the CATEGORY on that class and the checks
 * are in objc_smoke.m. Cross-translation-unit class registration is the case
 * that was once misdiagnosed here (see the plan's P1 record), so the probe keeps
 * it in the acceptance forever: a class implemented in one unit must be usable,
 * with its category, from another.
 */

#ifndef OBJC_SMOKE_H
#define OBJC_SMOKE_H

#include <objc/runtime.h>

/*
 * The root class. IT MUST DECLARE STORAGE FOR THE isa: class_createInstance()
 * returns nil when instance_size < sizeof(Class) (runtime.c:360), and a class
 * with no instance variables has size 0 - measured. A "minimal" root class with
 * no ivars therefore produces no instances at all, silently.
 */
@interface SmokeObject { Class isa; }
+ (id)alloc;
- (id)init;
- (id)retain;
- (oneway void)release;
- (id)autorelease;
- (unsigned long)retainCount;
@end

@protocol SmokeGreeting
- (const char *)greet;
@end

@interface SmokeGreeter : SmokeObject <SmokeGreeting>
@property (nonatomic) int count;
- (const char *)greet;
- (const char *)origin;
@end

/* The category, IMPLEMENTED IN THE OTHER UNIT than the class it extends. */
@interface SmokeGreeter (SmokeExtra)
- (int)twice;
@end

/* Blocks are exercised on the MRR side: Block_copy's macros need bridged casts
 * under ARC, and one of the two units has to be MRR anyway. */
int blockSum(int n);

#endif /* OBJC_SMOKE_H */
