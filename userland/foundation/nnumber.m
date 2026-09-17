/*
 * nnumber.m — the boxed scalar.
 *
 * ARC file: nothing here implements -retain/-release.
 */

#import <foundation/NSNumber.h>
#import <foundation/NSString.h>
#include <stdio.h>

@implementation NSNumber

+ (NSNumber *)numberWithInt:(int)value
{
	return [[self alloc] initWithInt:value];
}

+ (NSNumber *)numberWithLongLong:(long long)value
{
	return [[self alloc] initWithLongLong:value];
}

+ (NSNumber *)numberWithUnsignedLongLong:(unsigned long long)value
{
	return [[self alloc] initWithUnsignedLongLong:value];
}

+ (NSNumber *)numberWithDouble:(double)value
{
	return [[self alloc] initWithDouble:value];
}

+ (NSNumber *)numberWithBool:(BOOL)value
{
	return [[self alloc] initWithBool:value];
}

- (id)initWithInt:(int)value
{
	return [self initWithLongLong:(long long)value];
}

- (id)initWithLongLong:(long long)value
{
	self = [super init];
	if (self != nil) {
		_value._signedValue = value;
		_kind = 'i';
	}
	return self;
}

- (id)initWithUnsignedLongLong:(unsigned long long)value
{
	self = [super init];
	if (self != nil) {
		_value._unsignedValue = value;
		_kind = 'u';
	}
	return self;
}

- (id)initWithDouble:(double)value
{
	self = [super init];
	if (self != nil) {
		_value._doubleValue = value;
		_kind = 'd';
	}
	return self;
}

- (id)initWithBool:(BOOL)value
{
	return [self initWithLongLong:value ? 1 : 0];
}

- (int)intValue
{
	return (int)[self longLongValue];
}

- (long long)longLongValue
{
	switch (_kind) {
	case 'u': return (long long)_value._unsignedValue;
	case 'd': return (long long)_value._doubleValue;
	default:  return _value._signedValue;
	}
}

- (unsigned long long)unsignedLongLongValue
{
	switch (_kind) {
	case 'u': return _value._unsignedValue;
	case 'd': return (unsigned long long)_value._doubleValue;
	default:  return (unsigned long long)_value._signedValue;
	}
}

- (double)doubleValue
{
	switch (_kind) {
	case 'u': return (double)_value._unsignedValue;
	case 'i': return (double)_value._signedValue;
	default:  return _value._doubleValue;
	}
}

- (BOOL)boolValue
{
	return ([self doubleValue] != 0.0) || ([self longLongValue] != 0);
}

- (BOOL)isEqualToNumber:(NSNumber *)other
{
	if (other == nil) {
		return NO;
	}
	if (other == self) {
		return YES;
	}
	/*
	 * Two integers compare as integers: as doubles they would lose precision
	 * above 2^53 and call two different numbers equal.
	 */
	if (_kind != 'd' && other->_kind != 'd') {
		if (_kind == 'u' || other->_kind == 'u') {
			return [self unsignedLongLongValue] ==
			       [other unsignedLongLongValue];
		}
		return [self longLongValue] == [other longLongValue];
	}
	return [self doubleValue] == [other doubleValue];
}

- (NSComparisonResult)compare:(NSNumber *)other
{
	double a = [self doubleValue];
	double b = [other doubleValue];

	if (a < b) {
		return NSOrderedAscending;
	}
	if (a > b) {
		return NSOrderedDescending;
	}
	return NSOrderedSame;
}

/*
 * THE AUDIT'S FIX A. A number is immutable, so copying returns self — and the
 * conformance is what makes a number usable as a dictionary KEY, since the table
 * copies its keys. Without it, [key copy] answered nil and the table filed a
 * phantom entry.
 */
- (id)copy
{
	return self;
}

- (id)mutableCopy
{
	return self;
}

- (id)copyWithZone:(NSZone *)zone
{
	(void)zone;
	return self;
}

- (id)mutableCopyWithZone:(NSZone *)zone
{
	(void)zone;
	return self;
}

- (BOOL)isEqual:(id)other
{
	if (other == self) {
		return YES;
	}
	if (other == nil || ![other isKindOfClass:[NSNumber class]]) {
		return NO;
	}
	return [self isEqualToNumber:(NSNumber *)other];
}

- (unsigned long)hash
{
	/*
	 * An integral value hashes as the integer it is, so 1 and 1.0 agree — which
	 * they must, because -isEqual: says they are equal. Only a genuinely
	 * fractional value hashes from its bits.
	 */
	double d = [self doubleValue];
	long long whole = (long long)d;
	unsigned long h = 2166136261UL;

	if ((double)whole == d) {
		h ^= (unsigned long)whole;
		return h * 16777619UL;
	}
	{
		union {
			double d;
			unsigned long u;
		} bits;

		bits.d = d;
		h ^= bits.u;
		return h * 16777619UL;
	}
}

- (NSString *)description
{
	char buffer[48];

	switch (_kind) {
	case 'u':
		snprintf(buffer, sizeof buffer, "%llu", _value._unsignedValue);
		break;
	case 'd':
		snprintf(buffer, sizeof buffer, "%g", _value._doubleValue);
		break;
	default:
		snprintf(buffer, sizeof buffer, "%lld", _value._signedValue);
		break;
	}
	return [NSString stringWithUTF8String:buffer];
}

@end
