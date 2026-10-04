/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSCoder.m — the abstract base (F13.12). MANUAL OWNERSHIP.
 *
 * EVERY DOOR RAISES, and that is the class's whole content: a coder that cannot encode has no
 * sensible answer for any of them, and answering a zero would be indistinguishable from data. The
 * message NAMES the door and the class, so a caller who reached here by mistake knows which half of
 * the pair is missing — an archiver where a unarchiver was wanted, or a subclass that implemented
 * only some of the protocol.
 */

#import <Foundation/NSCoder.h>
#import <Foundation/NSString.h>
#import <Foundation/NSSet.h>
#import <Foundation/NSError.h>
#import <Foundation/NSDictionary.h>	/* for the userInfo literal in -decodeBytesWithMinimumLength: */
#import <Foundation/NSException.h>
#include <stdarg.h>

static void fn_abstract(Class cls, SEL door)
{
	[NSException raise:NSInvalidArgumentException
		    format:@"%@: -%s is a door the abstract NSCoder does not implement — "
			   "use NSKeyedArchiver or NSKeyedUnarchiver", cls, sel_getName(door)];
}

/* THE SEQUENTIAL FAMILY'S ADVICE IS THE OTHER ONE, and this message is the only place a caller in a
 * sequential context is told where to go (§62.86). */
static void fn_abstract_sequential(Class cls, SEL door)
{
	[NSException raise:NSInvalidArgumentException
		    format:@"%@: -%s is a SEQUENTIAL door — use NSArchiver or NSUnarchiver; the keyed "
			   "family answers keys, not order and type", cls, sel_getName(door)];
}

@implementation NSCoder

/* --- THE INSPECTION AND DECODE-ERROR SURFACE ------------------------------------------------------- */

/* THESE ARE THE DEFAULTS, NOT STUBS, and the difference matters: a coder that has said nothing allows
 * no keyed coding, requires no secure coding, has no allow-list, records no error, and raises on
 * failure — which is exactly what this base's own doors do. `NSKeyedArchiver`/`NSKeyedUnarchiver`
 * override the ones that are not the default for them. */
- (BOOL)allowsKeyedCoding
{
	return NO;
}

- (BOOL)requiresSecureCoding
{
	return _requiresSecureCoding;
}

- (void)setRequiresSecureCoding:(BOOL)flag
{
	_requiresSecureCoding = flag;
}

- (nullable NSSet *)allowedClasses
{
	return _allowedClasses;
}

- (void)setAllowedClasses:(nullable NSSet *)classes
{
	/* COPY, not retain: the list is a policy SNAPSHOT. A caller that mutates its own set after handing
	 * it over must not thereby widen what a decode already in flight will accept. */
	if (classes != _allowedClasses) {
		[_allowedClasses release];
		_allowedClasses = [classes copy];
	}
}

- (nullable NSError *)error
{
	return _error;
}

- (NSDecodingFailurePolicy)decodingFailurePolicy
{
	return _decodingFailurePolicy;
}

- (void)setDecodingFailurePolicy:(NSDecodingFailurePolicy)policy
{
	_decodingFailurePolicy = policy;
}

- (nullable NSString *)systemVersion
{
	return nil;	/* this library records none; nil is "unknown", which is not a version */
}

- (void)dealloc
{
	[_allowedClasses release];
	[_error release];
	[super dealloc];
}

/* `-failWithError:` IS THE DECODER'S "I CANNOT ANSWER", and WHAT IT DOES is `-decodingFailurePolicy`'s
 * business: `NSKeyedUnarchiver` overrides this to honour the policy. A coder that overrides nothing has
 * no policy but the default, and the default is to RAISE — so this base raises, carrying THE ERROR'S OWN
 * description.
 *
 * ⚠ AND THAT MESSAGE IS A CORRECTION (§63.43, 2026-10-01). It used to name the absent door ("the abstract
 * NSCoder does not implement -failWithError:") — which was a lie the moment a SEQUENTIAL door needed it:
 * `NSUnarchiver` overrides nothing here, so `-decodeBytesWithMinimumLength:`'s short-run refusal would
 * have reported an abstract class that does implement the door that failed. A failure message has to name
 * the failure, and the failure is the caller's. */
- (void)failWithError:(NSError *)error
{
	[NSException raise:NSInvalidArgumentException
		    format:@"%@: %@", [self class],
			   (error != nil ? [error localizedDescription] : @"the decoder failed")];
}

/* --- THE SEQUENTIAL OBJECT-CONVENTIONS, AS THEIR EQUIVALENCE (see the file note in NSCoder.h) ----- */

- (void)encodeBycopyObject:(nullable id)object
{
	[self encodeObject:object];
}

- (void)encodeByrefObject:(nullable id)object
{
	[self encodeObject:object];
}

- (void)encodeConditionalObject:(nullable id)object
{
	[self encodeObject:object];
}

- (void)encodeObject:(nullable id)object forKey:(NSString *)key
{
	(void)object;
	(void)key;
	fn_abstract([self class], _cmd);
}

- (nullable id)decodeObjectForKey:(NSString *)key
{
	(void)key;
	fn_abstract([self class], _cmd);
	return nil;
}

- (void)encodeBool:(BOOL)value forKey:(NSString *)key
{
	(void)value;
	(void)key;
	fn_abstract([self class], _cmd);
}

- (BOOL)decodeBoolForKey:(NSString *)key
{
	(void)key;
	fn_abstract([self class], _cmd);
	return NO;
}

- (void)encodeInt:(int)value forKey:(NSString *)key
{
	(void)value;
	(void)key;
	fn_abstract([self class], _cmd);
}

- (int)decodeIntForKey:(NSString *)key
{
	(void)key;
	fn_abstract([self class], _cmd);
	return 0;
}

- (void)encodeInteger:(NSInteger)value forKey:(NSString *)key
{
	(void)value;
	(void)key;
	fn_abstract([self class], _cmd);
}

- (NSInteger)decodeIntegerForKey:(NSString *)key
{
	(void)key;
	fn_abstract([self class], _cmd);
	return 0;
}

- (void)encodeDouble:(double)value forKey:(NSString *)key
{
	(void)value;
	(void)key;
	fn_abstract([self class], _cmd);
}

- (double)decodeDoubleForKey:(NSString *)key
{
	(void)key;
	fn_abstract([self class], _cmd);
	return 0;
}

- (void)encodeFloat:(float)value forKey:(NSString *)key
{
	(void)value;
	(void)key;
	fn_abstract([self class], _cmd);
}

- (float)decodeFloatForKey:(NSString *)key
{
	(void)key;
	fn_abstract([self class], _cmd);
	return 0;
}

- (void)encodeBytes:(const void *)bytes length:(NSUInteger)length forKey:(NSString *)key
{
	(void)bytes;
	(void)length;
	(void)key;
	fn_abstract([self class], _cmd);
}

- (nullable const void *)decodeBytesForKey:(NSString *)key
			    returnedLength:(nullable NSUInteger *)lengthp
{
	(void)key;
	(void)lengthp;
	fn_abstract([self class], _cmd);
	return NULL;
}

- (BOOL)containsValueForKey:(NSString *)key
{
	(void)key;
	fn_abstract([self class], _cmd);
	return NO;
}

- (void)encodeInt32:(int32_t)value forKey:(NSString *)key
{
	(void)value;
	(void)key;
	fn_abstract([self class], _cmd);
}

- (int32_t)decodeInt32ForKey:(NSString *)key
{
	(void)key;
	fn_abstract([self class], _cmd);
	return 0;
}

- (void)encodeInt64:(int64_t)value forKey:(NSString *)key
{
	(void)value;
	(void)key;
	fn_abstract([self class], _cmd);
}

- (int64_t)decodeInt64ForKey:(NSString *)key
{
	(void)key;
	fn_abstract([self class], _cmd);
	return 0;
}

- (nullable const void *)decodeBytesForKey:(NSString *)key minimumLength:(NSUInteger)minimumLength
{
	(void)key;
	(void)minimumLength;
	fn_abstract([self class], _cmd);
	return NULL;
}

- (nullable id)decodePropertyListForKey:(NSString *)key
{
	(void)key;
	fn_abstract([self class], _cmd);
	return nil;
}

/* ⚠ THE `CG`-SPELLED KEYED GEOMETRY DOORS ARE NOT DECLARED OR IMPLEMENTED HERE (§63.54): they are the
 * CoreGraphics tier's — `userland/CoreGraphics/NSCoderCGGeometry.{h,m}`, a category on `NSCoder` over the PUBLIC
 * keyed doors. The `NS`-spelled five below stay, because macOS's `Foundation/NSGeometry.h` declares them — and
 * what told the two spellings apart is the same measurement §63.53 made: NOTHING IN THIS LIBRARY COULD, since
 * `NSPoint` IS `CGPoint` here and both box the same bytes under the same encoding. */

- (void)encodePoint:(NSPoint)point forKey:(NSString *)key
{
	(void)point;
	(void)key;
	fn_abstract([self class], _cmd);
}

- (NSPoint)decodePointForKey:(NSString *)key
{
	(void)key;
	fn_abstract([self class], _cmd);
	return (NSPoint){ 0, 0 };
}

- (void)encodeSize:(NSSize)size forKey:(NSString *)key
{
	(void)size;
	(void)key;
	fn_abstract([self class], _cmd);
}

- (NSSize)decodeSizeForKey:(NSString *)key
{
	(void)key;
	fn_abstract([self class], _cmd);
	return (NSSize){ 0, 0 };
}

- (void)encodeRect:(NSRect)rect forKey:(NSString *)key
{
	(void)rect;
	(void)key;
	fn_abstract([self class], _cmd);
}

- (NSRect)decodeRectForKey:(NSString *)key
{
	(void)key;
	fn_abstract([self class], _cmd);
	return (NSRect){ { 0, 0 }, { 0, 0 } };
}

- (nullable id)decodeObjectOfClass:(Class)aClass forKey:(NSString *)key
{
	(void)aClass;
	(void)key;
	fn_abstract([self class], _cmd);
	return nil;
}

- (nullable id)decodeObjectOfClasses:(nullable NSSet *)classes forKey:(NSString *)key
{
	(void)classes;
	(void)key;
	fn_abstract([self class], _cmd);
	return nil;
}

- (nullable NSArray *)decodeArrayOfObjectsOfClass:(Class)cls forKey:(NSString *)key
{
	(void)cls;
	(void)key;
	fn_abstract([self class], _cmd);
	return nil;
}

- (nullable NSArray *)decodeArrayOfObjectsOfClasses:(nullable NSSet *)classes forKey:(NSString *)key
{
	(void)classes;
	(void)key;
	fn_abstract([self class], _cmd);
	return nil;
}

- (nullable NSDictionary *)decodeDictionaryWithKeysOfClass:(Class)keyClass
						    objectsOfClass:(Class)objectClass
							    forKey:(NSString *)key
{
	(void)keyClass;
	(void)objectClass;
	(void)key;
	fn_abstract([self class], _cmd);
	return nil;
}

- (nullable NSDictionary *)decodeDictionaryWithKeysOfClasses:(nullable NSSet *)keyClasses
						     objectsOfClasses:(nullable NSSet *)objectClasses
							      forKey:(NSString *)key
{
	(void)keyClasses;
	(void)objectClasses;
	(void)key;
	fn_abstract([self class], _cmd);
	return nil;
}

- (void)encodeConditionalObject:(nullable id)object forKey:(NSString *)key
{
	(void)object;
	(void)key;
	fn_abstract([self class], _cmd);
}

- (nullable id)decodeTopLevelObjectAndReturnError:(NSError * _Nullable * _Nullable)error
{
	if (error != NULL) {
		*error = nil;
	}
	fn_abstract([self class], _cmd);
	return nil;
}

- (nullable id)decodeTopLevelObjectForKey:(NSString *)key
				    error:(NSError * _Nullable * _Nullable)error
{
	(void)key;
	if (error != NULL) {
		*error = nil;
	}
	fn_abstract([self class], _cmd);
	return nil;
}

- (nullable id)decodeTopLevelObjectOfClass:(Class)cls
				    forKey:(NSString *)key
				     error:(NSError * _Nullable * _Nullable)error
{
	(void)cls;
	(void)key;
	if (error != NULL) {
		*error = nil;
	}
	fn_abstract([self class], _cmd);
	return nil;
}

- (nullable id)decodeTopLevelObjectOfClasses:(nullable NSSet *)classes
				      forKey:(NSString *)key
				       error:(NSError * _Nullable * _Nullable)error
{
	(void)classes;
	(void)key;
	if (error != NULL) {
		*error = nil;
	}
	fn_abstract([self class], _cmd);
	return nil;
}

- (void)encodeValueOfObjCType:(const char *)valueType at:(const void *)address
{
	(void)valueType;
	(void)address;
	fn_abstract_sequential([self class], _cmd);
}

/* THE SIZED READING DOOR IS THE MODERN SPELLING, and "a subclass must supply this" is exactly where the
 * abstract base's statement belongs — Apple's own documentation says so ("Subclasses must override this
 * method and provide an implementation to decode the value"). THE UN-SIZED DOOR IS A BASE IMPLEMENTATION
 * that sizes the type code itself and funnels in: that is the shape Apple deprecates (the size is guessed
 * from the code rather than declared by the caller), and funnelling is what keeps a caller who still sends
 * the old selector on the ONE decoding path every subclass already implements. */
- (void)decodeValueOfObjCType:(const char *)valueType at:(void *)data size:(NSUInteger)size
{
	(void)valueType;
	(void)data;
	(void)size;
	fn_abstract_sequential([self class], _cmd);
}

- (void)decodeValueOfObjCType:(const char *)valueType at:(void *)data
{
	NSUInteger size = 0;

	if (valueType == NULL) {
		[NSException raise:NSInvalidArgumentException format:@"a nil type code has no type"];
	}
	NSGetSizeAndAlignment(valueType, &size, NULL);
	[self decodeValueOfObjCType:valueType at:data size:size];
}

/* --- THE ARRAY DOORS, WHICH APPLE'S OWN DOCUMENTATION PUTS ON THIS BASE (see NSCoder.h) ------------ */

- (void)encodeArrayOfObjCType:(const char *)type count:(NSUInteger)count at:(const void *)array
{
	NSUInteger stride = 0;
	NSUInteger i;

	if (type == NULL) {
		[NSException raise:NSInvalidArgumentException format:@"a nil type code has no type"];
	}
	/* THE STRIDE IS THE TYPE'S OWN SIZE, which is what an array of it occupies in memory — the same
	 * `NSGetSizeAndAlignment` the wire and NSValue's box use, so the three cannot disagree. */
	NSGetSizeAndAlignment(type, &stride, NULL);
	for (i = 0; i < count; i++) {
		[self encodeValueOfObjCType:type at:(const void *)((const char *)array + i * stride)];
	}
}

- (void)decodeArrayOfObjCType:(const char *)itemType count:(NSUInteger)count at:(void *)array
{
	NSUInteger stride = 0;
	NSUInteger i;

	if (itemType == NULL) {
		[NSException raise:NSInvalidArgumentException format:@"a nil type code has no type"];
	}
	NSGetSizeAndAlignment(itemType, &stride, NULL);
	for (i = 0; i < count; i++) {
		[self decodeValueOfObjCType:itemType at:(char *)array + i * stride];
	}
}

/* --- THE UNKEYED GEOMETRY DOORS ---------------------------------------------------------------------
 *
 * Each one is the type-code door applied to its struct, which is Apple's own description of them ("invoke
 * -encodeValueOfObjCType:at: and must be matched by a -decodePoint in order"). WHAT THEY ARE NOT is a
 * second wire: the pair is spelled by `@encode(NSPoint)` — `"{CGPoint=dd}"` here, because NSGeometry.h
 * makes NSPoint a typedef of the CG type — and the sequential wire is what carries a struct.
 *
 * A KEYED CODER REACHES ITS OWN `-encodeValueOfObjCType:at:`, which raises the sequential refusal and
 * names the family that answers it: these doors are the sequential family's, and on the keyed one they
 * fail exactly as every other one of its doors does. */
- (void)encodePoint:(NSPoint)point { [self encodeValueOfObjCType:@encode(NSPoint) at:&point]; }

- (void)encodeSize:(NSSize)size { [self encodeValueOfObjCType:@encode(NSSize) at:&size]; }

- (void)encodeRect:(NSRect)rect { [self encodeValueOfObjCType:@encode(NSRect) at:&rect]; }

- (NSPoint)decodePoint
{
	NSPoint point = { 0, 0 };

	[self decodeValueOfObjCType:@encode(NSPoint) at:&point];
	return point;
}

- (NSSize)decodeSize
{
	NSSize size = { 0, 0 };

	[self decodeValueOfObjCType:@encode(NSSize) at:&size];
	return size;
}

- (NSRect)decodeRect
{
	NSRect rect = { { 0, 0 }, { 0, 0 } };

	[self decodeValueOfObjCType:@encode(NSRect) at:&rect];
	return rect;
}

/* --- THE BYTES DOOR WITH A FLOOR UNDER IT ----------------------------------------------------------
 *
 * `-decodeBytesWithReturnedLength:` is the reading primitive; this is the caller who also says how SHORT a
 * run it will accept, and Apple's own text says what a violation does: "the decoder uses -failWithError:
 * to fail the entire decode operation. The result of that is configurable on a per-NSCoder basis using
 * -decodingFailurePolicy." A run that is PRESENT but short is a corrupt archive rather than a shorter
 * value — the same rule the keyed twin states — and going through `-failWithError:` is what makes the
 * outcome the receiver's policy instead of a second refusal rule invented here. */
- (nullable const void *)decodeBytesWithMinimumLength:(NSUInteger)minimumLength
{
	NSUInteger length = 0;
	const void *bytes = [self decodeBytesWithReturnedLength:&length];

	if (bytes != NULL && length < minimumLength) {
		[self failWithError:
			[NSError errorWithDomain:NSCocoaErrorDomain
					    code:NSCoderInvalidValueError
					userInfo:@{ NSLocalizedDescriptionKey :
			[NSString stringWithFormat:@"the decoder holds %lu bytes, fewer than the %lu required",
				(unsigned long)length, (unsigned long)minimumLength] }]];
		return NULL;
	}
	return bytes;
}

- (void)encodeObject:(nullable id)object
{
	(void)object;
	fn_abstract_sequential([self class], _cmd);
}

- (nullable id)decodeObject
{
	fn_abstract_sequential([self class], _cmd);
	return nil;
}

- (void)encodeDataObject:(NSData *)data
{
	(void)data;
	fn_abstract_sequential([self class], _cmd);
}

- (nullable NSData *)decodeDataObject
{
	fn_abstract_sequential([self class], _cmd);
	return nil;
}

- (void)encodeBytes:(nullable const void *)bytesp length:(NSUInteger)length
{
	(void)bytesp;
	(void)length;
	fn_abstract_sequential([self class], _cmd);
}

- (nullable const void *)decodeBytesWithReturnedLength:(NSUInteger *)lengthp
{
	(void)lengthp;
	fn_abstract_sequential([self class], _cmd);
	return NULL;
}

/* --- THE LEGACY SEQUENTIAL PLIST AND BULK DOORS (see the note in NSCoder.h) ------------------------- */

/* A property list IS an object, so the door is the object door — which is why these two doors are NOT
 * abstract: they are the same equivalence the bycopy/byref doors express, and they reach whichever
 * `-encodeObject:`/`-decodeObject` the receiver's family supplies. */
- (void)encodePropertyList:(nullable id)aPropertyList
{
	[self encodeObject:aPropertyList];
}

- (nullable id)decodePropertyList
{
	return [self decodeObject];
}

/* A RUN OF VALUES, LEFT TO RIGHT: each code in `types` names one argument, every argument is the ADDRESS
 * of its value, and the code that follows is where `NSGetSizeAndAlignment` stops reading. A code that does
 * not advance would spin forever, so that one case is broken out rather than looped. */
- (void)encodeValuesOfObjCTypes:(const char *)types, ...
{
	va_list ap;

	va_start(ap, types);
	while (types != NULL && types[0] != '\0') {
		const char *next;
		const void *address = va_arg(ap, const void *);

		[self encodeValueOfObjCType:types at:address];
		next = NSGetSizeAndAlignment(types, NULL, NULL);
		if (next == types) {
			break;
		}
		types = next;
	}
	va_end(ap);
}

- (void)decodeValuesOfObjCTypes:(const char *)types, ...
{
	va_list ap;

	va_start(ap, types);
	while (types != NULL && types[0] != '\0') {
		const char *next;
		void *address = va_arg(ap, void *);

		[self decodeValueOfObjCType:types at:address];
		next = NSGetSizeAndAlignment(types, NULL, NULL);
		if (next == types) {
			break;
		}
		types = next;
	}
	va_end(ap);
}

- (NSInteger)versionForClassName:(NSString *)className
{
	(void)className;
	fn_abstract_sequential([self class], _cmd);
	return 0;
}

@end
