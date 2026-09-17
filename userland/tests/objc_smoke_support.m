/*
 * objc_smoke, unit 1 of 2 - the root class, the class under test, and blocks.
 *
 * MRR on purpose: ARC forbids implementing -retain/-release in ARC code, and a
 * root class has to implement them for ARC to work on its instances at all.
 */

#include "objc_smoke.h"
#include <Block.h>

@implementation SmokeObject
+ (id)alloc { return class_createInstance(self, 0); }
- (id)init { return self; }
- (id)retain { return self; }
- (oneway void)release { }
- (id)autorelease { return self; }
- (unsigned long)retainCount { return 1; }
@end

@implementation SmokeGreeter
@synthesize count;
- (const char *)greet { return "hello from an ObjC method"; }
- (const char *)origin { return "support-tu"; }
@end

int blockSum(int n)
{
	int (^adder)(int) = ^(int x) { return x + n; };	/* stack block */
	int (^heap)(int) = Block_copy(adder);		/* runtime: copy to the heap */
	int r = heap(10);
	Block_release(heap);
	return r;
}
