/*
 * NSArray.m — the ordered collection that IS a CFArray.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * SELF IS THE ARRAY. There is no second storage to fall out of step with CF's: every door below casts the
 * RECEIVER — not an ivar — to a CFArrayRef and calls CF's own function on it. That is what makes the free cast
 * in both directions true rather than merely compiled, and it is the same shape this library's NSString takes
 * toward CFString.
 *
 * AND BECAUSE SELF IS THE ARRAY, THIS CLASS IS A ROOT CLASS. It inherits nothing, so it answers the NSObject
 * protocol itself — and the reason it must be a root class rather than an NSObject subclass is a word
 * collision, worth stating where the doors are: NSObject's `_refcount` sits at offset 16, and so does CFArray's
 * `_count`. A subclass would shadow CF's count with a refcount, and every inherited NSObject method that reads
 * `_refcount` would read CF's element count. So EVERY DOOR BELOW TAKES CF'S ANSWER WHEREVER CF HAS ONE:
 * -retain/-release are CFRetain/CFRelease, because the one count is CF's; -retainCount asks CF for it;
 * -_cfTypeID asks the bridge which type this class was registered for; +alloc makes an empty CF array, because
 * that is the only kind of object this class can be.
 *
 * THE SENTINEL IS CF'S, because this tree defines no NSNotFound. CFArrayGetFirstIndexOfValue answers
 * kCFNotFound when the value is absent, so the door returns exactly what CF returned rather than translating
 * one sentinel into another.
 */

#import <Foundation/NSArray.h>
#import <Foundation/NSException.h>
#include <objc/runtime.h>
#include <objc/message.h>	/* objc_msgSend, for the two makeObjectsPerformSelector: doors — see their note */

extern unsigned long CFNXBridgeClassToType(Class cls, CFTypeID typeID);

/* THE TWO ANSWERS THIS ROOT CLASS SHARES WITH NSObject RATHER THAN COPYING. The type lookup and the
 * runtime-built format string live in NSObject.m because that is where the bridge map is filled; a second copy
 * here is how two classes come to disagree about which type they are, or about how they describe themselves. */
extern unsigned long FNXTypeIDForClass(Class cls);
extern CFStringRef FNXCreateFormatString(const char *utf8);

/*
 * THE COMPARISON IS OURS; EVERYTHING ELSE IN THE PAIR IS CF'S. This was decided by measurement.
 *
 * kCFTypeArrayCallBacks' equal is CFEqual (CFArray.c:23), and CFEqual's ObjC dispatch only fires for a class
 * CF's runtime has been told about (CFRuntime.c:1078) -- for any other class it falls through to
 * __CFGenericAssertIsCF (CFRuntime.c:1082) and TRAPS. So -containsObject: died with SIGILL on the first
 * object of an unregistered class, while -count and -objectAtIndex: were fine: they never compare.
 *
 * The shim below is the missing half of CF's own delegation rather than a replacement for it: CFEqual MEANT to
 * send -isEqual:, and that is exactly what this sends, for any class. Apple's NSArray compares with -isEqual:
 * too, so the two worlds agree here rather than one being worked around.
 *
 * RETAIN AND RELEASE STAY CF'S OWN. They are the half the object probe measured as working -- a CF array holds
 * an object of this library and is what ends it -- so they are copied verbatim from kCFTypeArrayCallBacks
 * rather than rewritten. Nothing about ownership changes here; only where the comparison comes from.
 *
 * AND APPLE WARNS ABOUT EXACTLY THIS SHAPE, in the same document that lists the bridged types: "If you install
 * a custom callback on a Core Foundation collection you are using, including a NULL callback, its memory
 * management behavior is undefined when accessed from Objective-C." THE DEVIATION HERE IS NARROWER THAN THE
 * WARNING, and stating which part is deliberately taken is what keeps it narrow: only `equal` is replaced,
 * while retain, release and copyDescription are CF's own kCFTypeArrayCallBacks values copied rather than
 * rewritten — so the zone Apple calls undefined is the COMPARISON, and item LIFETIME stays entirely CF's.
 */
static Boolean fnx_array_equal(const void *value1, const void *value2)
{
	if (value1 == value2) {
		return true;
	}
	if (value1 == NULL || value2 == NULL) {
		return false;
	}
	return [(id)value1 isEqual:(id)value2] ? true : false;
}

/* Built once, on first use, from CF's own pair. A plain flag is enough: the two threads that raced here would
 * write the same values, and a HALF-built pair is the one failure that would matter, so the ready flag is set
 * only after every field is in place. */
static CFArrayCallBacks fnx_array_callbacks;
static Boolean fnx_array_callbacks_ready = false;

static const CFArrayCallBacks *fnx_array_callbacks_get(void)
{
	if (!fnx_array_callbacks_ready) {
		fnx_array_callbacks = kCFTypeArrayCallBacks;
		fnx_array_callbacks.equal = fnx_array_equal;
		fnx_array_callbacks_ready = true;
	}
	return &fnx_array_callbacks;
}

@implementation NSArray

/*
 * ALLOCATING AN NSArray CREATES AN EMPTY CF ARRAY, AND THERE IS NO OTHER HONEST ANSWER FOR THIS CLASS. It
 * declares no storage of its own — an NSArray IS a CFArray — so a receiver with no CF storage would be an
 * object whose words are not CF's. And the +alloc it would otherwise inherit is NSObject's, which writes
 * `_refcount` at offset 16: on this class that offset is CF's `_count`, AND a root class holding only `isa` is
 * 8 bytes wide, so the write would be both the wrong word and past the end of the allocation. Creating the
 * real thing avoids both — there is no shell in this design, so there is nothing to size.
 */
+ (id)alloc
{
	return (id)CFArrayCreate(kCFAllocatorDefault, NULL, 0, fnx_array_callbacks_get());
}

- (id)init
{
	/* NOTHING TO DO, AND THAT IS NOT A SHORTCUT: what +alloc handed over already IS an empty CF array. */
	return self;
}

- (instancetype)initWithObjects:(const id _Nonnull * _Nullable)objects count:(NSUInteger)count
{
	/* CF's own retain/release callbacks hold each item as CFArrayCreate places it, so no slot is retained
	 * here; only the comparison is ours (see fnx_array_equal). A count of zero is legal with a NULL vector. */
	CFArrayRef array = CFArrayCreate(kCFAllocatorDefault, (const void **)objects, (CFIndex)count, fnx_array_callbacks_get());

	/* THE RECEIVER IS RELEASED, NOT DISPOSED, BECAUSE IT IS A CF OBJECT RATHER THAN A SHELL. Re-initialising
	 * is not a supported operation; what this does support is the shape the compiler writes for
	 * `[[NSArray alloc] initWithObjects:count:]` — the +alloc'd EMPTY ARRAY is dropped and the filled one is
	 * handed back, which is the class-cluster initialiser contract. CFRelease is the right spelling because
	 * the receiver's +1 came from CFArrayCreate. */
	CFRelease((CFTypeRef)self);

	/* A NULL CFArrayCreate is a failed allocation, and Apple's contract for a failed -init is nil. */
	return (NSArray *)array;
}

/* THE ONE COUNT, AND IT IS CF'S — the file header says why this pair cannot be NSObject's. */
- (id)retain
{
	return (id)CFRetain((CFTypeRef)self);
}

- (void)release
{
	CFRelease((CFTypeRef)self);
}

/* ASKED OF CF, BECAUSE CF IS WHAT HOLDS IT. NSObject answers this from its own `_refcount`, which on this
 * class is CF's `_count` — a three-element array would report a retain count of three. CF's own door is the
 * only answer here that is not a coincidence. */
- (NSUInteger)retainCount
{
	return (NSUInteger)CFGetRetainCount((CFTypeRef)self);
}

- (Class)class
{
	return object_getClass(self);
}

+ (Class)class
{
	return self;
}

- (BOOL)isKindOfClass:(Class)cls
{
	/* THE SAME WALK NSObject USES, taken from the header, so both root classes answer this identically. */
	return FNXClassIsKindOfClass(object_getClass(self), cls);
}

/* CF'S OWN PROTOCOL FOR OBJC OBJECTS, AND IT IS NOT OPTIONAL: CFGetTypeID SENDS THIS rather than reading the
 * object's header when it recognises an object as Objective-C (CFRuntime.c:793). A root class must answer it
 * itself — NSObject's implementation is not inherited. */
- (unsigned long)_cfTypeID
{
	return FNXTypeIDForClass(object_getClass(self));
}

/* IDENTITY AND HASH, WHICH IS WHAT NSObject ANSWERS TOO, AND THEY STAY A COHERENT PAIR.
 *
 * ⚠ A CONTENT-COMPARING -isEqual: IS OWED, AND DELIBERATELY NOT GUESSED AT HERE. Apple's NSArray compares its
 * elements; this answers identity, which is exactly what the class answered before it became a root class, so
 * nothing regresses — and -hash is identity for the same reason. The reason not to simply reach for CFEqual:
 * it dispatches only for a class CF knows (the trap named above), so comparing against an UNBRIDGED object
 * would fall through to __CFGenericAssertIsCF and TRAP rather than answering NO. That is the collection
 * family's question to answer once, together with NSSet and NSDictionary. */
- (BOOL)isEqual:(id)other
{
	return (other == self) ? YES : NO;
}

- (NSUInteger)hash
{
	return (NSUInteger)(uintptr_t)self;
}

/* THE DESCRIPTION DOORS, SHARED WITH NSObject THROUGH THE FORMAT HELPER RATHER THAN BY INHERITANCE.
 *
 * ⚠ AN NSArray's description SHOULD LIST ITS ELEMENTS (Apple's) AND THIS ONE DOES NOT YET — it is NSObject's
 * `<Class: 0x...>` shape, which is again exactly what was inherited before, so nothing regresses. Building the
 * list means a separator, per-element descriptions and a walk of CF's storage; it is owed and named here rather
 * than half-written. */
- (NSString *)description
{
	CFStringRef format = FNXCreateFormatString("<%s: %p>");
	CFStringRef text = NULL;

	if (format != NULL) {
		text = CFStringCreateWithFormat(kCFAllocatorDefault, NULL, format,
			class_getName(object_getClass(self)), (void *)self);
		CFRelease(format);
	}
	return (NSString *)text;
}

+ (NSString *)description
{
	CFStringRef format = FNXCreateFormatString("<%s>");
	CFStringRef text = NULL;

	if (format != NULL) {
		text = CFStringCreateWithFormat(kCFAllocatorDefault, NULL, format, class_getName(self));
		CFRelease(format);
	}
	return (NSString *)text;
}

- (CFStringRef)copyDescription
{
	/* CF's own spelling of the same door, and it RETURNS RETAINED because CF's naming says so. */
	return (CFStringRef)[self description];
}

- (NSUInteger)count
{
	return (NSUInteger)CFArrayGetCount((CFArrayRef)self);
}

- (id _Nonnull)objectAtIndex:(NSUInteger)index
{
	/* OUT OF RANGE RAISES, WHICH IS APPLE'S CONTRACT AND THE ONLY ANSWER THAT CANNOT BE A SILENT ONE. The bound
	 * is CF's own count, so there is one notion of how long this array is; the message names the index and the
	 * bound because a raise that does not say WHAT was wrong costs the caller a debugger.
	 *
	 * AND THIS DOOR WOULD OTHERWISE READ PAST THE STORAGE: CFArrayGetValueAtIndex is CF's unchecked accessor,
	 * so before this check an index past the end was a read of memory this array does not own. */
	if (index >= [self count]) {
		[NSException raise:NSRangeException
		            format:@"*** -[NSArray objectAtIndex:]: index %lu beyond bounds %lu",
		                   (unsigned long)index, (unsigned long)[self count]];
	}
	return (id)CFArrayGetValueAtIndex((CFArrayRef)self, (CFIndex)index);
}

- (id _Nonnull)objectAtIndexedSubscript:(NSUInteger)index
{
	return [self objectAtIndex:index];
}

- (id _Nullable)firstObject
{
	return ([self count] == 0) ? nil : [self objectAtIndex:0];
}

- (id _Nullable)lastObject
{
	NSUInteger n = [self count];

	return (n == 0) ? nil : [self objectAtIndex:n - 1];
}

- (BOOL)containsObject:(id _Nonnull)anObject
{
	/* An array cannot hold nil under Apple's contract, so a nil query is false rather than a search. */
	if (anObject == nil) {
		return NO;
	}
	return CFArrayContainsValue((CFArrayRef)self, CFRangeMake(0, CFArrayGetCount((CFArrayRef)self)), (const void *)anObject);
}

/*
 * THE TWO TRANSLATIONS EVERY DOOR IN THIS BLOCK NEEDS, and both exist because CF and Cocoa disagree about a
 * spelling rather than about a behaviour.
 *
 * fnx_not_found IS THE SENTINEL: CF answers kCFNotFound (-1) and Apple answers NSNotFound (NSIntegerMax), so
 * a door that passed CF's value straight through would fail every Cocoa-style `== NSNotFound` test. The
 * archived library's own note records that as a silent failure — a lookup that never matches and never says so.
 *
 * fnx_check_range IS THE VALIDATION: Apple's range doors RAISE NSRangeException on a range beyond the bounds;
 * CF's own array functions ASSERT instead. Both mean "caller error", and the raise is the one a caller can
 * catch, so the bounds are checked here against CF's own count before CF ever sees the range.
 */
static NSUInteger fnx_not_found(CFIndex found)
{
	return (found == kCFNotFound) ? NSNotFound : (NSUInteger)found;
}

static void fnx_check_range(NSArray *array, NSRange range)
{
	NSUInteger count = [array count];

	/* THE ORDER MATTERS: `range.length > count - range.location` would underflow if location were already
	 * past the end, so location is tested first and the subtraction only runs when it is safe. */
	if (range.location > count || range.length > count - range.location) {
		[NSException raise:NSRangeException
		            format:@"*** -[NSArray]: range {%lu, %lu} out of bounds %lu",
		                   (unsigned long)range.location, (unsigned long)range.length, (unsigned long)count];
	}
}

- (NSUInteger)indexOfObject:(id _Nonnull)anObject
{
	if (anObject == nil) {
		return NSNotFound;
	}
	return fnx_not_found(CFArrayGetFirstIndexOfValue((CFArrayRef)self, CFRangeMake(0, CFArrayGetCount((CFArrayRef)self)), (const void *)anObject));
}

- (NSUInteger)indexOfObject:(id _Nonnull)anObject inRange:(NSRange)range
{
	fnx_check_range(self, range);

	if (anObject == nil) {
		return NSNotFound;
	}
	return fnx_not_found(CFArrayGetFirstIndexOfValue((CFArrayRef)self, CFRangeMake((CFIndex)range.location, (CFIndex)range.length), (const void *)anObject));
}

/*
 * IDENTITY, NOT EQUALITY, WHICH IS WHY THIS ONE IS A SCAN RATHER THAN A CF CALL. CF's index search compares
 * through the array's OWN callbacks — meaning -isEqual:, which is what -indexOfObject: wants. This door wants
 * the same POINTER, and no callback can ask that question, so the walk is ours.
 */
- (NSUInteger)indexOfObjectIdenticalTo:(id _Nonnull)anObject
{
	return [self indexOfObjectIdenticalTo:anObject inRange:NSMakeRange(0, [self count])];
}

- (NSUInteger)indexOfObjectIdenticalTo:(id _Nonnull)anObject inRange:(NSRange)range
{
	NSUInteger i;

	fnx_check_range(self, range);
	for (i = 0; i < range.length; i++) {
		if ([self objectAtIndex:range.location + i] == anObject) {
			return range.location + i;
		}
	}
	return NSNotFound;
}

/* APPLE'S BULK ACCESSOR, AND CF HAS THE SAME ONE — so this delegates rather than walking the array a second
 * time. CFArrayGetValues writes into the caller's buffer through no callbacks at all: it hands back the very
 * pointers the array holds, which is exactly what the door promises. */
- (void)getObjects:(id * _Nonnull)objects
{
	[self getObjects:objects range:NSMakeRange(0, [self count])];
}

- (void)getObjects:(id * _Nonnull)objects range:(NSRange)range
{
	fnx_check_range(self, range);
	CFArrayGetValues((CFArrayRef)self, CFRangeMake((CFIndex)range.location, (CFIndex)range.length), (const void **)objects);
}

/*
 * --- THE DERIVED ARRAYS, AND WHERE THE ITEMS' OWNERSHIP COMES FROM ---------------------------------------
 *
 * EVERY ONE OF THESE BUILDS THE NEW ARRAY WITH **THIS** ARRAY'S CALLBACKS, because CFArrayCreateMutableCopy
 * copies the source's callback pair. So the items are retained by the same policy this array uses — one
 * retention for the new array, not two — and the comparison stays ours (fnx_array_equal) rather than reverting
 * to CF's CFEqual, which cannot compare an object this library built.
 *
 * AND THEY ANSWER +1 WHERE APPLE ANSWERS +0 (autoreleased). No pool exists here, so the header states the
 * deviation once rather than each door pretending otherwise.
 */
- (NSArray *)arrayByAddingObject:(id _Nonnull)anObject
{
	CFMutableArrayRef copy = CFArrayCreateMutableCopy(kCFAllocatorDefault, (CFIndex)[self count] + 1, (CFArrayRef)self);

	if (copy == NULL) {
		return nil;
	}
	CFArrayAppendValue(copy, (const void *)anObject);
	return (NSArray *)copy;
}

- (NSArray *)arrayByAddingObjectsFromArray:(NSArray * _Nonnull)otherArray
{
	CFMutableArrayRef copy = CFArrayCreateMutableCopy(kCFAllocatorDefault,
	                                                  (CFIndex)([self count] + [otherArray count]),
	                                                  (CFArrayRef)self);

	if (copy == NULL) {
		return nil;
	}
	/* CF APPENDS THE OTHER ARRAY'S VALUES THROUGH THIS COPY'S CALLBACKS, so an item that both arrays hold is
	 * retained once per array — which is what ownership means — and never twice for one slot. */
	CFArrayAppendArray(copy, (CFArrayRef)otherArray, CFRangeMake(0, CFArrayGetCount((CFArrayRef)otherArray)));
	return (NSArray *)copy;
}

- (NSArray *)subarrayWithRange:(NSRange)range
{
	CFMutableArrayRef sub;
	NSUInteger i;

	fnx_check_range(self, range);
	sub = CFArrayCreateMutable(kCFAllocatorDefault, (CFIndex)range.length, fnx_array_callbacks_get());
	if (sub == NULL) {
		return nil;
	}
	for (i = 0; i < range.length; i++) {
		CFArrayAppendValue(sub, CFArrayGetValueAtIndex((CFArrayRef)self, (CFIndex)(range.location + i)));
	}
	return (NSArray *)sub;
}

/*
 * COMPARISON DELEGATES TO CF, AND THAT IS ONLY LEGAL BECAUSE BOTH OPERANDS ARE CF-SHAPED. CFEqual's ObjC
 * dispatch is compiled OUT of this build (CFRuntime.c:784-786), so it takes its C path: same type ID, then
 * __CFArrayEqual, which walks the items through the array's OWN callbacks — ours. Two ordinary Foundation
 * objects would have trapped here; two arrays cannot.
 */
- (BOOL)isEqualToArray:(NSArray * _Nonnull)otherArray
{
	if (otherArray == self) {
		return YES;
	}
	if (otherArray == nil || [otherArray count] != [self count]) {
		return NO;
	}
	return CFEqual((CFTypeRef)self, (CFTypeRef)otherArray) ? YES : NO;
}

- (id)firstObjectCommonWithArray:(NSArray * _Nonnull)otherArray
{
	NSUInteger i;

	if (otherArray == nil) {
		return nil;
	}
	for (i = 0; i < [self count]; i++) {
		id candidate = [self objectAtIndex:i];

		if ([otherArray containsObject:candidate]) {
			return candidate;
		}
	}
	/* APPLE ANSWERS THE FIRST ELEMENT OF SELF THAT THE OTHER ARRAY ALSO CONTAINS, and nil when there is none —
	 * which is why the header annotates this door `_Nullable` while the derived arrays are only conditionally
	 * so. */
	return nil;
}

/*
 * --- SENDING NEWS TO THE ELEMENTS -----------------------------------------------------------------------
 *
 * THE ENVELOPE IS SENT, NOT SKIPPED, WHICH IS APPLE'S CONTRACT AND NOT A NICETY: these doors do NOT ask
 * -respondsToSelector: first, so an element that does not implement the selector raises
 * doesNotRecognizeSelector — the caller's mistake, reported where it happened. A version that skipped
 * non-responders would be silently doing less than it says.
 *
 * AND THE DISPATCH IS objc_msgSend RATHER THAN -performSelector:, FOR A REASON WORTH STATING: -performSelector:
 * is an NSObject door this library does not have yet (NSObject's own surface is owed separately), and inventing
 * it here would put a foreign method on a class that is not this file's to change. objc_msgSend IS the message
 * send — the cast only tells the compiler the shape of the call.
 */
- (void)makeObjectsPerformSelector:(SEL _Nonnull)aSelector
{
	NSUInteger i;

	if (aSelector == NULL) {
		[NSException raise:NSInvalidArgumentException
		            format:@"*** -[NSArray makeObjectsPerformSelector:]: NULL selector"];
	}
	for (i = 0; i < [self count]; i++) {
		((void (*)(id, SEL))objc_msgSend)([self objectAtIndex:i], aSelector);
	}
}

- (void)makeObjectsPerformSelector:(SEL _Nonnull)aSelector withObject:(id _Nullable)argument
{
	NSUInteger i;

	if (aSelector == NULL) {
		[NSException raise:NSInvalidArgumentException
		            format:@"*** -[NSArray makeObjectsPerformSelector:withObject:]: NULL selector"];
	}
	for (i = 0; i < [self count]; i++) {
		((void (*)(id, SEL, id))objc_msgSend)([self objectAtIndex:i], aSelector, argument);
	}
}

/*
 * THE BRIDGE, AND WHY THIS CLASS NEEDS IT. Registering the class with CF's type tells CF's runtime that an
 * object of THIS class is a CFArray: it is what makes CFArrayGetCount and its relatives accept one, and it is
 * the same line NSString carries for CFString. The hook that calls this runs when the class exists, which is
 * why this is a separate entry point rather than a constructor.
 *
 * AND THE REGISTRATION IS WHAT MAKES THE DOORS ABOVE NON-RECURSIVE. CF_IS_OBJC(typeID, obj) is an ISA
 * COMPARISON — true when the object's isa is NOT the class registered for that type — so it is FALSE for an
 * object of this class, and CFArrayGetCount called from -count takes CF's own C path instead of dispatching
 * back into -count. An object whose isa differed from the registered class would dispatch, and this file's
 * doors would call themselves.
 */
void _CFNXBridgeArrayClasses(void)
{
	/* Warm the callback pair too, so a caller that somehow reaches +alloc first still gets a complete pair
	 * rather than the all-zero one a static starts as. */
	(void)fnx_array_callbacks_get();
	extern void _FNXBridgeClass(Class cls, unsigned long typeID);

	/* THE REGISTRATION ITSELF. A pattern-based edit removed this line while stripping a diagnostic that sat
	 * beside it -- the second time in one session that a regex took a load-bearing line with it. If this call
	 * disappears, NSArray silently stops being a CFArray to CF and nothing else looks wrong. */
	_FNXBridgeClass([NSArray class], (unsigned long)CFArrayGetTypeID());
}

@end
