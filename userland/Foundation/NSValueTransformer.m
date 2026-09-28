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
#import <Foundation/NSArray.h>
#import <Foundation/NSNumber.h>
#import <Foundation/NSData.h>
#import <Foundation/NSKeyedArchiver.h>	/* and NSKeyedUnarchiver, which lives in the same header */
#import <Foundation/NSArchiver.h>	/* and NSUnarchiver, likewise */
#import <Foundation/NSAutoreleasePool.h>
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

/* ------------------------------------------------------------------ THE FIVE NAMES APPLE REGISTERS (§62.96)
 *
 * WHY THEY ARE REGISTERED RATHER THAN NAMED AFTER THEIR CLASSES. This registry can already resolve a NAME by
 * looking for a class of that name (see `+valueTransformerForName:`), and the one transformer that shipped
 * before this unit uses exactly that trick. These five cannot: `+valueTransformerNames` is a documented door
 * and Apple's answer INCLUDES them, so they have to be IN the registry — a class-name trick would make the
 * transformer findable and the list still empty, which is the difference the probe checks first.
 *
 * ONE CLASS PER BEHAVIOUR, because the registry keeps INSTANCES: `+valueTransformerForName:` answers the object
 * it holds, so a mode flag inside one class would work and would make the class's own name say nothing about
 * what it does.
 *
 * AND NIL IS THE ANSWER FOR WHAT CANNOT BE TRANSFORMED, which is the base class's own contract rather than an
 * invention: the two Boolean questions can answer about anything (that is their whole purpose), the negator
 * refuses what is not a number, and the two unarchivers refuse what is not data.
 */

NSValueTransformerName const NSIsNilTransformerName = @"NSIsNil";
NSValueTransformerName const NSIsNotNilTransformerName = @"NSIsNotNil";
NSValueTransformerName const NSNegateBooleanTransformerName = @"NSNegateBoolean";
NSValueTransformerName const NSKeyedUnarchiveFromDataTransformerName = @"NSKeyedUnarchiveFromData";
NSValueTransformerName const NSUnarchiveFromDataTransformerName = @"NSUnarchiveFromData";

/* THE CLASSES ARE PRIVATE TO THIS FILE: a caller asks for one of these by NAME (that is the API), so whom the
 * registry holds is this library's business. */
@interface FNIsNilTransformer : NSValueTransformer
@end

@interface FNIsNotNilTransformer : NSValueTransformer
@end

@interface FNNegateBooleanTransformer : NSValueTransformer
@end

@interface FNKeyedUnarchiveFromDataTransformer : NSValueTransformer
@end

@interface FNUnarchiveFromDataTransformer : NSValueTransformer
@end

@implementation FNIsNilTransformer
- (id)transformedValue:(id)value
{
	return [NSNumber numberWithBool:(value == nil)];
}
@end

@implementation FNIsNotNilTransformer
- (id)transformedValue:(id)value
{
	return [NSNumber numberWithBool:(value != nil)];
}
@end

@implementation FNNegateBooleanTransformer
- (id)transformedValue:(id)value
{
	/* A BOOLEAN IS THE DOCUMENTED DOMAIN - the name says so - so anything that is not a number answers nil
	 * rather than being coerced to a truth value, which would be answering a question nobody asked. */
	if (![value isKindOfClass:[NSNumber class]]) {
		return nil;
	}
	return [NSNumber numberWithBool:![value boolValue]];
}
@end

@implementation FNKeyedUnarchiveFromDataTransformer
- (id)transformedValue:(id)value
{
	if (![value isKindOfClass:[NSData class]]) {
		return nil;
	}
	return [NSKeyedUnarchiver unarchiveObjectWithData:value];
}
@end

@implementation FNUnarchiveFromDataTransformer
- (id)transformedValue:(id)value
{
	if (![value isKindOfClass:[NSData class]]) {
		return nil;
	}
	return [NSUnarchiver unarchiveObjectWithData:value];
}
@end

/* THE REGISTRATION HAPPENS AT LOAD, the standing this library already gave its own URL transport (§62.83): a
 * program that asks for one of these names by name never has to know that anything has to be set up first.
 * The instances are owned by the registry, which is why nothing here releases them. */
__attribute__((constructor))
static void fn_register_standard_value_transformers(void)
{
	NSValueTransformer *each;

	each = [[FNIsNilTransformer alloc] init];
	[NSValueTransformer setValueTransformer:each forName:NSIsNilTransformerName];
	each = [[FNIsNotNilTransformer alloc] init];
	[NSValueTransformer setValueTransformer:each forName:NSIsNotNilTransformerName];
	each = [[FNNegateBooleanTransformer alloc] init];
	[NSValueTransformer setValueTransformer:each forName:NSNegateBooleanTransformerName];
	each = [[FNKeyedUnarchiveFromDataTransformer alloc] init];
	[NSValueTransformer setValueTransformer:each forName:NSKeyedUnarchiveFromDataTransformerName];
	each = [[FNUnarchiveFromDataTransformer alloc] init];
	[NSValueTransformer setValueTransformer:each forName:NSUnarchiveFromDataTransformerName];
}
