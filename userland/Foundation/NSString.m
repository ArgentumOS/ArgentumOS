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
extern unsigned long CFNXBridgeClassToType(Class cls, CFTypeID typeID);
extern CFIndex _CFStringGetLength2(CFStringRef str);

#import <Foundation/NSString.h>

@implementation NSString

/* THE ROOT CLASS ANSWERS +class ITSELF. A class inheriting from NSObject gets this for free; a root class
 * does not, and the failure is quiet: `[NSString class]` returned NIL, the registration read that as "no
 * class" and skipped, and the only visible symptom was a string whose first word was zero. */
+ (Class)class
{
	return self;
}

/* NOT static, AND NOT A CONSTRUCTOR: CF calls this BY NAME, weakly, from inside object creation and only
 * once its runtime is initialised. A constructor was tried first and MEASURED TOO EARLY -- at that point
 * [NSString class] is Nil and the registration silently did nothing. This is called at the moment CF needs
 * the answer, which is the only moment guaranteed to be late enough and early enough at once. */
void _CFNXBridgeAllClasses(void)
{
	/* THE CLASS IS LOOKED UP BY NAME, NOT MESSAGED, AND THE REASON IS MEASURED. `[NSString class]` returned
	 * NIL here -- twice: once from a constructor and once from this hook -- while objc_getClass("NSString")
	 * answers in the same process. The compiler had said why in a warning worth reading: "class method
	 * '+class' not found". THIS CLASS IS A ROOT CLASS AND DOES NOT IMPLEMENT +class, so the message went
	 * nowhere and the registration registered nothing, silently, because the door's own guard skips a Nil
	 * class. Two characters of instrument ('Hnz') said all of that after a great deal of reasoning had not. */
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
