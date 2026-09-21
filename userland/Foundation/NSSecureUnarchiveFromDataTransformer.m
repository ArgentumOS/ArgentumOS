/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSSecureUnarchiveFromDataTransformer.m — W9. See the header for why the class exists.
 *
 * THE REFUSAL IS THE FEATURE, so it is written as an early answer rather than a last resort: every path that
 * cannot vouch for the object answers nil, and only a top-level object that IS one of the allowed classes is
 * handed back. `nil` is also what the transformer answers for input that is not data at all, which keeps one
 * answer for every "no".
 *
 * MANUAL OWNERSHIP (MRC): the upstream value is autoreleased by the class method, so nothing here is
 * retained past the call.
 */

#import <Foundation/NSSecureUnarchiveFromDataTransformer.h>
#import <Foundation/NSKeyedArchiver.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSSet.h>
#import <Foundation/NSString.h>
#import <Foundation/NSData.h>
#import <Foundation/NSDate.h>
#import <Foundation/NSNull.h>
#import <Foundation/NSNumber.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSURL.h>
#import <Foundation/NSUUID.h>

/* THE NAME IS THE CLASS NAME, which is what makes it resolve without registration. */
NSValueTransformerName const NSSecureUnarchiveFromDataTransformerName =
	@"NSSecureUnarchiveFromDataTransformer";

@implementation NSSecureUnarchiveFromDataTransformer

+ (NSArray *)allowedTopLevelClasses
{
	static NSArray *allowed = nil;

	if (allowed == nil) {
		allowed = [[NSArray alloc] initWithObjects:
			[NSArray class], [NSMutableArray class],
			[NSDictionary class], [NSMutableDictionary class],
			[NSSet class], [NSMutableSet class],
			[NSString class], [NSMutableString class],
			[NSNumber class], [NSData class], [NSMutableData class],
			[NSDate class], [NSNull class], [NSURL class], [NSUUID class], nil];
	}
	return allowed;
}

+ (Class)transformedValueClass
{
	/* THE OUTPUT IS ARBITRARY — it is whatever the archive held — so the honest answer is the root class. */
	return [NSObject class];
}

+ (BOOL)allowsReverseTransformation
{
	return YES;
}

- (nullable id)transformedValue:(nullable id)value
{
	NSArray *allowed;
	id decoded;
	NSUInteger i;

	if (![value isKindOfClass:[NSData class]]) {
		return nil;
	}
	decoded = [NSKeyedUnarchiver unarchiveObjectWithData:(NSData *)value];
	if (decoded == nil) {
		return nil;
	}
	/* THE TOP-LEVEL CHECK: an archive whose root is not one of the allowed classes is REFUSED, whatever it
	 * decoded to. This is the property's own promise — `+allowedTopLevelClasses` names the top level. */
	allowed = [[self class] allowedTopLevelClasses];
	for (i = 0; i < [allowed count]; i++) {
		if ([decoded isKindOfClass:(Class)[allowed objectAtIndex:i]]) {
			return decoded;
		}
	}
	return nil;
}

- (nullable id)reverseTransformedValue:(nullable id)value
{
	if (value == nil) {
		return nil;
	}
	return [NSKeyedArchiver archivedDataWithRootObject:value];
}

@end
