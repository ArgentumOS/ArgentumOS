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
/* THE HASH TWIN, named FOR THE CLASS IT SERVES -- the same convention as the length twin above. CF keeps it
 * internal because upstream expects its own Foundation to be the caller. */
extern CFHashCode CFStringHashNSString(CFStringRef str);

#import <Foundation/NSString.h>

@implementation NSString

/* THE PROTOCOL'S DOORS, ANSWERED HERE BECAUSE THIS CLASS INHERITS NOTHING. Each delegates to a
 * NON-DISPATCHING entry point, never to the public door that would call back into the method:
 *   -retain/-release -> CFRetain/CFRelease, which is SAFE and does not loop because this class is REGISTERED
 *      for CFStringGetTypeID(): CF_IS_OBJC is FALSE for it, so CF's own C path runs rather than the arm
 *      returning here. One count -- CF's -- reached from both sides.
 *   -hash -> CFStringHashNSString, the twin upstream names for exactly this class.
 *   -isEqual: -> this class's OWN -length and -characterAtIndex:, deliberately: comparing through CFEqual
 *      would dispatch on the first argument and call this very method again. */
- (id)retain
{
	return (id)CFRetain((CFTypeRef)self);
}

- (void)release
{
	CFRelease((CFTypeRef)self);
}

- (NSUInteger)hash
{
	return (NSUInteger)CFStringHashNSString((CFStringRef)self);
}

- (BOOL)isKindOfClass:(Class)cls
{
	return FNXClassIsKindOfClass(object_getClass(self), cls);
}

- (BOOL)isEqual:(id)other
{
	unsigned long n;
	unsigned long i;

	if (other == self) {
		return YES;
	}
	if (other == nil || ![(id)other isKindOfClass:[NSString class]]) {
		return NO;
	}
	n = (unsigned long)[self length];
	if ((unsigned long)[(NSString *)other length] != n) {
		return NO;
	}
	for (i = 0; i < n; i++) {
		if ([self characterAtIndex:i] != [(NSString *)other characterAtIndex:i]) {
			return NO;
		}
	}
	return YES;
}

/* A STRING'S DESCRIPTION IS ITSELF, which is Apple's contract for this door and is also the only answer that
 * cannot disagree with anything: it is the same object. */
- (NSString *)description
{
	return (NSString *)CFRetain((CFTypeRef)self);
}

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
/*
 * THE REGISTRATION, AND WHY IT IS CALLED FROM TWO PLACES. CF calls _CFNXBridgeAllClasses when IT finishes
 * initializing, but CF only initializes on its FIRST CALL -- and for the first object of a bridged class that
 * first call is CFArrayCreate INSIDE the initialiser, i.e. AFTER +alloc has already asked what type its class
 * is and been told 0. That is measured: the header word of the first NSArray was zero the instant it existed,
 * while later arrays carried 0x1300 correctly.
 *
 * So +alloc may pull this forward, and the flag makes BOTH orders safe. Re-entrancy here is the normal case
 * rather than an edge: the CF call below starts CF's initialization, CF calls the hook, and the hook arrives
 * back here while this call is still running -- which must do nothing and let THIS call finish, which it does,
 * because the state only reaches 2 after every class has been registered.
 */
void _FNXRegisterAllBridgedClasses(void)
{
	static int state = 0;	/* 0 = not started, 1 = running, 2 = done */

	if (state != 0) {
		return;
	}
	state = 1;

	{
		extern void _FNXBridgeClass(Class cls, unsigned long typeID);
		extern void _CFNXBridgeArrayClasses(void);

		_FNXBridgeClass([NSString class], (unsigned long)CFStringGetTypeID());
		_CFNXBridgeArrayClasses();
	}

	/* AND THE CONSTANT STRINGS, WHICH CF'S OWN CODE HAS BEEN WAITING FOR -- restored here because a regex
	 * edit removed it from the hook, which is exactly the kind of silent loss this file has suffered before.
	 * Why the pointer matters: CF already compares an object's isa against it (CFRuntime.c:1956), and the
	 * symbol it compares is the one clang puts in every CFSTR struct as its isa. The alias that makes that
	 * symbol BE the class lives in NSCFConstantString.m; this is the other half. */
	{
		extern void *__CFConstantStringClassReferencePtr;

		__CFConstantStringClassReferencePtr = objc_getClass("NSConstantString");
	}
	state = 2;
}

void _CFNXBridgeAllClasses(void)
{
	/* THE CLASS IS LOOKED UP BY NAME, NOT MESSAGED, AND THE REASON IS MEASURED. `[NSString class]` returned
	 * NIL here -- twice: once from a constructor and once from this hook -- while objc_getClass("NSString")
	 * answers in the same process. The compiler had said why in a warning worth reading: "class method
	 * '+class' not found". THIS CLASS IS A ROOT CLASS AND DOES NOT IMPLEMENT +class, so the message went
	 * nowhere and the registration registered nothing, silently, because the door's own guard skips a Nil
	 * class. Two characters of instrument ('Hnz') said all of that after a great deal of reasoning had not. */
	_FNXRegisterAllBridgedClasses();
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

/*
 * THE ALIAS FOR CFConstantStringClassReference IS NOT HERE, AND THAT IS MEASURED RATHER THAN TIDY. An
 * assembler .set resolves only inside its own assembly unit, so a block in THIS file aliasing
 * ._OBJC_CLASS_NSConstantString -- a symbol this translation unit does not define -- emits NOTHING, with or
 * without a C-level reference to it. The working block lives in NSCFConstantString.m, the file that owns the
 * class. Putting it back here would silently restore the bug this file's history already paid for twice.
 */
