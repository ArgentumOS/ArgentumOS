/*
 * NSCFConstantString.m — the class of every COMPILE-TIME CF string, and the class `CFSTR` depends on.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * WHAT THIS CLASS IS. `CFSTR("x")`, compiled with -fconstant-cfstrings, is not a call: clang emits a
 * __CFConstantString STRUCT in the object file, and the isa field of that struct names this class. Until it
 * exists, ANY use of CFSTR in this tree is an undefined symbol — which is exactly what the object unit met,
 * as `$s10Foundation19_NSCFConstantStringCN`: upstream's class of that name is Swift's, and this tree had
 * none of its own.
 *
 * AND IT IS NOT REGISTERED WITH CF, WHICH IS THE ONE PLACE IT DIFFERS FROM THE CLASS BESIDE IT. Every other
 * class in this library registers itself for a CF type id, so CF's C path and the class's methods are two
 * ways to the same object. THIS ONE MUST NOT: a constant string is NOT a CF object — it is a plain struct
 * with a class for an isa — and everything that reads it does so because CF_IS_OBJC is TRUE for it, so CF
 * dispatches to -length instead of treating the struct as a raw CFString. Registering it would make that
 * comparison false and send CF down its C path over a struct that is not a CFString, which is the one thing
 * this class exists to prevent.
 *
 * THE LAYOUT IS THE CONTRACT AND IT IS CLANG'S. __CFConstantString is { isa; flags; ptr; length }: an isa,
 * a long of flags, a byte pointer and a long. The fields below are declared IN THAT ORDER, which is the
 * whole reason the class inherits from NSString rather than from nothing — NSString contributes exactly the
 * isa, so the subclass's fields land where clang's struct puts them. A root class here would put them
 * somewhere else and the compiler would have no way to tell either of us.
 */
#import <Foundation/NSString.h>
#include <objc/runtime.h>

/* THE FLAG THAT SAYS WHICH FORM THE BYTES ARE IN. clang has exactly two constant-string layouts and the
 * bit that separates them is in `flags` (Apple's two values differ in the 0x08 bit: 0x07C8 for C strings and
 * 0x07D0 for UTF-16). The probe PRINTS the flags it sees rather than trusting this note, because the note is
 * an expectation and the print is a measurement. */
#define FNX_CF_CONSTANT_IS_UTF16 0x08

@interface _NSCFConstantString : NSString
{
	/* THE OFFSETS COME FROM CFString.h's OWN CFSTR MACRO, not from Apple's documentation and not from
	 * memory — which is where the first version of this class got them wrong, with _flags reading the
	 * retain count. The macro builds:
	 *     struct __CFConstStr str = {{ (isa), (rc), 0x07C8 }, bytes, sizeof(cStr) - 1 };
	 * so after the isa the superclass contributes there are exactly four fields, in this order. */
	unsigned long _rc;		/* offset 8  */
	unsigned long _flags;		/* offset 16 — 0x07C8, and the 0x08 bit means UTF-16 */
	const unsigned char *_bytes;	/* offset 24 */
	unsigned long _count;		/* offset 32 */
}
- (unsigned long)length;
- (unsigned short)characterAtIndex:(unsigned long)index;
@end

@implementation _NSCFConstantString

- (unsigned long)length
{
	return (unsigned long)_count;
}

/* BOTH FORMS ARE ANSWERED, and the reason is that the alternative is a silent wrong answer rather than a
 * failure: a UTF-16 literal read as bytes would return half the characters and a NUL for every other one. */
- (unsigned short)characterAtIndex:(unsigned long)index
{
	if (index >= (unsigned long)_count) {
		return 0;
	}
	if ((_flags & FNX_CF_CONSTANT_IS_UTF16) != 0) {
		return ((const unsigned short *)_bytes)[index];
	}
	return (unsigned short)_bytes[index];
}

@end
