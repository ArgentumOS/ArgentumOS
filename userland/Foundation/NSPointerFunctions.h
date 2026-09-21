/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSPointerFunctions — the CALLOUTS a pointer collection uses, gathered into one object.
 * docs/design/foundation-plan.md §12.3 W13a; it is the core of NSPointerArray, NSHashTable and NSMapTable.
 *
 * WHY THIS CLASS EXISTS AT ALL is the thing to understand before the API: the three pointer collections hold
 * RAW POINTERS, and a raw pointer has no `-hash`, no `-isEqual:`, no ownership and no `-description`. So EVERY
 * decision a collection normally gets from the object itself — how to hash it, how to compare it, what to do
 * with a copy that arrives, how to describe it, whether to retain it and how to give it back — has to be
 * supplied from OUTSIDE. This object is that supply: an options word selects a PERSONALITY (how the pointer is
 * interpreted) and a MEMORY POLICY (who owns it), and the seven callouts are the result.
 *
 * THE TWO PARTS OF THE OPTIONS WORD ARE INDEPENDENT, which is why they are two families of bits:
 *
 *   * the MEMORY options say who owns the pointer — strong (the collection retains and releases it), weak
 *     (it is not owned and is NOT zeroed), opaque (it is not owned at all), malloc (the collection frees it
 *     with `free`), or Mach virtual memory;
 *   * the PERSONALITY says what the pointer MEANS — an object, an object's identity regardless of equality, a
 *     C string, a struct, an integer, or nothing at all.
 *
 * THE CALLBACKS ARE TYPED, NOT `void *`: `NSPointerFunctionsOpaquePersonality` implies the pointer is just an
 * address, but the callouts are declared with a `NSUInteger`-returning hash and a `BOOL`-returning equality,
 * because that is the shape the collections call them with.
 *
 * ONE DEVIATION, NAMED AND NECESSARY: `NSPointerFunctionsMachVirtualMemory` exists in Apple's options because
 * a collection may own memory obtained from Mach's `vm_allocate`. THIS SYSTEM HAS NO MACH, so the option is
 * declared (a caller's bit pattern must be spellable) and treated as `NSPointerFunctionsMallocMemory`, which
 * is the closest real policy: the collection owns the memory and gives it back with `free`. That is a §11
 * deviation on the "this system lacks the dependency" ground, and it is registered.
 */

#ifndef FOUNDATION_NSPOINTERFUNCTIONS_H
#define FOUNDATION_NSPOINTERFUNCTIONS_H

#import <Foundation/NSObject.h>

NS_ASSUME_NONNULL_BEGIN

/*
 * THE OPTIONS, as two families of bits. THE VALUES ARE THIS LIBRARY'S: Apple publishes the option NAMES and
 * the fact that they are a mask, and does not publish the numbers, so — exactly as with the Foundation enums
 * before them (§11's D2 rule) — the bit layout is ours. It is laid out the way the names read: the memory
 * policy in the low byte, the personality in the next, and the single behavioural flag above both.
 */
typedef NSUInteger NSPointerFunctionsOptions;

/* MEMORY: who owns the pointer. */
#define NSPointerFunctionsStrongMemory		((NSUInteger)0 << 0)	/* the collection retains/releases */
#define NSPointerFunctionsOpaqueMemory		((NSUInteger)2 << 0)	/* it owns nothing */
#define NSPointerFunctionsMallocMemory		((NSUInteger)3 << 0)	/* it owns it, and frees() it */
#define NSPointerFunctionsMachVirtualMemory	((NSUInteger)4 << 0)	/* Mach's memory — see the header */
#define NSPointerFunctionsWeakMemory		((NSUInteger)5 << 0)	/* not owned, and NOT zeroed */

/* PERSONALITY: what the pointer means. */
#define NSPointerFunctionsObjectPersonality		((NSUInteger)0 << 8)	/* an object, by -isEqual: */
#define NSPointerFunctionsOpaquePersonality		((NSUInteger)1 << 8)	/* an address, by identity */
#define NSPointerFunctionsObjectPointerPersonality	((NSUInteger)2 << 8)	/* an object, by identity */
#define NSPointerFunctionsCStringPersonality		((NSUInteger)3 << 8)	/* a NUL-terminated string */
#define NSPointerFunctionsStructPersonality		((NSUInteger)4 << 8)	/* a struct, by bytes */
#define NSPointerFunctionsIntegerPersonality		((NSUInteger)5 << 8)	/* the pointer IS the value */

/* BEHAVIOUR: on insertion, copy the pointee rather than keeping the pointer. */
#define NSPointerFunctionsCopyIn			((NSUInteger)1 << 16)

/* The callouts' shapes. They are declared as typedefs because a caller who supplies its own function pointer
 * needs the exact type, and because the properties have to name it. */
typedef NSUInteger (*NSPointerFunctionsHashFunction)(const void *item, NSUInteger (*size)(const void *item));
typedef BOOL (*NSPointerFunctionsIsEqualFunction)(const void *item1, const void *item2,
						  NSUInteger (*size)(const void *item));
typedef NSString * _Nullable (*NSPointerFunctionsDescriptionFunction)(const void *item);
typedef void * _Nullable (*NSPointerFunctionsAcquireFunction)(const void *item,
							      NSUInteger (*size)(const void *item),
							      BOOL shouldCopy);
typedef void (*NSPointerFunctionsRelinquishFunction)(const void *item,
						     NSUInteger (*size)(const void *item));
typedef NSUInteger (*NSPointerFunctionsSizeFunction)(const void *item);

@interface NSPointerFunctions : NSObject <NSCopying>
{
	NSPointerFunctionsOptions _options;
	NSPointerFunctionsHashFunction _hashFunction;
	NSPointerFunctionsIsEqualFunction _isEqualFunction;
	NSPointerFunctionsSizeFunction _sizeFunction;
	NSPointerFunctionsDescriptionFunction _descriptionFunction;
	NSPointerFunctionsAcquireFunction _acquireFunction;
	NSPointerFunctionsRelinquishFunction _relinquishFunction;
	BOOL _shouldCopy;			/* the CopyIn flag, split out because it is read on every insert */
	BOOL _usesStrongWriteBarrier;
	BOOL _usesWeakReadAndWriteBarriers;
}

/* THE OPTIONS WORD IS THE WHOLE CONFIGURATION. The callouts it selects are readable below and may be
 * replaced one at a time, which is how a caller keeps Apple's personality but changes one decision. */
- (instancetype)initWithOptions:(NSPointerFunctionsOptions)options;

/* PERSONALITY: how the pointer is hashed, compared, measured and described. */
- (nullable NSPointerFunctionsHashFunction)hashFunction;
- (void)setHashFunction:(nullable NSPointerFunctionsHashFunction)function;
- (nullable NSPointerFunctionsIsEqualFunction)isEqualFunction;
- (void)setIsEqualFunction:(nullable NSPointerFunctionsIsEqualFunction)function;
- (nullable NSPointerFunctionsSizeFunction)sizeFunction;
- (void)setSizeFunction:(nullable NSPointerFunctionsSizeFunction)function;
- (nullable NSPointerFunctionsDescriptionFunction)descriptionFunction;
- (void)setDescriptionFunction:(nullable NSPointerFunctionsDescriptionFunction)function;

/* MEMORY: how the pointer is taken and given back. `acquire` answers the pointer the collection will hold
 * (which is a COPY when the options ask for one), and `relinquish` is handed what it answered. */
- (nullable NSPointerFunctionsAcquireFunction)acquireFunction;
- (void)setAcquireFunction:(nullable NSPointerFunctionsAcquireFunction)function;
- (nullable NSPointerFunctionsRelinquishFunction)relinquishFunction;
- (void)setRelinquishFunction:(nullable NSPointerFunctionsRelinquishFunction)function;

/* THE TWO BARRIER FLAGS ARE ABOUT A GARBAGE-COLLECTED RUNTIME, which this system does not have: they are
 * declared because the API has them and are stored and answered, but no collection here reads them. A caller
 * that sets one is not misled — it is simply inert, and this comment is where that is stated. */
- (BOOL)usesStrongWriteBarrier;
- (void)setUsesStrongWriteBarrier:(BOOL)flag;
- (BOOL)usesWeakReadAndWriteBarriers;
- (void)setUsesWeakReadAndWriteBarriers:(BOOL)flag;

@end

/*
 * THE DOORS THE COLLECTIONS USE, spelled with the library's internal `fn` prefix and declared here because
 * NSPointerArray, NSHashTable and NSMapTable are SEPARATE FILES that all need them. They apply the callouts
 * and the CopyIn flag TOGETHER, which is the point of gathering the callouts into an object: no collection
 * has to know which personality it holds, or whether it copies.
 */
@interface NSPointerFunctions (FNInternal)
- (void *)fnAcquire:(const void *)item;
- (void)fnRelinquish:(const void *)item;
- (NSUInteger)fnHash:(const void *)item;
- (BOOL)fnIsEqual:(const void *)item1 to:(const void *)item2;
- (BOOL)fnShouldCopy;
- (NSPointerFunctionsOptions)fnOptions;
@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSPOINTERFUNCTIONS_H */
