/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSPointerFunctions.m — W13a. See the header for why the class exists and for the Mach deviation.
 *
 * THE CALLOUTS ARE ORDINARY C FUNCTIONS IN THIS FILE, one short group per personality, and `-initWithOptions:`
 * is just a selection: the memory policy and the personality are read out of the options word and the matching
 * functions are installed. That is what makes the object inspectable — a caller can read the callouts back
 * and see exactly which decisions its options chose.
 *
 * THE `size` ARGUMENT IS PASSED THROUGH TO THE PERSONALITIES THAT NEED IT (a struct is hashed and compared
 * over its bytes, and only the caller's size function knows how many there are), and IGNORED by the ones that
 * do not. A personality that needs a size function but has none cannot do its job, so the struct personality
 * answers "no size" by hashing the ADDRESS instead of the bytes — see the note on that group below.
 *
 * MANUAL OWNERSHIP (MRC: the whole library is).
 */

#import <Foundation/NSPointerFunctions.h>
#import <Foundation/NSString.h>
#import <Foundation/NSNumber.h>
#import <Foundation/NSException.h>

#include <string.h>
#include <stdlib.h>

/* ---- the memory policies, as the pair of callouts they mean ---- */

static void *fn_acquire_strong(const void *item, NSUInteger (*size)(const void *item), BOOL shouldCopy)
{
	(void)size;
	if (shouldCopy) {
		return (void *)[(id)item copy];
	}
	return (void *)[(id)item retain];
}

static void fn_relinquish_strong(const void *item, NSUInteger (*size)(const void *item))
{
	(void)size;
	[(id)item release];
}

/* NOT OWNED, AND NOT ZEROED: the weak policy here is NOT a zeroing weak reference — Apple's own note says the
 * weak option leaves the stored pointer ALONE when the pointee dies, so a collection holding one has a
 * dangling pointer rather than a nil. Answering the same pointer for acquire and doing nothing for relinquish
 * is exactly that behaviour, and it is worth stating because "weak" elsewhere in this library DOES zero. */
static void *fn_acquire_none(const void *item, NSUInteger (*size)(const void *item), BOOL shouldCopy)
{
	(void)size;
	(void)shouldCopy;
	return (void *)item;
}

static void fn_relinquish_none(const void *item, NSUInteger (*size)(const void *item))
{
	(void)item;
	(void)size;
}

/* THE OWNED-AND-FREED POLICY, which is also what Mach's option is treated as (no Mach here — see the header):
 * the collection owns the bytes and gives them back with `free`. */
static void fn_relinquish_free(const void *item, NSUInteger (*size)(const void *item))
{
	(void)size;
	free((void *)item);
}

/* ---- the personalities ---- */

/* AN OBJECT, BY -hash/-isEqual:. */
static NSUInteger fn_hash_object(const void *item, NSUInteger (*size)(const void *item))
{
	(void)size;
	return [(id)item hash];
}

static BOOL fn_equal_object(const void *item1, const void *item2, NSUInteger (*size)(const void *item))
{
	(void)size;
	return [(id)item1 isEqual:(id)item2];
}

/* AN OBJECT, BY IDENTITY: the pointer is the answer, so two equal-but-distinct objects are two entries. */
static NSUInteger fn_hash_address(const void *item, NSUInteger (*size)(const void *item))
{
	(void)size;
	return (NSUInteger)(uintptr_t)item;
}

static BOOL fn_equal_address(const void *item1, const void *item2, NSUInteger (*size)(const void *item))
{
	(void)size;
	return item1 == item2;
}

static NSString *fn_describe_object(const void *item)
{
	return [(id)item description];
}

/* A C STRING: hashed and compared by CONTENT, so two different buffers holding the same text are one entry. */
static NSUInteger fn_hash_cstring(const void *item, NSUInteger (*size)(const void *item))
{
	const char *text = (const char *)item;
	NSUInteger hash = 5381;

	(void)size;
	while (*text != '\0') {
		hash = (hash * 33) ^ (NSUInteger)(unsigned char)*text++;
	}
	return hash;
}

static BOOL fn_equal_cstring(const void *item1, const void *item2, NSUInteger (*size)(const void *item))
{
	(void)size;
	return strcmp((const char *)item1, (const char *)item2) == 0;
}

static NSString *fn_describe_cstring(const void *item)
{
	return [NSString stringWithUTF8String:(const char *)item];
}

/*
 * A STRUCT: hashed and compared over its BYTES, which is why this is the one personality that needs a size
 * function. WITHOUT ONE THERE IS NOTHING TO READ, so this falls back to the ADDRESS — the honest answer, and
 * the same one the opaque personality gives: it says "I cannot look inside", rather than reading a length
 * nobody supplied.
 */
static NSUInteger fn_hash_bytes(const void *item, NSUInteger (*size)(const void *item))
{
	const unsigned char *bytes = (const unsigned char *)item;
	NSUInteger length = size != NULL ? size(item) : 0;
	NSUInteger hash = 5381;
	NSUInteger i;

	if (length == 0) {
		return (NSUInteger)(uintptr_t)item;
	}
	for (i = 0; i < length; i++) {
		hash = (hash * 33) ^ (NSUInteger)bytes[i];
	}
	return hash;
}

static BOOL fn_equal_bytes(const void *item1, const void *item2, NSUInteger (*size)(const void *item))
{
	NSUInteger length = size != NULL ? size(item1) : 0;

	if (length == 0) {
		return item1 == item2;
	}
	return memcmp(item1, item2, length) == 0;
}

/* AN INTEGER: the pointer IS the value, so identity and equality are the same question and there is nothing
 * to dereference. */
static NSUInteger fn_hash_integer(const void *item, NSUInteger (*size)(const void *item))
{
	(void)size;
	return (NSUInteger)(uintptr_t)item;
}

static NSString *fn_describe_integer(const void *item)
{
	return [[NSNumber numberWithUnsignedLong:(unsigned long)(uintptr_t)item] description];
}

@implementation NSPointerFunctions

- (instancetype)initWithOptions:(NSPointerFunctionsOptions)options
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_options = options;

	/* THE MEMORY POLICY, from the low byte — AND IT IS READ TOGETHER WITH THE PERSONALITY, because the two
	 * are not independent after all. "Strong" means the collection RETAINS THE POINTER AS AN OBJECT, and an
	 * integer or an opaque address is not one: this pairing was MEASURED as a crash before it was written
	 * down (an integer-personality array with strong memory sent `-retain` to the address 42). So the
	 * owning policies apply to the object personalities, and for every other personality the collection
	 * keeps the pointer WITHOUT owning it — which is the only meaning ownership could have there. */
	{
		NSUInteger personality = (options >> 8) & 0xFF;
		BOOL objectPersonality = (personality == 0 || personality == 2);
		NSUInteger memory = options & 0xFF;

		if (!objectPersonality) {
			_acquireFunction = fn_acquire_none;
			_relinquishFunction = (memory == 3 || memory == 4)
					    ? fn_relinquish_free : fn_relinquish_none;
		} else {
			switch (memory) {
			case 2:		/* OpaqueMemory */
			case 5:		/* WeakMemory — see fn_acquire_none */
				_acquireFunction = fn_acquire_none;
				_relinquishFunction = fn_relinquish_none;
				break;
			case 3:		/* MallocMemory */
			case 4:		/* MachVirtualMemory — treated as malloc: no Mach here */
				_acquireFunction = fn_acquire_none;
				_relinquishFunction = fn_relinquish_free;
				break;
			default:	/* StrongMemory */
				_acquireFunction = fn_acquire_strong;
				_relinquishFunction = fn_relinquish_strong;
				break;
			}
		}
	}

	/* THE PERSONALITY, from the second byte. */
	switch ((options >> 8) & 0xFF) {
	case 1:		/* OpaquePersonality */
		_hashFunction = fn_hash_address;
		_isEqualFunction = fn_equal_address;
		_descriptionFunction = NULL;
		break;
	case 2:		/* ObjectPointerPersonality */
		_hashFunction = fn_hash_address;
		_isEqualFunction = fn_equal_address;
		_descriptionFunction = fn_describe_object;
		break;
	case 3:		/* CStringPersonality */
		_hashFunction = fn_hash_cstring;
		_isEqualFunction = fn_equal_cstring;
		_descriptionFunction = fn_describe_cstring;
		break;
	case 4:		/* StructPersonality */
		_hashFunction = fn_hash_bytes;
		_isEqualFunction = fn_equal_bytes;
		_descriptionFunction = NULL;
		break;
	case 5:		/* IntegerPersonality */
		_hashFunction = fn_hash_integer;
		_isEqualFunction = fn_equal_address;
		_descriptionFunction = fn_describe_integer;
		break;
	default:	/* ObjectPersonality */
		_hashFunction = fn_hash_object;
		_isEqualFunction = fn_equal_object;
		_descriptionFunction = fn_describe_object;
		break;
	}
	/* THE COPY FLAG IS A MEMORY DECISION, NOT A PERSONALITY ONE, and it is read by the acquire callout. */
	_shouldCopy = (options & NSPointerFunctionsCopyIn) != 0;
	_sizeFunction = NULL;
	return self;
}

- (void)dealloc
{
	[super dealloc];
}

/* THE LIBRARY'S COPYING PROTOCOL IS `-copy`, NOT COCOA'S `-copyWithZone:` — NSObject.h records the
 * deviation (no NSZone on a 64-bit-only system), and this is a class that has to conform to IT. */
- (id)copy
{
	NSPointerFunctions *copy = [[[self class] alloc] initWithOptions:_options];

	/* A REPLACED CALLOUT SURVIVES THE COPY: the options word alone cannot reconstruct a function somebody
	 * installed by hand, so the copy carries every callout over rather than re-deriving them. */
	copy->_hashFunction = _hashFunction;
	copy->_isEqualFunction = _isEqualFunction;
	copy->_sizeFunction = _sizeFunction;
	copy->_descriptionFunction = _descriptionFunction;
	copy->_acquireFunction = _acquireFunction;
	copy->_relinquishFunction = _relinquishFunction;
	copy->_shouldCopy = _shouldCopy;
	copy->_usesStrongWriteBarrier = _usesStrongWriteBarrier;
	copy->_usesWeakReadAndWriteBarriers = _usesWeakReadAndWriteBarriers;
	return copy;
}

/* THE ACQUIRE CALLOUT IS WRAPPED SO THAT CopyIn IS APPLIED IN ONE PLACE rather than inside each policy: the
 * policies above say WHO OWNS the pointer, and this says whether it is copied first. */
- (nullable NSPointerFunctionsAcquireFunction)acquireFunction
{
	return _acquireFunction;
}

- (void)setAcquireFunction:(nullable NSPointerFunctionsAcquireFunction)function
{
	_acquireFunction = function;
}

- (nullable NSPointerFunctionsRelinquishFunction)relinquishFunction { return _relinquishFunction; }
- (void)setRelinquishFunction:(nullable NSPointerFunctionsRelinquishFunction)function
{
	_relinquishFunction = function;
}

- (nullable NSPointerFunctionsHashFunction)hashFunction { return _hashFunction; }
- (void)setHashFunction:(nullable NSPointerFunctionsHashFunction)function { _hashFunction = function; }

- (nullable NSPointerFunctionsIsEqualFunction)isEqualFunction { return _isEqualFunction; }
- (void)setIsEqualFunction:(nullable NSPointerFunctionsIsEqualFunction)function
{
	_isEqualFunction = function;
}

- (nullable NSPointerFunctionsSizeFunction)sizeFunction { return _sizeFunction; }
- (void)setSizeFunction:(nullable NSPointerFunctionsSizeFunction)function { _sizeFunction = function; }

- (nullable NSPointerFunctionsDescriptionFunction)descriptionFunction { return _descriptionFunction; }
- (void)setDescriptionFunction:(nullable NSPointerFunctionsDescriptionFunction)function
{
	_descriptionFunction = function;
}

- (BOOL)usesStrongWriteBarrier { return _usesStrongWriteBarrier; }
- (void)setUsesStrongWriteBarrier:(BOOL)flag { _usesStrongWriteBarrier = flag; }

- (BOOL)usesWeakReadAndWriteBarriers { return _usesWeakReadAndWriteBarriers; }
- (void)setUsesWeakReadAndWriteBarriers:(BOOL)flag { _usesWeakReadAndWriteBarriers = flag; }

/* THE COLLECTIONS' DOORS, not Apple's public API but the same object's job: they apply the callouts and the
 * copy flag together so that no collection has to know which personality it holds. */
- (void *)fnAcquire:(const void *)item
{
	void *answer;

	if (_acquireFunction == NULL) {
		return (void *)item;
	}
	answer = _acquireFunction(item, _sizeFunction, _shouldCopy);
	return answer;
}

- (void)fnRelinquish:(const void *)item
{
	if (_relinquishFunction != NULL) {
		_relinquishFunction(item, _sizeFunction);
	}
}

- (NSUInteger)fnHash:(const void *)item
{
	return _hashFunction != NULL ? _hashFunction(item, _sizeFunction) : (NSUInteger)(uintptr_t)item;
}

- (BOOL)fnIsEqual:(const void *)item1 to:(const void *)item2
{
	return _isEqualFunction != NULL
	     ? _isEqualFunction(item1, item2, _sizeFunction) : item1 == item2;
}

- (BOOL)fnShouldCopy { return _shouldCopy; }

- (NSPointerFunctionsOptions)fnOptions { return _options; }

@end
