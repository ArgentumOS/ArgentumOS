/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSSet.m — the unordered collection with no duplicates (F13.8).
 *
 * MANUAL OWNERSHIP. The two design facts from the header are what this file IS, so they bear repeating:
 * members are RETAINED and lookup is LINEAR by -hash/-isEqual: (not a borrowed dictionary, which
 * would copy its keys), and every mutation REPLACES the member array rather than editing it.
 *
 * FAST ENUMERATION IS THE PROTOCOL'S, done properly: each call fills the CALLER'S buffer with one
 * batch from the member array and advances state->state by what it delivered. Nothing is allocated
 * per loop, and a mutation during the loop is caught by mutationsPtr — the protocol's own answer to
 * that, and a raise rather than a quiet wrong answer.
 */

#import <Foundation/NSSet.h>
#import <Foundation/NSCoder.h>	/* the NSCoding doors call the coder's methods, not just its type */
#import <Foundation/NSArray.h>
#import <Foundation/NSEnumerator.h>
#import <Foundation/NSString.h>
#include <stdlib.h>
#include <stdarg.h>		/* the variadic initializer's list */		/* calloc/free: the variadic factory's exactly-sized list */
/* THE KEYED ARCHIVE'S KEY NAMES, shared with NSKeyedArchiver's structural branch so the NSCoding doors below
 * and that branch cannot spell the same key differently (§63.11). */
#import <Foundation/FNKeyedWire.h>

/* ===================================================================================================
 * THE PRIVATE CONCRETE CLASSES (plan §C.3, M3) - the same shape the array and dictionary families use.
 * THE STORAGE IS THE FRONT'S OWN IVARS, which is why the mutable and counted classes need no second copy
 * of them: NSMutableSet defines no initializers of its own and inherits every one from the front, so the
 * class-choosing guard is a MEMBERSHIP test.
 * =================================================================================================== */
@interface AGSetEmpty : NSSet
+ (AGSetEmpty *)emptySet;
@end

@interface AGSetItems : NSSet
@end

@interface AGSetMutable : NSMutableSet
@end


@implementation NSSet

/* THE DOOR IS `+alloc` (§C.3 item 1): a concrete class INHERITS this, and `[super alloc]` starts the
 * lookup at NSSet's superclass with the receiver still being the class that was asked, so the routing
 * happens exactly ONCE, at the front. */
+ (id)alloc
{
	if (self != [NSSet class]) {
		return [super alloc];
	}
	return [AGSetItems alloc];
}


+ (instancetype)set
{
	return [[self alloc] initWithObjects:NULL count:0];
}

+ (instancetype)setWithObject:(id)object
{
	return [[self alloc] initWithObjects:&object count:1];
}

+ (instancetype)setWithObjects:(const id _Nonnull * _Nullable)objects count:(NSUInteger)count
{
	return [[self alloc] initWithObjects:objects count:count];
}

/* THE VARIADIC FACTORY: the same door as the count form above, with the list terminated by nil. TWO PASSES
 * over the list, exactly as `NSArray`'s variadic factory does it — the storage has to be exactly sized and a
 * va_list cannot be rewound without a copy. The empty call answers the shared empty instance through the
 * initializer's own rule rather than by a second path. */
+ (instancetype)setWithObjects:(id)firstObject, ...
{
	va_list args;
	va_list counter;
	id *objects;
	size_t extra = 0;
	size_t i;
	id result;

	if (firstObject == nil) {
		return [[self alloc] initWithObjects:NULL count:0];
	}
	va_start(args, firstObject);
	va_copy(counter, args);
	while (va_arg(counter, id) != nil) {
		extra++;
	}
	va_end(counter);
	objects = (id *)calloc(extra + 2, sizeof(id));
	if (objects == NULL) {
		va_end(args);
		return nil;
	}
	objects[0] = firstObject;
	for (i = 0; i < extra; i++) {
		objects[i + 1] = va_arg(args, id);
	}
	objects[extra + 1] = nil;
	va_end(args);
	result = [[self alloc] initWithObjects:objects count:extra + 1];
	free(objects);
	return result;
}

+ (instancetype)setWithArray:(NSArray *)array
{
	return [[self alloc] initWithArray:array];
}

+ (instancetype)setWithSet:(NSSet *)set
{
	return [[self alloc] initWithSet:set];
}

- (instancetype)initWithObjects:(const id _Nonnull * _Nullable)objects count:(NSUInteger)count
{
	NSMutableArray *members;
	NSUInteger i;

	/* THE CLASS IS CHOSEN BY THE DATA (§C.3 item 2), for this family's general class only - a
	 * mutable receiver inherits this implementation and must keep it. Both constructions here are
	 * COMPLETE (the member array is assigned at the end), which is what makes answering the shared
	 * empty instance safe. */
	if ([self isMemberOfClass:[AGSetItems class]] && count == 0) {
		[self release];	/* never initialized: the storage was never built */
		return (id)[AGSetEmpty emptySet];
	}
	self = [super init];
	if (self == nil) {
		return nil;
	}
	members = [NSMutableArray array];
	for (i = 0; i < count; i++) {
		id object = objects[i];
		NSUInteger j;
		BOOL seen = NO;

		if (object == nil) {
			continue;
		}
		/* DEDUPLICATION IS BY VALUE, which is the whole contract: the FIRST of two equal members
		 * is the one that stays, as in Cocoa. */
		for (j = 0; j < [members count]; j++) {
			if ([[members objectAtIndex:j] isEqual:object]) {
				seen = YES;
				break;
			}
		}
		if (!seen) {
			[members addObject:object];
		}
	}
	/* A SNAPSHOT, so -allObjects hands out nothing that can be mutated through this class. */
	_members = [members copy];
	_mutations = 0;
	return self;
}

- (instancetype)initWithArray:(NSArray *)array
{
	NSMutableArray *members;
	NSUInteger i;

	/* THE CLASS IS CHOSEN BY THE DATA (§C.3 item 2), for this family's general class only - a
	 * mutable receiver inherits this implementation and must keep it. Both constructions here are
	 * COMPLETE (the member array is assigned at the end), which is what makes answering the shared
	 * empty instance safe. */
	if ([self isMemberOfClass:[AGSetItems class]] && [array count] == 0) {
		[self release];	/* never initialized: the storage was never built */
		return (id)[AGSetEmpty emptySet];
	}
	self = [super init];
	if (self == nil) {
		return nil;
	}
	members = [NSMutableArray array];
	for (i = 0; i < [array count]; i++) {
		id object = [array objectAtIndex:i];
		NSUInteger j;
		BOOL seen = NO;

		if (object == nil) {
			continue;
		}
		for (j = 0; j < [members count]; j++) {
			if ([[members objectAtIndex:j] isEqual:object]) {
				seen = YES;
				break;
			}
		}
		if (!seen) {
			[members addObject:object];
		}
	}
	_members = [members copy];
	_mutations = 0;
	return self;
}

- (instancetype)initWithSet:(NSSet *)set
{
	return [self initWithArray:[set allObjects]];
}

/* THE VARIADIC INITIALIZER, built the way the variadic FACTORY above is built (two passes over the list,
 * because a va_list cannot be rewound and the storage has to be exactly sized) and handed to the same
 * -initWithObjects:count: funnel, so the dedup rule and the class-choosing rule stay the initializer's. */
- (instancetype)initWithObjects:(id)firstObject, ...
{
	va_list args;
	va_list counter;
	id *objects;
	size_t extra = 0;
	size_t i;
	id result;

	if (firstObject == nil) {
		return [self initWithObjects:NULL count:0];
	}
	va_start(args, firstObject);
	va_copy(counter, args);
	while (va_arg(counter, id) != nil) {
		extra++;
	}
	va_end(counter);
	objects = (id *)calloc(extra + 2, sizeof(id));
	if (objects == NULL) {
		va_end(args);
		return nil;
	}
	objects[0] = firstObject;
	for (i = 0; i < extra; i++) {
		objects[i + 1] = va_arg(args, id);
	}
	objects[extra + 1] = nil;
	va_end(args);
	result = [self initWithObjects:objects count:extra + 1];
	free(objects);
	return result;
}

/* -copy SENT ONCE PER MEMBER, and the copies are autoreleased here because the set retains them: what the
 * caller gains is a set whose members cannot be changed underneath it by whoever owns the originals. */
- (instancetype)initWithSet:(NSSet *)set copyItems:(BOOL)copyItems
{
	NSMutableArray *copies;
	NSArray *members;
	NSUInteger i;

	if (!copyItems) {
		return [self initWithSet:set];
	}
	members = [set allObjects];
	copies = [NSMutableArray array];
	for (i = 0; i < [members count]; i++) {
		[copies addObject:[[[members objectAtIndex:i] copy] autorelease]];
	}
	return [self initWithArray:copies];
}

/* ===================================================================================================
 * THE NSCoding DOORS (§63.11). What they are FOR, since the archiver does not need them, is in the header.
 * One key, and `-initWithArray:` is the funnel both ends meet at — so the dedup rule and the class-choosing
 * rule stay the INITIALIZER's.
 * =================================================================================================== */
- (nullable instancetype)initWithCoder:(NSCoder *)coder
{
	return [self initWithArray:[coder decodeObjectForKey:FNKeyedObjectsKey]];
}

- (void)encodeWithCoder:(NSCoder *)coder
{
	/* A COPY, AND THAT IS THE POINT: encoding `self` under this key would ask the coder for the very object it
	 * is in the middle of writing, and the archive would record the set referring to itself. */
	[coder encodeObject:[NSArray arrayWithArray:[self allObjects]] forKey:FNKeyedObjectsKey];
}

- (NSUInteger)count
{
	return [_members count];
}

- (nullable id)member:(id)object
{
	NSUInteger i;

	if (object == nil) {
		return nil;
	}
	for (i = 0; i < [_members count]; i++) {
		id candidate = [_members objectAtIndex:i];

		if ([candidate isEqual:object]) {
			return candidate;
		}
	}
	return nil;
}

- (BOOL)containsObject:(id)object
{
	return [self member:object] != nil;
}

- (nullable id)anyObject
{
	/* OVER THE PRIMITIVE, and this one was MISSED by the substitution that moved its neighbours: it read
	 * `[_members objectAtIndex:0]`, which the pattern `[_members objectAtIndex:i]` did not match. For a
	 * concrete class with no member array that is not a crash - messaging nil answers nil - it is a
	 * SILENTLY WRONG ANSWER, which is worse, and the probe's third-party check is what caught it. */
	return [[self objectEnumerator] nextObject];
}

- (NSArray *)allObjects
{
	NSMutableArray *out = [NSMutableArray arrayWithCapacity:[self count]];
	NSEnumerator *enumerator = [self objectEnumerator];
	id object;

	/* OVER THE PRIMITIVES (§C.3 item 5). This used to hand back the internal member array, which a concrete
	 * class with a different layout does not have - the empty one has none at all. */
	while ((object = [enumerator nextObject]) != nil) {
		[out addObject:object];
	}
	return out;
}

- (NSEnumerator *)objectEnumerator
{
	return [_members objectEnumerator];
}

/* ONE WALK, two doors: the plain form is the options form with no options, so the reverse case and the
 * stopping case are the same arithmetic rather than two loops that could drift apart. The member array is
 * taken ONCE - the old loop asked -allObjects for every element. */
- (void)enumerateObjectsWithOptions:(NSEnumerationOptions)options usingBlock:(void (^)(id object, BOOL *stop))block
{
	NSArray *members = [self allObjects];
	NSUInteger n = [members count];
	NSUInteger k;
	BOOL stop = NO;

	if (block == NULL) {
		return;
	}
	for (k = 0; k < n; k++) {
		NSUInteger i = (options & NSEnumerationReverse) != 0 ? (n - 1 - k) : k;

		block([members objectAtIndex:i], &stop);
		if (stop) {
			break;
		}
	}
}

- (void)makeObjectsPerformSelector:(SEL)aSelector
{
	NSArray *members = [self allObjects];
	NSUInteger i;

	for (i = 0; i < [members count]; i++) {
		[[members objectAtIndex:i] performSelector:aSelector];
	}
}

- (void)makeObjectsPerformSelector:(SEL)aSelector withObject:(nullable id)argument
{
	NSArray *members = [self allObjects];
	NSUInteger i;

	for (i = 0; i < [members count]; i++) {
		[[members objectAtIndex:i] performSelector:aSelector withObject:argument];
	}
}

/* THE TEST DOORS COLLECT, AND AN UNORDERED COLLECTION NEEDS NO ORDER TO DO IT. The answer is built through
 * -initWithSet: rather than returned as the mutable accumulator - -copy on this family is a retain, so a
 * frozen result has to be BUILT. */
- (NSSet *)objectsWithOptions:(NSEnumerationOptions)options passingTest:(BOOL (^)(id object, BOOL *stop))predicate
{
	NSMutableSet *picked = [[NSMutableSet alloc] init];
	NSSet *answer;
	NSArray *members = [self allObjects];
	NSUInteger n = [members count];
	NSUInteger k;
	BOOL stop = NO;

	if (predicate != NULL) {
		for (k = 0; k < n; k++) {
			NSUInteger i = (options & NSEnumerationReverse) != 0 ? (n - 1 - k) : k;
			id member = [members objectAtIndex:i];
			BOOL stopHere = NO;

			if (predicate(member, &stopHere)) {
				[picked addObject:member];
			}
			if (stopHere || stop) {
				break;
			}
		}
	}
	answer = [[NSSet alloc] initWithSet:picked];
	[picked release];
	return [answer autorelease];
}

- (NSSet *)objectsPassingTest:(BOOL (^)(id object, BOOL *stop))predicate
{
	return [self objectsWithOptions:0 passingTest:predicate];
}

- (void)enumerateObjectsUsingBlock:(void (^)(id object, BOOL *stop))block
{
	NSUInteger i;
	BOOL stop = NO;

	if (block == NULL) {
		return;
	}
	for (i = 0; i < [self count]; i++) {
		block([[self allObjects] objectAtIndex:i], &stop);
		if (stop) {
			break;
		}
	}
}

- (BOOL)isEqualToSet:(NSSet *)other
{
	NSUInteger i;

	if (other == nil || [other count] != [self count]) {
		return NO;
	}
	for (i = 0; i < [self count]; i++) {
		if (![other containsObject:[[self allObjects] objectAtIndex:i]]) {
			return NO;
		}
	}
	return YES;
}

- (BOOL)isSubsetOfSet:(NSSet *)other
{
	NSUInteger i;

	if (other == nil || [self count] > [other count]) {
		return NO;
	}
	for (i = 0; i < [self count]; i++) {
		if (![other containsObject:[[self allObjects] objectAtIndex:i]]) {
			return NO;
		}
	}
	return YES;
}

- (BOOL)intersectsSet:(NSSet *)other
{
	NSUInteger i;

	if (other == nil) {
		return NO;
	}
	for (i = 0; i < [self count]; i++) {
		if ([other containsObject:[[self allObjects] objectAtIndex:i]]) {
			return YES;
		}
	}
	return NO;
}

- (instancetype)setByAddingObject:(id)object
{
	NSMutableSet *copy = [self mutableCopy];

	[copy addObject:object];
	return copy;
}

- (instancetype)setByAddingObjectsFromSet:(NSSet *)other
{
	NSMutableSet *copy = [self mutableCopy];

	[copy unionSet:other];
	return copy;
}

- (instancetype)setByAddingObjectsFromArray:(NSArray *)other
{
	NSMutableSet *copy = [self mutableCopy];

	[copy addObjectsFromArray:other];
	return copy;
}

- (NSArray *)sortedArrayUsingDescriptors:(NSArray *)descriptors
{
	return [[self allObjects] sortedArrayUsingDescriptors:descriptors];
}

- (BOOL)isEqual:(id)other
{
	if (other == self) {
		return YES;
	}
	if (other == nil || ![other isKindOfClass:[NSSet class]]) {
		return NO;
	}
	return [self isEqualToSet:(NSSet *)other];
}

/* ORDER-INDEPENDENT BY CONSTRUCTION, which is what a set's hash has to be: equal sets hash alike.
 * Counting is enough to be correct; it is not trying to be clever. */
- (NSUInteger)hash
{
	return [self count];
}

/* THE MEMBER LADDER IS THE ARRAY FAMILY'S, applied to the members of an unordered collection: a member
 * that can describe itself with a locale AND an indent does (a nested array), one that takes only a
 * locale does, and anything else answers -description. */
static void fnAppendSetMember(NSMutableString *out, id member, id locale, NSUInteger level)
{
	NSUInteger i;

	for (i = 0; i < level; i++) {
		[out appendString:@"    "];
	}
	if (locale != nil && [member respondsToSelector:@selector(descriptionWithLocale:indent:)]) {
		[out appendString:[member descriptionWithLocale:locale indent:level]];
	} else if (locale != nil && [member respondsToSelector:@selector(descriptionWithLocale:)]) {
		[out appendString:[member descriptionWithLocale:locale]];
	} else {
		[out appendString:[member description]];
	}
}

- (NSString *)descriptionWithLocale:(nullable id)locale
{
	NSMutableString *out = [[NSMutableString alloc] initWithUTF8String:"{(\n"];
	NSArray *members = [self allObjects];
	NSUInteger i;

	for (i = 0; i < [members count]; i++) {
		fnAppendSetMember(out, [members objectAtIndex:i], locale, 1);
		if (i + 1 < [members count]) {
			[out appendString:@","];
		}
		[out appendString:@"\n"];
	}
	[out appendString:@")}"];
	return [out autorelease];
}

- (NSString *)description
{
	NSMutableString *out = [NSMutableString stringWithString:@"{("];
	NSUInteger i;

	for (i = 0; i < [self count]; i++) {
		if (i > 0) {
			[out appendString:@", "];
		}
		[out appendString:[[[self allObjects] objectAtIndex:i] description]];
	}
	[out appendString:@")}"];
	return out;
}

/* Immutable, so copying is itself and a mutable copy is a real one. */
/* §C.3 item 4: an archiver asks for THIS, never for -class. */
- (Class)classForCoder
{
	return [NSSet class];
}

- (id)copy
{
	return [self retain];	/* +1: `copy` is an OWNED family (plan §15.2) */
}

- (id)mutableCopy
{
	return [[NSMutableSet alloc] initWithSet:self];
}

- (unsigned long)countByEnumeratingWithState:(NSFastEnumerationState *)state
				     objects:(id __unsafe_unretained *)buffer
				       count:(unsigned long)length
{
	unsigned long cursor = state->state;
	unsigned long produced = 0;
	unsigned long skip;
	NSEnumerator *enumerator = [self objectEnumerator];
	id object;

	/*
	 * OVER THE PRIMITIVES, THROUGH THE CALLER'S BUFFER. It used to batch out of the internal member array,
	 * which is storage a concrete class with a different layout does not have; `objects` is caller-provided
	 * scratch that stays valid for the batch, and `state->state` is the cursor, so a short batch RESUMES.
	 */
	for (skip = 0; skip < cursor && (object = [enumerator nextObject]) != nil; skip++) {
	}
	while (produced < length && (object = [enumerator nextObject]) != nil) {
		buffer[produced++] = object;
	}
	state->mutationsPtr = &_mutations;
	if (produced == 0) {
		return 0;
	}
	state->itemsPtr = buffer;
	state->state = cursor + produced;
	return produced;
}

@end

@implementation NSMutableSet

/* THE SAME DOOR (§C.3 item 1), and the mutable front answers ITSELF to an archiver (§C.3 item 4) - the
 * bullet that keeps the private concrete names out of every archive (§C.4). */
+ (id)alloc
{
	if (self != [NSMutableSet class]) {
		return [super alloc];
	}
	return [AGSetMutable alloc];
}

- (Class)classForCoder
{
	return [NSMutableSet class];
}


+ (instancetype)setWithCapacity:(NSUInteger)capacity
{
	return [[self alloc] initWithCapacity:capacity];
}

- (instancetype)initWithCapacity:(NSUInteger)capacity
{
	(void)capacity;
	return [self initWithObjects:NULL count:0];
}

/* THE SAME DOOR ON THE MUTABLE CLASS, which Apple declares here too and which therefore needs an
 * implementation HERE: `--unimplemented` counts an implementation in the class or a SUBCLASS, so the front's
 * body does not satisfy this declaration. `[super initWithCoder:]` reaches the front's, which funnels through
 * `-initWithArray:` with `self` still the MUTABLE class — and the class-choosing rule sends only
 * `AGSetItems` (the immutable concrete class) to the shared empty instance, so a mutable answer stays
 * mutable. */
- (nullable instancetype)initWithCoder:(NSCoder *)coder
{
	return [super initWithCoder:coder];
}

/* ONE PLACE REPLACES THE MEMBERS, so the mutation token is bumped exactly when the storage changes
 * and never by accident. */
- (void)fnReplaceMembers:(NSArray *)members
{
	_members = [members copy];
	_mutations++;
}

- (void)addObject:(id)object
{
	NSMutableArray *members;

	if (object == nil || [self member:object] != nil) {
		return;			/* a set has no duplicates, and nil is not a member */
	}
	members = [NSMutableArray arrayWithCapacity:[self count] + 1];
	[members addObjectsFromArray:[self allObjects]];
	[members addObject:object];
	[self fnReplaceMembers:members];
}

- (void)removeObject:(id)object
{
	NSMutableArray *members;
	NSUInteger i;

	if (object == nil) {
		return;
	}
	members = [NSMutableArray arrayWithCapacity:[self count]];
	for (i = 0; i < [self count]; i++) {
		id candidate = [[self allObjects] objectAtIndex:i];

		if (![candidate isEqual:object]) {
			[members addObject:candidate];
		}
	}
	[self fnReplaceMembers:members];
}

- (void)removeAllObjects
{
	[self fnReplaceMembers:[NSArray array]];
}

- (void)addObjectsFromArray:(NSArray *)array
{
	NSUInteger i;

	for (i = 0; i < [array count]; i++) {
		[self addObject:[array objectAtIndex:i]];
	}
}

- (void)unionSet:(NSSet *)other
{
	[self addObjectsFromArray:[other allObjects]];
}

- (void)minusSet:(NSSet *)other
{
	NSArray *theirs = [other allObjects];
	NSUInteger i;

	for (i = 0; i < [theirs count]; i++) {
		[self removeObject:[theirs objectAtIndex:i]];
	}
}

- (void)intersectSet:(NSSet *)other
{
	NSMutableArray *members;
	NSUInteger i;

	members = [NSMutableArray arrayWithCapacity:[self count]];
	for (i = 0; i < [self count]; i++) {
		id candidate = [[self allObjects] objectAtIndex:i];

		if ([other containsObject:candidate]) {
			[members addObject:candidate];
		}
	}
	[self fnReplaceMembers:members];
}

- (void)setSet:(NSSet *)other
{
	[self fnReplaceMembers:[other allObjects]];
}

/* A MUTABLE SET COPIES BY VALUE — the members, not a shared reference — and a copy of one is the
 * immutable snapshot the house's rule asks for. */
- (id)copy
{
	NSSet *snapshot;

	snapshot = [[NSSet alloc] initWithArray:[self allObjects]];
	return snapshot;
}

- (id)mutableCopy
{
	return [[NSMutableSet alloc] initWithArray:[self allObjects]];
}

@end


/* ===================================================================================================
 * THE CONCRETE CLASSES (§C.3 items 2, 3 and 8).
 * =================================================================================================== */

@implementation AGSetItems

/* [[NSSet alloc] init] IS A LEGITIMATE THING TO WRITE (§C.3 item 1) AND IT IS THE EMPTY CASE. Here, unlike
 * the dictionary family, it is implemented on the concrete class rather than the front, because every
 * constructor in this family is COMPLETE: none of them allocates and then fills. */
- (instancetype)init
{
	[self release];
	return (id)[AGSetEmpty emptySet];
}

@end

@implementation AGSetEmpty

+ (AGSetEmpty *)emptySet
{
	static AGSetEmpty *shared = nil;

	if (shared == nil) {
		shared = [[AGSetEmpty alloc] init];
	}
	return shared;
}

/* IMMORTAL, the price of a singleton in a library with no `+allocWithZone:` and no collector. */
- (id)retain { return self; }
- (void)release { }
- (id)autorelease { return self; }
- (NSUInteger)retainCount { return NSUIntegerMax; }

/* THE THREE PRIMITIVES (§C.3 item 5) AND NOTHING ELSE. */
- (NSUInteger)count
{
	return 0;
}

- (nullable id)member:(id)object
{
	(void)object;
	return nil;
}

- (NSEnumerator *)objectEnumerator
{
	return [[NSArray array] objectEnumerator];
}

@end

@implementation AGSetMutable

/* NOTHING TO IMPLEMENT: NSMutableSet's implementation IS the mutable storage implementation, and what a
 * caller gains is the NAME that -class answers. */

@end
