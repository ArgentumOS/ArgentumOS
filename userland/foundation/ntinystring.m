/*
 * ntinystring.m — the tagged-string class. See NSTinyString.h for why it exists.
 */

#import <foundation/NSTinyString.h>
#include <stdint.h>

/* The encoding, from clang's own codegen: 7 bits per character from bit 57
 * down, a 4-bit length in bits 3-6, and the tag 4 in bits 0-2. */
#define TINY_TAG		4u
#define TINY_LENGTH_MASK	0xFu
#define TINY_LENGTH_SHIFT	3
#define TINY_CHAR_MASK		0x7Fu
#define TINY_FIRST_SHIFT	57
#define TINY_CHAR_STRIDE	7
#define TINY_MAX_LENGTH		8u

static size_t tiny_length(id self)
{
	return (size_t)((((uintptr_t)self) >> TINY_LENGTH_SHIFT) & TINY_LENGTH_MASK);
}

static char tiny_character(id self, size_t index)
{
	return (char)((((uintptr_t)self) >> (TINY_FIRST_SHIFT - index * TINY_CHAR_STRIDE))
		      & TINY_CHAR_MASK);
}

@implementation NSTinyString

+ (void)load
{
	/*
	 * The registration that makes `@"short"` work at all. 64-bit only: on a
	 * 32-bit target there is a single small-object class and no tiny strings.
	 */
	if (sizeof(void *) == 8) {
		objc_registerSmallObjectClass_np(self, TINY_TAG);
	}
}

/* A tagged pointer owns no memory and carries no reference count. */
- (id)retain { return self; }
- (oneway void)release { }
- (id)autorelease { return self; }
- (void)dealloc { }

- (Class)class { return [NSTinyString class]; }

- (size_t)length { return tiny_length(self); }

/* 7-bit ASCII, so one byte is one character. */
- (size_t)characterCount { return tiny_length(self); }

- (unsigned short)characterAtIndex:(size_t)index
{
	if (index >= tiny_length(self)) {
		return 0;
	}
	return (unsigned short)(unsigned char)tiny_character(self, index);
}

- (const char *)UTF8String
{
	/*
	 * THE ONE AWKWARD THING ABOUT A TAGGED STRING: its characters live in the
	 * pointer, so a C string has to be MATERIALISED. This returns a static
	 * buffer, valid until the next -UTF8String on any tiny string — documented,
	 * rather than hidden. Nothing else here needs one: -length,
	 * -characterAtIndex: and the value methods all decode the bits directly.
	 */
	static char buffer[TINY_MAX_LENGTH + 1];
	size_t n = tiny_length(self);
	size_t i;

	for (i = 0; i < n; i++) {
		buffer[i] = tiny_character(self, i);
	}
	buffer[n] = '\0';
	return buffer;
}

@end
