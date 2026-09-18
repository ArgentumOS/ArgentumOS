/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * nscoder.m — the abstract base (F13.12). MANUAL OWNERSHIP.
 *
 * EVERY DOOR RAISES, and that is the class's whole content: a coder that cannot encode has no
 * sensible answer for any of them, and answering a zero would be indistinguishable from data. The
 * message NAMES the door and the class, so a caller who reached here by mistake knows which half of
 * the pair is missing — an archiver where a unarchiver was wanted, or a subclass that implemented
 * only some of the protocol.
 */

#import <foundation/NSCoder.h>
#import <foundation/NSString.h>
#import <foundation/NSException.h>

static void fn_abstract(Class cls, SEL door)
{
	[NSException raise:NSInvalidArgumentException
		    format:@"%@: -%s is a door the abstract NSCoder does not implement — "
			   "use NSKeyedArchiver or NSKeyedUnarchiver", cls, sel_getName(door)];
}

@implementation NSCoder

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

@end
