/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSValueTransformer.m — the implementation (W2h).
 */
#import <Foundation/NSValueTransformer.h>
#import <Foundation/NSDictionary.h>	/* NSMutableDictionary lives here */
#import <Foundation/NSException.h>
#import <Foundation/NSObject.h>	/* NSStringFromClass */
#import <objc/runtime.h>

static NSMutableDictionary *fn_transformers(void)
{
	static NSMutableDictionary *registry = nil;

	if (registry == nil) {
		registry = [[NSMutableDictionary alloc] init];
	}
	return registry;
}

@implementation NSValueTransformer

+ (void)setValueTransformer:(NSValueTransformer *)transformer forName:(NSString *)name
{
	if (transformer == nil) {
		[fn_transformers() removeObjectForKey:name];
		return;
	}
	[fn_transformers() setObject:transformer forKey:name];
}

+ (NSValueTransformer *)valueTransformerForName:(NSString *)name
{
	NSValueTransformer *found = (name != nil) ? [fn_transformers() objectForKey:name] : nil;

	if (found != nil) {
		return found;
	}
	/* THE FALLBACK IS THE REASON A TRANSFORMER NEEDS NO REGISTRATION: if the name is a class, that
	 * class IS the transformer, which is how Cocoa's bindings find one by the name in a nib or a
	 * model. Only a subclass of this class is usable, and anything else answers nil rather than
	 * being registered under a name that would then return something that cannot transform. */
	if (name != nil) {
		Class candidate = NSClassFromString(name);

		if (candidate != Nil && [candidate isSubclassOfClass:[NSValueTransformer class]]) {
			return [[candidate alloc] init];
		}
	}
	return nil;
}

+ (NSArray *)valueTransformerNames
{
	return [fn_transformers() allKeys];
}

+ (Class)transformedValueClass
{
	return [NSObject class];
}

+ (BOOL)allowsReverseTransformation
{
	return NO;
}

- (id)transformedValue:(id)value
{
	/* THE CLASS GOES IN AS A STRING, not as a %@ argument: formatting a Class through %@ is a
	 * message to the metaclass this library does not answer for, and a raise that crashes instead
	 * of raising is the one failure an abstract method cannot afford. */
	[NSException raise:NSInvalidArgumentException
		    format:@"-[%@ transformedValue:] is abstract: a value transformer subclass must override it",
			   NSStringFromClass([self class])];
	return nil;	/* -raise: does not return */
}

- (id)reverseTransformedValue:(id)value
{
	[NSException raise:NSInvalidArgumentException
		    format:@"-[%@ reverseTransformedValue:] is abstract: a value transformer subclass must override it",
			   NSStringFromClass([self class])];
	return nil;
}

+ (NSString *)description
{
	return [NSString stringWithFormat:@"<%@: %@>", [self class], [self transformedValueClass]];
}

@end
