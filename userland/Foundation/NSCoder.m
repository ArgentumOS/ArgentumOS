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

- (NSInteger)versionForClassName:(NSString *)className
{
	(void)className;
	fn_abstract_sequential([self class], _cmd);
	return 0;
}

@end
