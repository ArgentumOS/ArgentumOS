/*
 * NSString.m — the Objective-C face of CFString, and the first half of free casting.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * WHY THIS CLASS EXISTS, AND WHY IT HAS NO IVARS. A string CoreFoundation creates with its C API
 * (CFStringCreateWithCString and friends) is a CF object: a CF header, then CFString's own fields. With the
 * class table empty, as this tree shipped, that object's first word was 0 — so a CFStringRef could be handed
 * to an NS API only in the direction that dispatches, and `(NSString *)cfStr` had nowhere to go, because a
 * pointer with no class cannot be messaged.
 *
 * This class is the missing side: registered for CFStringGetTypeID(), it becomes the isa of every CF string
 * created afterwards, so such a string IS a messageable object. It declares NO IVARS ON PURPOSE — the
 * storage belongs to CFString, and every method reads it through CF's own code. A class that added fields
 * would be describing a DIFFERENT object from the one CF allocated.
 *
 * AND ITS METHODS CALL THE TWINS, WHICH IS THE DELEGATION RULE IN ITS SHARPEST FORM. CF's public doors
 * dispatch to us (CF_OBJC_FUNCDISPATCHV(..., NSString, length)) and fall back to the C implementation;
 * the non-dispatching twins exist beside them labelled "for NSCFString", which is THIS class. So -length
 * calls _CFStringGetLength2 — CF's real work, no dispatch, no assertion check — rather than reimplementing
 * anything, and rather than calling CFStringGetLength, which would dispatch straight back into here.
 *
 * AND ITS NAME IS NOT A CHOICE. CFBase.h has carried the answer all along:
 *
 *     typedef const struct CF_BRIDGED_TYPE(NSString) __CFString * CFStringRef;
 *
 * Upstream's own public header DECLARES that a CFStringRef and an NSString are the same object, and clang
 * enforces it: a class named anything else is refused at the cast, with "bridged to CFStringRef, which is
 * not valid CF object". This class was written as NSString first and the compiler said so.
 */
#include <objc/runtime.h>
#include <CoreFoundation/CoreFoundation.h>

/* THE TWO DECLARATIONS THIS FILE NEEDS, both ours, both with the same provenance: the bridging door is
 * this tree's addition to the CF package (modification 9), and the twin is upstream's, declared in CF's
 * INTERNAL headers because upstream expects its own Foundation to be the caller. */
extern void CFNXBridgeClassToType(Class cls, CFTypeID typeID);
extern CFIndex _CFStringGetLength2(CFStringRef str);

__attribute__((objc_root_class))
@interface NSString
{
	Class isa;		/* the CF object's own first word: CF's header IS the object here */
}
- (unsigned long)length;
@end

@implementation NSString

/* +load SO THE REGISTRATION PRECEDES ANY STRING. It must happen before the first CF string is created,
 * because it is what such an object's isa is set FROM — a registration after the fact would leave the
 * strings made before it unmessageable. */
+ (void)load
{
	CFNXBridgeClassToType([NSString class], CFStringGetTypeID());
}

- (unsigned long)length
{
	return (unsigned long)_CFStringGetLength2((CFStringRef)self);
}

/* -characterAtIndex: BELONGS HERE TOO, and it is deliberately not written yet: its twin's name was GUESSED
 * (the length twin's was checked, and this one's guess cost a link error), and a guess is not worth a
 * method. The twin exists -- CFString.c has the pair -- and finding its name is the first thing the next
 * door added here needs. -length alone is what proves the cast works.
 */

@end
