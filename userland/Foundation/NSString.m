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
 * calls _CFStringGetLength2 and -characterAtIndex: calls _CFStringCheckAndGetCharacterAtIndex — CF's real
 * work, no dispatch, no assertion check — rather than reimplementing anything, and rather than calling
 * CFStringGetLength/CFStringGetCharacterAtIndex, which would dispatch straight back into here.
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

/* THE DECLARATIONS THIS FILE NEEDS, both ours, both with the same provenance: the bridging door is this
 * tree's addition to the CF package (modification 9), and the twins are upstream's, declared in CF's INTERNAL
 * headers because upstream expects its own Foundation to be the caller. */
extern unsigned long CFNXBridgeClassToType(Class cls, CFTypeID typeID);
extern CFIndex _CFStringGetLength2(CFStringRef str);
/* THE CHARACTER TWIN, and CFString.c's own comment is what makes it the right one rather than the obvious
 * one: "This one is for NSCFString usage; it doesn't do ObjC dispatch; but it does do range check". The
 * range check is the whole reason to use it instead of the guts function it wraps. */
extern int _CFStringCheckAndGetCharacterAtIndex(CFStringRef str, CFIndex idx, UniChar *ch);
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

/* AND THE INSTANCE TWIN, WHOSE ABSENCE WAS A PROTOCOL GAP RATHER THAN A HARMLESS OMISSION. The NSObject
 * protocol declares `- (Class)class` as well as `+ (Class)class`, and this class answered only the class
 * method -- which is why the build carried the warning "method 'class' in protocol 'NSObject' not
 * implemented". A SUBCLASS inherits both and needs no declaration; a ROOT class must answer both, because it
 * inherits nothing that could answer on its behalf. */
- (Class)class
{
	return object_getClass(self);
}

/* NOT static, AND REACHED FROM TWO PLACES: the library constructor below, and CF's own hook when it finishes
 * initializing. The flag makes both orders safe. Re-entrancy here is the normal case rather than an
 * edge: the CF calls below start CF's initialization, CF calls the hook, and the hook arrives back here while
 * this call is still running -- which must do nothing and let THIS call finish, which it does, because the
 * state only reaches 2 after every class has been registered.
 *
 * WHY IT IS CALLED FROM TWO PLACES, AND WHY THAT IS THE WHOLE BUG THIS CLASS HAD. CF initializes on its
 * FIRST CALL, and for the first object of a bridged class that first call is CFArrayCreate INSIDE the
 * initialiser -- i.e. AFTER +alloc has already asked what type its class is and been told 0. Measured: the
 * header word of the first NSArray was zero the instant it existed, while later arrays carried 0x1300. So
 * registration must complete BEFORE any allocation, which only a load-time constructor can guarantee; CF's
 * hook is the fallback for the one order a constructor cannot control -- a class its runtime had not realised
 * yet when the constructor ran.
 *
 * A CONSTRUCTOR WAS TRIED ONCE BEFORE AND MEASURED TOO EARLY -- [NSString class] was Nil at load, because this
 * root class had no +class then -- and the failure was SILENT in the worst way: the old code advanced to the
 * done state anyway, so the only later caller skipped a registration that had registered nothing. Both halves
 * of that are fixed here: the class IS resolved before the state is claimed (a class the runtime has not
 * realised yet leaves the state at 0 so the next caller retries), and +class exists on both root classes.
 */
void _FNXRegisterAllBridgedClasses(void)
{
	static int state = 0;	/* 0 = not started, 1 = running, 2 = done */

	if (state != 0) {
		return;
	}

	/* RESOLVE BEFORE CLAIMING TO HAVE RUN. objc_getClass is asked rather than the class messaged, because a
	 * class the runtime has not realised answers nil to both, and nil must NOT look like "registered". */
	{
		Class str = objc_getClass("NSString");
		Class arr = objc_getClass("NSArray");

		if (str == Nil || arr == Nil) {
			return;
		}
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
	/* CF CALLS THIS BY NAME, WEAKLY, FROM INSIDE OBJECT CREATION, and it is the fallback rather than the
	 * primary route now that the constructor below registers at load. It still matters for the orders a
	 * constructor cannot control: a class the runtime had not realised when the constructor ran leaves the
	 * state at 0, and this is what then completes the registration -- now with the classes realised, because
	 * CF is inside object creation and the image is fully loaded. */
	_FNXRegisterAllBridgedClasses();
}

/*
 * THE ORDERING FIX, AND THE ONLY REASON THIS IS A CONSTRUCTOR. CF's runtime registers our classes only when
 * it FINISHES INITIALIZING, and CF initializes on its FIRST CALL -- which, for the first object of a bridged
 * class, is a CF call made from inside that object's initialiser, i.e. AFTER +alloc has already asked its
 * class's type ID and been told 0. A constructor runs at image load, before any allocation exists, so the
 * registration is complete by the time the first +alloc asks. Measured before this was added: the first
 * NSArray's type word was 0x0 while later arrays carried 0x1300.
 *
 * IT IS SAFE TO RUN TOO EARLY: _FNXRegisterAllBridgedClasses resolves its classes first and returns WITHOUT
 * marking itself done if the runtime has not realised them yet, so an early constructor costs a retry and
 * never a silently-skipped registration.
 */
__attribute__((constructor)) static void _FNXRegisterBridgedClassesAtLoad(void)
{
	_FNXRegisterAllBridgedClasses();
}

/* WHAT COUNT IS: UNITS, which is what CFStringGetLength means and what Apple's -length means, so the two
 * worlds cannot disagree about how long this string is. */
- (unsigned long)length
{
	return (unsigned long)_CFStringGetLength2((CFStringRef)self);
}

/* THE OTHER HALF OF THAT PAIR, AND IT IS CF'S OWN FUNCTION RATHER THAN A REIMPLEMENTATION. CF keeps this
 * twin beside the length one for exactly this class, and its own comment says what it adds: no ObjC dispatch,
 * but a range check. The range check is the point -- the guts function it wraps would read past a CFString's
 * storage, while this answers CF's bounds error instead. (The sentinel is CF's enum, whose success value is
 * _CFStringErrNone == 0, so "non-zero" is the failure test and no constant of ours is being invented.)
 *
 * OUT OF RANGE ANSWERS 0, WHICH IS THIS LIBRARY'S ANSWER AND NOT APPLE'S, AND THE DIFFERENCE IS NAMED RATHER
 * THAN HIDDEN. Apple raises NSRangeException; this library has no NSException class to raise (it is these
 * four classes and no more). NSConstantString, the class beside this one, answers 0 for the same reason --
 * so the two agree, which is the property worth having until there is an exception to raise with.
 *
 * AND ONE LIVE BUG DIES WITH IT: -isEqual: ABOVE CALLS THIS DOOR ON BOTH OPERANDS, so before this method
 * existed, comparing a CF-native string raised doesNotRecognizeSelector instead of comparing. The warning
 * that used to sit here said the twin's name had been GUESSED once and cost a link error; the name is now
 * read out of CFString.c's own comment pair rather than guessed, which is why the guess is not repeated. */
- (unsigned short)characterAtIndex:(unsigned long)index
{
	UniChar ch = 0;

	if (_CFStringCheckAndGetCharacterAtIndex((CFStringRef)self, (CFIndex)index, &ch) != 0) {
		return 0;
	}
	return (unsigned short)ch;
}

@end

/*
 * THE ALIAS FOR CFConstantStringClassReference IS NOT HERE, AND THAT IS MEASURED RATHER THAN TIDY. An
 * assembler .set resolves only inside its own assembly unit, so a block in THIS file aliasing
 * ._OBJC_CLASS_NSConstantString -- a symbol this translation unit does not define -- emits NOTHING, with or
 * without a C-level reference to it. The working block lives in NSCFConstantString.m, the file that owns the
 * class. Putting it back here would silently restore the bug this file's history already paid for twice.
 */
