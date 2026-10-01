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

/* `-failWithError:` IS THE DECODER'S "I CANNOT ANSWER", and the abstract base has no answer to give —
 * so it raises, like the doors that would have called it. `NSKeyedUnarchiver` overrides it to honour
 * `-decodingFailurePolicy`. */
- (void)failWithError:(NSError *)error
{
	(void)error;
	fn_abstract([self class], _cmd);
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

/* --- THE KEYED GEOMETRY DOORS (see the note in NSCoder.h): the keyed family answers them, so the abstract
 * base raises like every other keyed door. ----------------------------------------------------------------- */
- (void)encodeCGPoint:(CGPoint)point forKey:(NSString *)key
{
	(void)point;
	(void)key;
	fn_abstract([self class], _cmd);
}

- (CGPoint)decodeCGPointForKey:(NSString *)key
{
	(void)key;
	fn_abstract([self class], _cmd);
	return (CGPoint){ 0, 0 };
}

- (void)encodeCGSize:(CGSize)size forKey:(NSString *)key
{
	(void)size;
	(void)key;
	fn_abstract([self class], _cmd);
}

- (CGSize)decodeCGSizeForKey:(NSString *)key
{
	(void)key;
	fn_abstract([self class], _cmd);
	return (CGSize){ 0, 0 };
}

- (void)encodeCGRect:(CGRect)rect forKey:(NSString *)key
{
	(void)rect;
	(void)key;
	fn_abstract([self class], _cmd);
}

- (CGRect)decodeCGRectForKey:(NSString *)key
{
	(void)key;
	fn_abstract([self class], _cmd);
	return (CGRect){ { 0, 0 }, { 0, 0 } };
}

- (void)encodeCGVector:(CGVector)vector forKey:(NSString *)key
{
	(void)vector;
	(void)key;
	fn_abstract([self class], _cmd);
}

- (CGVector)decodeCGVectorForKey:(NSString *)key
{
	(void)key;
	fn_abstract([self class], _cmd);
	return (CGVector){ 0, 0 };
}

- (void)encodeCGAffineTransform:(CGAffineTransform)transform forKey:(NSString *)key
{
	(void)transform;
	(void)key;
	fn_abstract([self class], _cmd);
}

- (CGAffineTransform)decodeCGAffineTransformForKey:(NSString *)key
{
	(void)key;
	fn_abstract([self class], _cmd);
	/* ⚠ THE IDENTITY'S VALUE, WRITTEN OUT — NOT `CGAffineTransformIdentity`. That constant is a SYMBOL IN
	 * libcoregraphics, and THIS library does not link the drawing library (mk/20-userland.mk stages it as its
	 * own library precisely so the dependency does not exist): naming it here left `libfoundation.so` with an
	 * undefined reference that broke the guest tool link - measured, `make testimg` exit 2. A constant is a
	 * VALUE; writing it out costs one line and keeps Foundation free of a drawing library it has no business
	 * needing. The same reasoning the probe's geometry checks follow when they build CG structs by hand. */
	return (CGAffineTransform){1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
}

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

- (void)decodeValueOfObjCType:(const char *)valueType at:(void *)data
{
	(void)valueType;
	(void)data;
	fn_abstract_sequential([self class], _cmd);
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
