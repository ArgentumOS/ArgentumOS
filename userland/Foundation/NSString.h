/*
 * NSString.h — the interface of the class that stands for CFString.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * WHY THIS HEADER EXISTS AT ALL, since this library held its interfaces inside its .m files until now: a
 * SUBCLASS needs one. `_NSCFConstantString` is a subclass of NSString (it must be, so that clang's
 * CF_BRIDGED_TYPE(NSString) toll-free check accepts it), and a subclass cannot be declared against an
 * interface that lives in someone else's implementation file. This is the first header the library owns.
 *
 * THE ROOT-CLASS ATTRIBUTE IS ON THE INTERFACE, here where it belongs: NSString inherits from nothing, and
 * libobjc2 has to be told rather than handed a superclass that does not exist in this library. Its ONLY
 * field is the isa, because a CFString's header IS the object — the storage belongs to CoreFoundation and
 * every door reads it through CF's own code.
 */
#ifndef FNX_FOUNDATION_NSSTRING_H
#define FNX_FOUNDATION_NSSTRING_H

#import <Foundation/NSObject.h>

/* THE DOORS THE CLASS ANSWERS TODAY. It is short on purpose: this is the CFString face, and each door added
 * here is one more place the two worlds must agree. -length returns UNITS, which is what CFStringGetLength
 * and NSString's own contract both mean. */
__attribute__((objc_root_class))
@interface NSString <NSObject>
{
	Class isa;		/* the CF object's own first word: CF's header IS the object here */
}
- (unsigned long)length;

/* THE PROTOCOL'S DOORS, WHICH THIS CLASS MUST ANSWER ITSELF BECAUSE IT INHERITS NOTHING. Each one delegates
 * to a NON-DISPATCHING twin or to CF's own C entry point, never to the public door that would call back:
 * -isEqual: via CFEqual(self, ...) would be the same self-delegation loop that once cost this library an
 * objc_retain recursion. */
- (BOOL)isEqual:(id)other;
- (NSUInteger)hash;
+ (Class)class;
- (BOOL)isKindOfClass:(Class)cls;
- (unsigned short)characterAtIndex:(unsigned long)index;
- (NSString *)description;
- (id)retain;
- (void)release;

/* +class IS APPLE'S DOOR AND ITS ABSENCE COST THIS PROJECT A SEARCH. A class that inherits from NSObject
 * answers +class through NSObject; a ROOT class does not, so `[NSString class]` sent a message nothing
 * implemented — and libobjc2 returned Nil rather than raising, which the registration read as "no class" and
 * skipped, silently, because a door that refuses a Nil class cannot refuse it loudly. `objc_getClass("...")`
 * was the way round it; this is the way through. */
+ (Class)class;
@end

#endif /* FNX_FOUNDATION_NSSTRING_H */
