/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSPersonNameComponents.m — the name bag. docs/design/foundation-plan.md §12.3 W11.
 *
 * MANUAL OWNERSHIP (MRC: the whole library is — mk/20-userland.mk says so in as many words). Six
 * strings and one nested components object are OWNED, so every setter is a release-then-retain-copy
 * and `-dealloc` releases all seven. There is no `@synthesize` to lean on, because there is no
 * @property to synthesise.
 *
 * WHY THE SETTERS COPY RATHER THAN RETAIN: Apple declares these `@property (copy)`, and the reason is
 * the reason it always is — a caller may hand in an NSMutableString and then mutate it, and a name
 * that changes under the object that owns it is a bug the class can prevent for one allocation. The
 * nested `phoneticRepresentation` is copy-assigned for the same reason: a components object is
 * mutable, so retaining it would let a caller edit a component through the owner.
 *
 * NSCoding/NSSecureCoding: seven keys, ours (an archive's key spelling is internal to this library's
 * own coder, as in NSDate's). `+supportsSecureCoding` answers YES because this class is exactly what
 * the protocol is FOR — a bag of strings and one other supported class, none of whose keys can name a
 * type the decoder would have to invent. The enforcement is the decoder's and is named in NSCoding.h.
 */

#import <Foundation/NSPersonNameComponents.h>
#import <Foundation/NSString.h>
#import <Foundation/NSCoder.h>

@implementation NSPersonNameComponents

- (id)init
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	/* All seven absent, which is a legitimate state: a name with no known components. */
	_namePrefix = nil;
	_givenName = nil;
	_middleName = nil;
	_familyName = nil;
	_nameSuffix = nil;
	_nickname = nil;
	_phoneticRepresentation = nil;
	return self;
}

- (void)dealloc
{
	[_namePrefix release];
	[_givenName release];
	[_middleName release];
	[_familyName release];
	[_nameSuffix release];
	[_nickname release];
	[_phoneticRepresentation release];
	[super dealloc];
}

/* One shape, six times: an absent value is nil, and nil is what the setter stores. */
- (nullable NSString *)namePrefix { return _namePrefix; }
- (void)setNamePrefix:(nullable NSString *)value
{
	id copy = [value copy];
	[_namePrefix release];
	_namePrefix = copy;
}

- (nullable NSString *)givenName { return _givenName; }
- (void)setGivenName:(nullable NSString *)value
{
	id copy = [value copy];
	[_givenName release];
	_givenName = copy;
}

- (nullable NSString *)middleName { return _middleName; }
- (void)setMiddleName:(nullable NSString *)value
{
	id copy = [value copy];
	[_middleName release];
	_middleName = copy;
}

- (nullable NSString *)familyName { return _familyName; }
- (void)setFamilyName:(nullable NSString *)value
{
	id copy = [value copy];
	[_familyName release];
	_familyName = copy;
}

- (nullable NSString *)nameSuffix { return _nameSuffix; }
- (void)setNameSuffix:(nullable NSString *)value
{
	id copy = [value copy];
	[_nameSuffix release];
	_nameSuffix = copy;
}

- (nullable NSString *)nickname { return _nickname; }
- (void)setNickname:(nullable NSString *)value
{
	id copy = [value copy];
	[_nickname release];
	_nickname = copy;
}

- (nullable NSPersonNameComponents *)phoneticRepresentation { return _phoneticRepresentation; }
- (void)setPhoneticRepresentation:(nullable NSPersonNameComponents *)value
{
	/* `-copy`, not a retain: a components object is mutable, and an owner that shared one would let
	 * its caller rewrite it from outside. See the header's nesting note. */
	id copy = [value copy];
	[_phoneticRepresentation release];
	_phoneticRepresentation = copy;
}

- (id)copy
{
	NSPersonNameComponents *copy = [[NSPersonNameComponents alloc] init];

	/* Through the setters, so the copy is deep in exactly the way the setters are: the six strings
	 * and the nested representation are all copied rather than shared. */
	[copy setNamePrefix:_namePrefix];
	[copy setGivenName:_givenName];
	[copy setMiddleName:_middleName];
	[copy setFamilyName:_familyName];
	[copy setNameSuffix:_nameSuffix];
	[copy setNickname:_nickname];
	[copy setPhoneticRepresentation:_phoneticRepresentation];
	return copy;
}

/*
 * NSCoding. Seven keys, spelled by us — an archive's internal keys are this library's business and a
 * program never sees them (NSDate's comment makes the same point about its one key).
 */
+ (BOOL)supportsSecureCoding
{
	return YES;
}

- (void)encodeWithCoder:(NSCoder *)coder
{
	[coder encodeObject:_namePrefix forKey:@"NS.namePrefix"];
	[coder encodeObject:_givenName forKey:@"NS.givenName"];
	[coder encodeObject:_middleName forKey:@"NS.middleName"];
	[coder encodeObject:_familyName forKey:@"NS.familyName"];
	[coder encodeObject:_nameSuffix forKey:@"NS.nameSuffix"];
	[coder encodeObject:_nickname forKey:@"NS.nickname"];
	[coder encodeObject:_phoneticRepresentation forKey:@"NS.phoneticRepresentation"];
}

- (instancetype)initWithCoder:(NSCoder *)coder
{
	self = [self init];
	if (self == nil) {
		return nil;
	}
	/* The setters, so a key that was never written leaves its component absent. */
	[self setNamePrefix:[coder decodeObjectForKey:@"NS.namePrefix"]];
	[self setGivenName:[coder decodeObjectForKey:@"NS.givenName"]];
	[self setMiddleName:[coder decodeObjectForKey:@"NS.middleName"]];
	[self setFamilyName:[coder decodeObjectForKey:@"NS.familyName"]];
	[self setNameSuffix:[coder decodeObjectForKey:@"NS.nameSuffix"]];
	[self setNickname:[coder decodeObjectForKey:@"NS.nickname"]];
	[self setPhoneticRepresentation:[coder decodeObjectForKey:@"NS.phoneticRepresentation"]];
	return self;
}

@end
